/* Moteur à poignées du pont JNI (voir handles.h). Sans jni.h : compilé aussi dans les tests PC.
 *
 * Protocole de la table (un seul mutex g_mu, une seule condition g_cv) :
 *   - acquire : entrée valide, du bon type et pas « en fermeture » → refs++ ; l'opération longue
 *     se fait ensuite HORS du mutex ; release : refs-- (et réveil si une fermeture attend).
 *   - fermeture : closing = vrai (plus aucun acquire ne réussit), attente de refs == 0, destruction
 *     de l'objet HORS du mutex, puis libération de l'entrée (génération + 1) et réveil.
 *   - une entrée en fermeture garde son type et son parent jusqu'à la fin de la destruction : un
 *     parent qui ferme ses enfants attend aussi ceux qu'un autre fil est en train de fermer.
 * Les enfants (volumes d'un disque, fichiers d'un volume, archives d'un fichier) sont créés pendant que l'opération tient
 * une référence sur le parent : une fois les références du parent retombées à 0, plus aucun enfant
 * ne peut apparaître. */
#define _POSIX_C_SOURCE 200809L
#include "handles.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fluluks/archive.h"
#include "fluluks/blockdev.h"
#include "fluluks/cache.h"
#include "cJSON.h"
#include "fluluks/error.h"
#include "fluluks/format.h"
#include "fluluks/fs.h"
#include "fluluks/luks.h"
#include "fluluks/partition.h"
#include "fluluks/probe.h"
#include "fluluks/usb.h"

#define CACHE_BLOCK 65536u
#define CACHE_BLOCKS 256u

enum { T_FREE = 0, T_DISK = 1, T_VOL = 2, T_FILE = 3, T_ARC = 4 };

typedef struct {
  uint32_t gen; /* >= 1 dès la première utilisation */
  uint8_t type;
  bool closing;
  uint32_t refs;
  int64_t parent; /* poignée du parent (0 pour un disque) */
  void *obj;
} slot;

static slot g_tab[FJ_MAX];
static unsigned g_next; /* allocation tournante : une entrée libérée n'est pas réutilisée tout de suite */
static unsigned g_used;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;

/* Détruit l'objet ; rend le code de la fermeture (synchronisation, démontage). */
typedef int (*destroy_fn)(void *obj, int64_t h);

static int64_t mkhandle(unsigned idx) { return (int64_t)(((uint64_t)g_tab[idx].gen << 16) | idx); }

/* Sous g_mu. NULL si la poignée est invalide, périmée ou d'un autre type. */
static slot *lookup(int64_t h, int type) {
  if (h <= 0) return NULL;
  uint64_t u = (uint64_t)h, idx = u & 0xFFFFu, gen = u >> 16;
  if (idx >= FJ_MAX || gen == 0 || gen > UINT32_MAX) return NULL;
  slot *s = &g_tab[idx];
  if (s->type != type || s->gen != (uint32_t)gen) return NULL;
  return s;
}

/* Enregistre un objet. Un enfant (volume, fichier) n'est jamais enregistré sous un parent en cours de
 * fermeture : FL_EBUSY (transitoire, la fermeture du parent va aboutir) ; parent disparu : FL_EINVAL.
 * L'appelant libère l'objet quand le résultat est < 0. */
static int64_t tab_put(int type, int64_t parent, void *obj) {
  pthread_mutex_lock(&g_mu);
  if (parent != 0) {
    slot *ps = lookup(parent, type - 1); /* T_VOL -> T_DISK, T_FILE -> T_VOL, T_ARC -> T_FILE */
    if (!ps) {
      pthread_mutex_unlock(&g_mu);
      return FL_EINVAL;
    }
    if (ps->closing) {
      pthread_mutex_unlock(&g_mu);
      return FL_EBUSY;
    }
  }
  for (unsigned k = 0; k < FJ_MAX; k++) {
    unsigned i = (g_next + k) % FJ_MAX;
    slot *s = &g_tab[i];
    if (s->type != T_FREE) continue;
    if (s->gen == 0) s->gen = 1;
    s->type = (uint8_t)type;
    s->closing = false;
    s->refs = 0;
    s->parent = parent;
    s->obj = obj;
    g_next = (i + 1) % FJ_MAX;
    g_used++;
    int64_t h = mkhandle(i);
    pthread_mutex_unlock(&g_mu);
    return h;
  }
  pthread_mutex_unlock(&g_mu);
  return FL_ENOMEM;
}

static void *tab_acquire(int64_t h, int type) {
  pthread_mutex_lock(&g_mu);
  slot *s = lookup(h, type);
  void *obj = NULL;
  if (s && !s->closing) {
    s->refs++;
    obj = s->obj;
  }
  pthread_mutex_unlock(&g_mu);
  return obj;
}

static void tab_release(int64_t h, int type) {
  pthread_mutex_lock(&g_mu);
  slot *s = lookup(h, type); /* toujours valide : une fermeture attend refs == 0 */
  if (s && s->refs) {
    s->refs--;
    if (!s->refs && s->closing) pthread_cond_broadcast(&g_cv);
  }
  pthread_mutex_unlock(&g_mu);
}

/* Sous g_mu, s déjà marqué closing : attend les opérations en cours, détruit l'objet hors du mutex,
 * libère l'entrée. Rend la main avec g_mu tenu, et le code du destructeur. */
static int finish_close_locked(slot *s, int64_t h, destroy_fn fn) {
  while (s->refs) pthread_cond_wait(&g_cv, &g_mu);
  void *obj = s->obj;
  pthread_mutex_unlock(&g_mu);
  int rc = fn(obj, h);
  pthread_mutex_lock(&g_mu);
  s->type = T_FREE;
  s->closing = false;
  s->parent = 0;
  s->obj = NULL;
  s->gen = s->gen == UINT32_MAX ? 1 : s->gen + 1;
  g_used--;
  pthread_cond_broadcast(&g_cv);
  return rc;
}

/* Ferme une entrée (FL_EINVAL si invalide ou déjà en fermeture). */
static int tab_close(int64_t h, int type, destroy_fn fn) {
  pthread_mutex_lock(&g_mu);
  slot *s = lookup(h, type);
  if (!s || s->closing) {
    pthread_mutex_unlock(&g_mu);
    return FL_EINVAL;
  }
  s->closing = true;
  int rc = finish_close_locked(s, h, fn);
  pthread_mutex_unlock(&g_mu);
  return rc;
}

/* Sous g_mu : nombre d'enfants de `parent` du type donné qui ne sont pas en fermeture. */
static unsigned live_children(int64_t parent, int type) {
  unsigned n = 0;
  for (unsigned i = 0; i < FJ_MAX; i++)
    if (g_tab[i].type == type && g_tab[i].parent == parent && !g_tab[i].closing) n++;
  return n;
}

/* Ferme tous les enfants de `parent`, et attend ceux qu'un autre fil est en train de fermer. */
static void close_children(int64_t parent, int type, destroy_fn fn) {
  pthread_mutex_lock(&g_mu);
  for (;;) {
    bool others = false;
    unsigned found = FJ_MAX;
    for (unsigned i = 0; i < FJ_MAX; i++) {
      if (g_tab[i].type != type || g_tab[i].parent != parent) continue;
      if (g_tab[i].closing) {
        others = true;
        continue;
      }
      found = i;
      break;
    }
    if (found < FJ_MAX) {
      slot *s = &g_tab[found];
      s->closing = true;
      finish_close_locked(s, mkhandle(found), fn);
      continue;
    }
    if (!others) break;
    pthread_cond_wait(&g_cv, &g_mu);
  }
  pthread_mutex_unlock(&g_mu);
}

int fj_debug_slot(int64_t h, unsigned *refs, bool *closing) {
  int rc = FL_EINVAL;
  pthread_mutex_lock(&g_mu);
  for (int t = T_DISK; t <= T_ARC && rc != FL_OK; t++) {
    slot *s = lookup(h, t);
    if (!s) continue;
    if (refs) *refs = s->refs;
    if (closing) *closing = s->closing;
    rc = FL_OK;
  }
  pthread_mutex_unlock(&g_mu);
  return rc;
}

unsigned fj_debug_used(void) {
  pthread_mutex_lock(&g_mu);
  unsigned n = g_used;
  pthread_mutex_unlock(&g_mu);
  return n;
}

/* ---- objets ---- */

typedef struct {
  fl_usb_transport *usb; /* NULL pour une image */
  fl_bdev *base;         /* fichier image ou usb_bdev */
  fl_bdev counted;       /* base, vidages comptés (fj_debug_flushes) */
  _Atomic uint64_t flushes;
  fl_bdev *dev;          /* cache au-dessus de counted : seul utilisé */
  bool formatting;       /* fj_format en cours (sous g_mu) : fj_mount refusé */
  unsigned mounting;     /* fj_mount en cours (sous g_mu) : fj_format refusé */
} jdisk;

static int counted_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  return fl_bdev_read(((jdisk *)d->priv)->base, lba, count, buf);
}
static int counted_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf) {
  return fl_bdev_write(((jdisk *)d->priv)->base, lba, count, buf);
}
static int counted_flush(fl_bdev *d) {
  jdisk *j = d->priv;
  atomic_fetch_add(&j->flushes, 1);
  return fl_bdev_flush(j->base);
}
static void counted_destroy(fl_bdev *d) { (void)d; /* intégré à jdisk */ }
static const fl_bdev_ops counted_ops = {counted_read, counted_write, counted_flush, counted_destroy};

typedef struct {
  fl_bdev *slice, *plain; /* plain : déchiffré (NULL si en clair) */
  fl_luks *luks;
  fl_fs *fs;
  fl_content kind; /* système de fichiers monté (clé "fs" de fj_statfs) */
} jvol;

static int file_destroy(void *obj, int64_t h); /* ferme d'abord les archives du fichier */

/* Ordre de la phase 1 : fs, déchiffré, luks, tranche. Rend le code du démontage. */
static int vol_free(jvol *v) {
  if (!v) return FL_OK;
  int rc = FL_OK;
  if (v->fs) {
    rc = fl_fs_sync(v->fs);       /* aucun fichier ouvert : close_children est passé avant */
    int ru = fl_fs_unmount(v->fs); /* libère même sur un disque mort */
    if (rc == FL_OK) rc = ru;
  }
  fl_bdev_close(v->plain);
  fl_luks_close(v->luks);
  fl_bdev_close(v->slice);
  free(v);
  return rc;
}

static int vol_destroy(void *obj, int64_t h) {
  close_children(h, T_FILE, file_destroy);
  return vol_free(obj);
}

/* Pile entière avant le transport : le usb_bdev copie le transport sans le posséder. */
static void disk_free(jdisk *d) {
  if (!d) return;
  fl_bdev_close(d->dev);
  fl_bdev_close(d->base);
#if defined(__linux__)
  fl_usbdevfs_transport_free(d->usb);
#endif
  free(d);
}

static int disk_destroy(void *obj, int64_t h) {
  close_children(h, T_VOL, vol_destroy);
  disk_free(obj);
  return FL_OK;
}

/* d->base rempli : ajoute le cache, enregistre le disque. Libère tout en cas d'échec. */
static int64_t disk_register(jdisk *d) {
  d->counted = (fl_bdev){&counted_ops, d->base->sector_size, d->base->sector_count, d->base->read_only, d};
  int rc = fl_cache_bdev_new(&d->counted, CACHE_BLOCK, CACHE_BLOCKS, &d->dev);
  if (rc != FL_OK) {
    d->dev = NULL;
    disk_free(d);
    return rc;
  }
  int64_t h = tab_put(T_DISK, 0, d);
  if (h < 0) disk_free(d);
  return h;
}

int64_t fj_open_image(const char *path, bool read_only) {
  if (!path) return FL_EINVAL;
  jdisk *d = calloc(1, sizeof *d);
  if (!d) return FL_ENOMEM;
  int rc = fl_file_bdev_open(path, 512, read_only, &d->base);
  if (rc != FL_OK) {
    d->base = NULL;
    disk_free(d);
    return rc;
  }
  return disk_register(d);
}

int64_t fj_open_usb(int fd, int iface, int ep_in, int ep_out) {
#if defined(__linux__)
  if (fd < 0 || iface < 0 || iface > 255 || ep_in < 0 || ep_in > 255 || ep_out < 0 || ep_out > 255)
    return FL_EINVAL;
  jdisk *d = calloc(1, sizeof *d);
  if (!d) return FL_ENOMEM;
  int rc = fl_usbdevfs_transport_new(fd, (uint8_t)iface, (uint8_t)ep_in, (uint8_t)ep_out, &d->usb);
  /* en écriture : MODE SENSE dit si le disque est protégé (alors lecture seule, RO_WP au montage) */
  if (rc == FL_OK) rc = fl_usb_bdev_open(d->usb, 0, false, &d->base);
  if (rc != FL_OK) {
    d->base = NULL;
    disk_free(d);
    return rc;
  }
  return disk_register(d);
#else
  (void)fd;
  (void)iface;
  (void)ep_in;
  (void)ep_out;
  return FL_ENOTSUP;
#endif
}

int fj_close_disk(int64_t disk) { return tab_close(disk, T_DISK, disk_destroy); }

/* ---- analyse ---- */

static const char *table_name(fl_pt_kind k) {
  switch (k) {
    case FL_PT_GPT: return "gpt";
    case FL_PT_MBR: return "mbr";
    default: return "none";
  }
}

static bool is_luks(fl_content c) { return c == FL_CONTENT_LUKS1 || c == FL_CONTENT_LUKS2; }

static void scan_part(fl_json *j, fl_bdev *dev, const fl_partition *p) {
  fl_bdev *sl = NULL;
  fl_probe_result pr;
  memset(&pr, 0, sizeof pr);
  fl_luks *l = NULL;
  bool damaged = false;
  int err = fl_slice_bdev_new(dev, p->first_lba, p->sector_count, &sl);
  if (err == FL_OK) err = fl_probe(sl, &pr);
  if (err != FL_OK) {
    memset(&pr, 0, sizeof pr);
  } else if (is_luks(pr.content)) {
    err = fl_luks_open(sl, &l);
  } else if (pr.content == FL_CONTENT_UNKNOWN && fl_luks_open(sl, &l) == FL_OK) {
    /* magie du primaire détruite, en-tête LUKS2 secondaire valide */
    damaged = true;
    pr.content = fl_luks_version(l) == 1 ? FL_CONTENT_LUKS1 : FL_CONTENT_LUKS2;
  }
  pr.label[sizeof pr.label - 1] = 0;
  pr.uuid[sizeof pr.uuid - 1] = 0;

  fl_json_obj_begin(j);
  fl_json_key(j, "index");
  fl_json_u64(j, p->index);
  fl_json_key(j, "first_lba");
  fl_json_u64(j, p->first_lba);
  fl_json_key(j, "size");
  fl_json_u64(j, p->sector_count * dev->sector_size); /* borné par la taille du disque */
  fl_json_key(j, "content");
  fl_json_cstr(j, fl_content_name(pr.content));
  fl_json_key(j, "label");
  fl_json_cstr(j, pr.label);
  fl_json_key(j, "uuid");
  fl_json_cstr(j, pr.uuid);
  fl_json_key(j, "supported");
  fl_json_bool(j, err == FL_OK && fl_content_supported(pr.content));
  if (l) {
    uint32_t kib = 0;
    fl_luks_max_kdf_memory_kib(l, &kib);
    fl_json_key(j, "luks");
    fl_json_obj_begin(j);
    fl_json_key(j, "version");
    fl_json_i64(j, fl_luks_version(l));
    fl_json_key(j, "cipher");
    fl_json_cstr(j, fl_luks_cipher(l));
    fl_json_key(j, "primary_damaged");
    fl_json_bool(j, damaged);
    fl_json_key(j, "max_kdf_memory_kib");
    fl_json_u64(j, kib);
    fl_json_obj_end(j);
  }
  if (err != FL_OK) {
    fl_json_key(j, "err");
    fl_json_i64(j, err);
  }
  fl_json_obj_end(j);
  fl_luks_close(l);
  fl_bdev_close(sl);
}

int fj_scan(int64_t disk, fl_json *out) {
  jdisk *d = tab_acquire(disk, T_DISK);
  if (!d) return FL_EINVAL;
  fl_part_table *t = calloc(1, sizeof *t);
  int rc = t ? fl_part_scan(d->dev, t) : FL_ENOMEM;
  if (rc == FL_OK) {
    fl_json_reset(out);
    fl_json_obj_begin(out);
    fl_json_key(out, "sector_size");
    fl_json_u64(out, d->dev->sector_size);
    fl_json_key(out, "size");
    fl_json_u64(out, fl_bdev_size_bytes(d->dev));
    fl_json_key(out, "table");
    fl_json_cstr(out, table_name(t->kind));
    fl_json_key(out, "parts");
    fl_json_arr_begin(out);
    for (uint32_t i = 0; i < t->count && i < sizeof t->parts / sizeof t->parts[0]; i++)
      scan_part(out, d->dev, &t->parts[i]);
    fl_json_arr_end(out);
    fl_json_obj_end(out);
    rc = out->err;
  }
  free(t);
  tab_release(disk, T_DISK);
  return rc;
}

/* ---- volumes ---- */

static int vol_open(fl_bdev *dev, uint32_t part_index, const void *pass, size_t passlen, bool want_rw, jvol *v) {
  fl_part_table *t = calloc(1, sizeof *t);
  if (!t) return FL_ENOMEM;
  int rc = fl_part_scan(dev, t);
  const fl_partition *p = NULL;
  for (uint32_t i = 0; rc == FL_OK && i < t->count && i < sizeof t->parts / sizeof t->parts[0]; i++)
    if (t->parts[i].index == part_index) {
      p = &t->parts[i];
      break;
    }
  if (rc == FL_OK && !p) rc = FL_EINVAL;
  if (rc == FL_OK) rc = fl_slice_bdev_new(dev, p->first_lba, p->sector_count, &v->slice);
  free(t);
  if (rc != FL_OK) return rc;

  fl_probe_result pr;
  if ((rc = fl_probe(v->slice, &pr)) != FL_OK) return rc;
  bool luks = false;
  if (is_luks(pr.content)) {
    if ((rc = fl_luks_open(v->slice, &v->luks)) != FL_OK) return rc;
    luks = true;
  } else if (pr.content == FL_CONTENT_UNKNOWN && fl_luks_open(v->slice, &v->luks) == FL_OK) {
    luks = true; /* en-tête principal abîmé, secondaire valide */
  }
  fl_bdev *target = v->slice;
  if (luks) {
    if (!pass) return FL_EINVAL;
    if ((rc = fl_luks_unlock(v->luks, pass, passlen, &v->plain)) != FL_OK) return rc;
    if ((rc = fl_probe(v->plain, &pr)) != FL_OK) return rc;
    target = v->plain;
  }
  if (!fl_content_supported(pr.content) || is_luks(pr.content)) return FL_ENOTSUP; /* ext4, FAT, exFAT */
  v->kind = pr.content;
  return fl_fs_mount(target, pr.content, want_rw, &v->fs);
}

static void (*g_mount_hook)(void *);
static void *g_mount_hook_arg;
void fj_debug_set_mount_hook(void (*fn)(void *), void *arg) {
  g_mount_hook = fn;
  g_mount_hook_arg = arg;
}

int64_t fj_mount(int64_t disk, uint32_t part_index, const void *pass, size_t passlen, bool want_rw) {
  jdisk *d = tab_acquire(disk, T_DISK);
  if (!d) return FL_EINVAL;
  pthread_mutex_lock(&g_mu);
  bool formatting = d->formatting;
  if (!formatting) d->mounting++; /* jusqu'à l'enregistrement du volume, fj_format le voit */
  pthread_mutex_unlock(&g_mu);
  int64_t h;
  jvol *v = formatting ? NULL : calloc(1, sizeof *v);
  if (formatting) {
    h = FL_EBUSY;
  } else if (!v) {
    h = FL_ENOMEM;
  } else {
    int rc = vol_open(d->dev, part_index, pass, passlen, want_rw, v);
    if (rc != FL_OK) {
      vol_free(v);
      h = rc;
    } else {
      if (g_mount_hook) g_mount_hook(g_mount_hook_arg); /* tests : pause avant l'enregistrement */
      /* enregistré tant que l'on tient la référence du disque : fj_close_disk le verra */
      h = tab_put(T_VOL, disk, v);
      if (h < 0) vol_free(v);
    }
  }
  if (!formatting) {
    pthread_mutex_lock(&g_mu);
    d->mounting--;
    pthread_mutex_unlock(&g_mu);
  }
  tab_release(disk, T_DISK);
  return h;
}

int fj_unmount(int64_t vol, bool force) {
  pthread_mutex_lock(&g_mu);
  slot *s = lookup(vol, T_VOL);
  if (!s) {
    pthread_mutex_unlock(&g_mu);
    return FL_EINVAL;
  }
  if (!force) {
    /* le disque parent ferme ses volumes : ce démontage-ci n'a plus rien à faire, et n'est pas une erreur de poignée */
    slot *ps = lookup(s->parent, T_DISK);
    if (ps && ps->closing) {
      pthread_mutex_unlock(&g_mu);
      return FL_EBUSY;
    }
  }
  if (s->closing) {
    pthread_mutex_unlock(&g_mu);
    return FL_EINVAL;
  }
  if (!force && live_children(vol, T_FILE)) {
    pthread_mutex_unlock(&g_mu);
    return FL_EBUSY;
  }
  s->closing = true;
  if (!force) {
    /* un fj_open_file en cours a pu ouvrir un fichier entre-temps */
    while (s->refs) pthread_cond_wait(&g_cv, &g_mu);
    if (live_children(vol, T_FILE)) {
      s->closing = false;
      pthread_cond_broadcast(&g_cv); /* un fj_close_disk peut attendre ce volume : il le fermera lui-même */
      pthread_mutex_unlock(&g_mu);
      return FL_EBUSY;
    }
  }
  int rc = finish_close_locked(s, vol, vol_destroy);
  pthread_mutex_unlock(&g_mu);
  return rc;
}

static const char *type_code(fl_ftype t) {
  switch (t) {
    case FL_FT_FILE: return "f";
    case FL_FT_DIR: return "d";
    case FL_FT_SYMLINK: return "l";
    default: return "o";
  }
}

static void entry_json(fl_json *j, const char *name, size_t namelen, const fl_stat *st) {
  fl_json_obj_begin(j);
  fl_json_key(j, "name");
  fl_json_str(j, name, namelen);
  fl_json_key(j, "type");
  fl_json_cstr(j, type_code(st->type));
  fl_json_key(j, "size");
  fl_json_u64(j, st->size);
  fl_json_key(j, "mtime");
  fl_json_i64(j, st->mtime);
  fl_json_key(j, "mode"); /* type + droits ; propriétaire : sens réel sur ext4 seulement */
  fl_json_u64(j, st->mode);
  fl_json_key(j, "uid");
  fl_json_u64(j, st->uid);
  fl_json_key(j, "gid");
  fl_json_u64(j, st->gid);
  fl_json_obj_end(j);
}

int fj_statfs(int64_t vol, fl_json *out) {
  jvol *v = tab_acquire(vol, T_VOL);
  if (!v) return FL_EINVAL;
  fl_statfs sf;
  memset(&sf, 0, sizeof sf);
  int rc = fl_fs_statfs(v->fs, &sf);
  fl_content kind = v->kind;
  tab_release(vol, T_VOL);
  if (rc != FL_OK) return rc;
  sf.label[sizeof sf.label - 1] = 0;
  sf.ro_reason[sizeof sf.ro_reason - 1] = 0;
  fl_json_reset(out);
  fl_json_obj_begin(out);
  fl_json_key(out, "total");
  fl_json_u64(out, sf.total_bytes);
  fl_json_key(out, "free");
  fl_json_u64(out, sf.free_bytes);
  fl_json_key(out, "label");
  fl_json_cstr(out, sf.label);
  fl_json_key(out, "read_only");
  fl_json_bool(out, sf.read_only);
  fl_json_key(out, "ro_reason");
  fl_json_cstr(out, sf.ro_reason);
  fl_json_key(out, "needs_recovery");
  fl_json_bool(out, sf.needs_recovery);
  fl_json_key(out, "fs");
  fl_json_cstr(out, fl_content_name(kind));
  fl_json_obj_end(out);
  return out->err;
}

int fj_stat(int64_t vol, const char *path, fl_json *out) {
  if (!path) return FL_EINVAL;
  jvol *v = tab_acquire(vol, T_VOL);
  if (!v) return FL_EINVAL;
  fl_stat st;
  int rc = fl_fs_stat(v->fs, path, &st);
  tab_release(vol, T_VOL);
  if (rc != FL_OK) return rc;
  const char *slash = strrchr(path, '/');
  const char *name = slash ? slash + 1 : path; /* "" pour "/" */
  fl_json_reset(out);
  entry_json(out, name, strlen(name), &st);
  return out->err;
}

typedef struct {
  char *name;
  fl_stat st;
} lent;

typedef struct {
  lent *v;
  size_t n, cap;
} lents;

static int list_cb(void *ctx, const fl_dirent *e) {
  lents *l = ctx;
  if (l->n >= FJ_LIST_MAX) return FL_ENOMEM;
  if (l->n == l->cap) {
    size_t cap = l->cap ? l->cap * 2 : 64;
    lent *nv = realloc(l->v, cap * sizeof *nv);
    if (!nv) return FL_ENOMEM;
    l->v = nv;
    l->cap = cap;
  }
  size_t len = strnlen(e->name, sizeof e->name);
  char *name = malloc(len + 1);
  if (!name) return FL_ENOMEM;
  memcpy(name, e->name, len);
  name[len] = 0;
  l->v[l->n].name = name;
  l->v[l->n].st = e->st;
  l->n++;
  return 0;
}

static int lent_cmp(const void *a, const void *b) {
  return strcmp(((const lent *)a)->name, ((const lent *)b)->name); /* octets non signés */
}

int fj_list(int64_t vol, const char *path, fl_json *out) {
  if (!path) return FL_EINVAL;
  jvol *v = tab_acquire(vol, T_VOL);
  if (!v) return FL_EINVAL;
  lents l = {NULL, 0, 0};
  int rc = fl_fs_readdir(v->fs, path, list_cb, &l);
  tab_release(vol, T_VOL);
  if (rc == FL_OK) {
    if (l.n > 1) qsort(l.v, l.n, sizeof *l.v, lent_cmp);
    fl_json_reset(out);
    fl_json_set_max(out, FL_JSON_LIST_MAX); /* au-delà : FL_ENOMEM, message habituel */
    fl_json_arr_begin(out);
    for (size_t i = 0; i < l.n && out->err == FL_OK; i++) entry_json(out, l.v[i].name, strlen(l.v[i].name), &l.v[i].st);
    fl_json_arr_end(out);
    rc = out->err;
  }
  for (size_t i = 0; i < l.n; i++) free(l.v[i].name);
  free(l.v);
  return rc;
}

/* ---- fichiers ---- */

int64_t fj_open_file(int64_t vol, const char *path, int flags) {
  if (!path) return FL_EINVAL;
  jvol *v = tab_acquire(vol, T_VOL);
  if (!v) return FL_EINVAL;
  fl_file *f = NULL;
  int64_t h = fl_fs_open(v->fs, path, flags, &f);
  if (h == FL_OK) {
    /* enregistré sous la référence du volume : fj_unmount le verra */
    h = tab_put(T_FILE, vol, f);
    if (h < 0) fl_file_close(f);
  }
  tab_release(vol, T_VOL);
  return h;
}

int64_t fj_pread(int64_t file, void *buf, size_t len, uint64_t off) {
  if (!buf && len) return FL_EINVAL;
  fl_file *f = tab_acquire(file, T_FILE);
  if (!f) return FL_EINVAL;
  if (len > FJ_PREAD_MAX) len = FJ_PREAD_MAX;
  int64_t r = fl_file_pread(f, buf, len, off);
  tab_release(file, T_FILE);
  return r;
}

int64_t fj_pwrite(int64_t file, const void *buf, size_t len, uint64_t off) {
  if ((!buf && len) || len > FJ_PREAD_MAX) return FL_EINVAL;
  fl_file *f = tab_acquire(file, T_FILE);
  if (!f) return FL_EINVAL;
  int64_t r = fl_file_pwrite(f, buf, len, off);
  tab_release(file, T_FILE);
  return r;
}

int fj_truncate(int64_t file, uint64_t size) {
  fl_file *f = tab_acquire(file, T_FILE);
  if (!f) return FL_EINVAL;
  int rc = fl_file_truncate(f, size);
  tab_release(file, T_FILE);
  return rc;
}

int fj_close_file(int64_t file) { return tab_close(file, T_FILE, file_destroy); }

/* Opération sur un chemin d'un volume, référence tenue. */
static int vol_path_op(int64_t vol, const char *path, int (*op)(fl_fs *, const char *)) {
  if (!path) return FL_EINVAL;
  jvol *v = tab_acquire(vol, T_VOL);
  if (!v) return FL_EINVAL;
  int rc = op(v->fs, path);
  tab_release(vol, T_VOL);
  return rc;
}

int fj_mkdir(int64_t vol, const char *path) { return vol_path_op(vol, path, fl_fs_mkdir); }
int fj_unlink(int64_t vol, const char *path) { return vol_path_op(vol, path, fl_fs_unlink); }
int fj_rmdir(int64_t vol, const char *path) { return vol_path_op(vol, path, fl_fs_rmdir); }

int fj_rename(int64_t vol, const char *from, const char *to) {
  if (!from || !to) return FL_EINVAL;
  jvol *v = tab_acquire(vol, T_VOL);
  if (!v) return FL_EINVAL;
  int rc = fl_fs_rename(v->fs, from, to);
  tab_release(vol, T_VOL);
  return rc;
}

int fj_sync(int64_t vol) {
  jvol *v = tab_acquire(vol, T_VOL);
  if (!v) return FL_EINVAL;
  int rc = fl_fs_sync(v->fs);
  tab_release(vol, T_VOL);
  return rc;
}

int64_t fj_debug_flushes(int64_t disk) {
  jdisk *d = tab_acquire(disk, T_DISK);
  if (!d) return FL_EINVAL;
  int64_t n = (int64_t)atomic_load(&d->flushes);
  tab_release(disk, T_DISK);
  return n;
}

/* ---- formatage (phase 8) ---- */

static int parse_format_opts(const char *json, fl_fmt_opts *o, char *label, size_t cap) {
  cJSON *r = json ? cJSON_Parse(json) : NULL;
  if (!cJSON_IsObject(r)) {
    cJSON_Delete(r);
    return FL_EINVAL;
  }
  static const char *noms[] = {"fat32", "exfat", "ext4", "ntfs"};
  static const fl_fmt_fs fss[] = {FL_FMT_FAT32, FL_FMT_EXFAT, FL_FMT_EXT4, FL_FMT_NTFS};
  const char *fs = cJSON_GetStringValue(cJSON_GetObjectItem(r, "fs"));
  const cJSON *enc = cJSON_GetObjectItem(r, "encrypted"), *lab = cJSON_GetObjectItem(r, "label");
  const cJSON *mem = cJSON_GetObjectItem(r, "kdf_mem_kib"), *uid = cJSON_GetObjectItem(r, "uid"),
              *gid = cJSON_GetObjectItem(r, "gid");
  int rc = FL_EINVAL;
  memset(o, 0, sizeof *o);
  for (size_t i = 0; fs && i < sizeof noms / sizeof noms[0]; i++)
    if (!strcmp(fs, noms[i])) {
      o->fs = fss[i];
      rc = FL_OK;
    }
  if ((enc && !cJSON_IsBool(enc)) || (lab && !cJSON_IsString(lab)) || (mem && !cJSON_IsNumber(mem)) ||
      (uid && !cJSON_IsNumber(uid)) || (gid && !cJSON_IsNumber(gid)))
    rc = FL_EINVAL;
  if (rc == FL_OK) {
    o->encrypted = cJSON_IsTrue(enc);
    const char *l = lab ? lab->valuestring : "";
    if (strlen(l) >= cap) rc = FL_EINVAL;
    else snprintf(label, cap, "%s", l);
    o->label = label;
    double m = mem ? mem->valuedouble : 1048576, u = uid ? uid->valuedouble : 1000, g = gid ? gid->valuedouble : 1000;
    if (m < 0 || m > UINT32_MAX || u < 0 || u > UINT32_MAX || g < 0 || g > UINT32_MAX) rc = FL_EINVAL;
    o->kdf_mem_kib = (uint32_t)m;
    o->root_uid = (uint32_t)u;
    o->root_gid = (uint32_t)g;
  }
  cJSON_Delete(r);
  return rc;
}

int fj_format(int64_t disk, const char *json, const void *pass, size_t passlen, void (*progress)(int pct, void *arg),
              void *arg) {
  fl_fmt_opts o;
  char label[256];
  int rc = parse_format_opts(json, &o, label, sizeof label);
  jdisk *d = tab_acquire(disk, T_DISK);
  if (!d) return FL_EINVAL;
  if (rc == FL_OK) {
    pthread_mutex_lock(&g_mu);
    if (d->formatting || d->mounting > 0 || live_children(disk, T_VOL) > 0) rc = FL_EBUSY;
    else d->formatting = true; /* plus aucun fj_mount sur ce disque jusqu'à la fin */
    pthread_mutex_unlock(&g_mu);
  }
  if (rc == FL_OK) {
    rc = d->dev->read_only ? FL_EROFS : fl_format(d->dev, &o, pass, passlen, progress, arg);
    pthread_mutex_lock(&g_mu);
    d->formatting = false;
    pthread_mutex_unlock(&g_mu);
  }
  tab_release(disk, T_DISK);
  return rc;
}

/* ---- archives (phase 9) ---- */

/* Une archive sur un fichier du moteur est l'enfant de sa poignée de fichier : elle lit et écrit le fl_file
 * directement (le fichier ne peut être détruit qu'après elle, file_destroy fermant d'abord ses archives),
 * et ferme la poignée de fichier avec elle. Sur un descripteur : pas de parent, le descripteur est fermé. */
typedef struct {
  int fd;          /* -1 pour un fichier du moteur */
  fl_file *file;
  uint64_t pos;    /* écriture : position suivante */
  fl_arc_in in;
  fl_arc_out out;
  fl_arc_reader *r;
  fl_arc_writer *w;
  bool commit;     /* fermeture explicite d'un ZIP écrit : terminer plutôt qu'abandonner */
  pthread_mutex_t mu; /* libarchive : une opération à la fois par archive */
} jarc;

static int64_t arc_pread(void *ctx, void *buf, size_t len, uint64_t off) {
  jarc *a = ctx;
  if (a->file) return fl_file_pread(a->file, buf, len, off);
  ssize_t n;
  do n = pread(a->fd, buf, len, (off_t)off);
  while (n < 0 && errno == EINTR);
  return n < 0 ? FL_EIO : n;
}

static int arc_write(void *ctx, const void *buf, size_t len) {
  jarc *a = ctx;
  const uint8_t *p = buf;
  while (len) {
    int64_t n;
    if (a->file) {
      n = fl_file_pwrite(a->file, p, len, a->pos);
    } else {
      do n = write(a->fd, p, len);
      while (n < 0 && errno == EINTR);
      if (n < 0) n = errno == ENOSPC ? FL_ENOSPC : FL_EIO;
    }
    if (n < 0) return (int)n;
    if (n == 0) return FL_EIO;
    a->pos += (uint64_t)n;
    p += n;
    len -= (size_t)n;
  }
  return FL_OK;
}

/* Libère l'archive ; ferme le descripteur, jamais le fichier (fermé par l'appelant). */
static int arc_free(jarc *a, bool commit) {
  int rc = FL_OK;
  if (a->r) fl_arc_close(a->r);
  if (a->w) rc = fl_arcw_close(a->w, commit);
  if (a->fd >= 0) close(a->fd);
  pthread_mutex_destroy(&a->mu);
  free(a);
  return rc;
}

static int arc_destroy(void *obj, int64_t h) {
  (void)h;
  return arc_free(obj, ((jarc *)obj)->commit);
}

/* Fichier fermé (ou volume, disque) sous l'archive : abandon, sans écrire. */
static int arc_abandon(void *obj, int64_t h) {
  (void)h;
  return arc_free(obj, false);
}

static int file_destroy(void *obj, int64_t h) {
  close_children(h, T_ARC, arc_abandon);
  return fl_file_close(obj); /* fichier écrit : synchronisation */
}

static jarc *arc_new(int fd) {
  jarc *a = calloc(1, sizeof *a);
  if (!a) return NULL;
  a->fd = fd;
  a->in = (fl_arc_in){a, 0, arc_pread};
  a->out = (fl_arc_out){a, arc_write};
  pthread_mutex_init(&a->mu, NULL);
  return a;
}

/* Ouvre le lecteur ou l'écrivain sur `a`, puis l'enregistre (sous `parent` : fichier tenu par l'appelant). */
static int64_t arc_start(jarc *a, int64_t parent, bool writer) {
  int rc = writer ? fl_arcw_open(&a->out, &a->w) : fl_arc_open(&a->in, &a->r);
  if (rc != FL_OK) {
    arc_free(a, false);
    return rc;
  }
  int64_t h = tab_put(T_ARC, parent, a);
  if (h < 0) arc_free(a, false);
  return h;
}

static int64_t arc_open_fd(int fd, bool writer) {
  if (fd < 0) return FL_EINVAL;
  jarc *a = arc_new(fd);
  if (!a) {
    close(fd);
    return FL_ENOMEM;
  }
  if (!writer) {
    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 0) {
      arc_free(a, false);
      return FL_EINVAL;
    }
    a->in.size = size;
  }
  return arc_start(a, 0, writer);
}

static int64_t arc_open_file(int64_t file, int64_t size, bool writer) {
  if (size < 0) {
    fj_close_file(file);
    return FL_EINVAL;
  }
  fl_file *f = tab_acquire(file, T_FILE);
  if (!f) return FL_EINVAL;
  jarc *a = arc_new(-1);
  int64_t h = FL_ENOMEM;
  if (a) {
    a->file = f;
    a->in.size = size;
    /* enregistrée sous la référence du fichier : sa fermeture la verra */
    h = arc_start(a, file, writer);
  }
  tab_release(file, T_FILE);
  if (h < 0) fj_close_file(file);
  return h;
}

int64_t fj_arc_open_fd(int fd) { return arc_open_fd(fd, false); }
int64_t fj_arc_open_file(int64_t file, int64_t size) { return arc_open_file(file, size, false); }
int64_t fj_arcw_open_fd(int fd) { return arc_open_fd(fd, true); }
int64_t fj_arcw_open_file(int64_t file) { return arc_open_file(file, 0, true); }

static const char *arc_type_code(fl_arc_type t) {
  switch (t) {
    case FL_ARC_FILE: return "f";
    case FL_ARC_DIR: return "d";
    case FL_ARC_SYMLINK: return "l";
    case FL_ARC_HARDLINK: return "h";
    default: return "o";
  }
}

/* Base64 (RFC 4648, avec bourrage) de n octets dans out (au moins 4 * ((n + 2) / 3) + 1 octets). */
static void base64(const uint8_t *in, size_t n, char *out) {
  static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = tab[v >> 18 & 63];
    out[o++] = tab[v >> 12 & 63];
    out[o++] = i + 1 < n ? tab[v >> 6 & 63] : '=';
    out[o++] = i + 2 < n ? tab[v & 63] : '=';
  }
  out[o] = 0;
}

int fj_arc_next(int64_t arc, fl_json *out) {
  jarc *a = tab_acquire(arc, T_ARC);
  if (!a) return FL_EINVAL;
  fl_arc_entry *e = a->r ? malloc(sizeof *e) : NULL;
  int rc = !a->r ? FL_EINVAL : !e ? FL_ENOMEM : FL_OK;
  bool raw = false;
  if (rc == FL_OK) {
    pthread_mutex_lock(&a->mu);
    rc = fl_arc_next(a->r, e);
    raw = rc == 1 && strcmp(fl_arc_format(a->r), "raw") == 0; /* .gz ou .xz seul : nom « data » de libarchive */
    pthread_mutex_unlock(&a->mu);
  }
  tab_release(arc, T_ARC);
  if (rc == 1) {
    char name[4 * ((sizeof e->name + 2) / 3) + 1];
    base64((const uint8_t *)e->name, e->name_len, name);
    fl_json_reset(out);
    fl_json_obj_begin(out);
    fl_json_key(out, "name");
    fl_json_cstr(out, name);
    fl_json_key(out, "type");
    fl_json_cstr(out, arc_type_code(e->type));
    fl_json_key(out, "size");
    fl_json_i64(out, e->size);
    fl_json_key(out, "mtime");
    fl_json_i64(out, e->mtime);
    fl_json_key(out, "encrypted");
    fl_json_bool(out, e->encrypted);
    fl_json_key(out, "raw");
    fl_json_bool(out, raw);
    fl_json_obj_end(out);
    if (out->err != FL_OK) rc = out->err;
  }
  free(e);
  return rc;
}

int64_t fj_arc_read(int64_t arc, void *buf, size_t len) {
  if (!buf && len) return FL_EINVAL;
  jarc *a = tab_acquire(arc, T_ARC);
  if (!a) return FL_EINVAL;
  if (len > FJ_PREAD_MAX) len = FJ_PREAD_MAX;
  int64_t r = FL_EINVAL;
  if (a->r) {
    pthread_mutex_lock(&a->mu);
    r = fl_arc_read(a->r, buf, len);
    pthread_mutex_unlock(&a->mu);
  }
  tab_release(arc, T_ARC);
  return r;
}

/* Fermeture explicite : l'archive (commit pour un ZIP écrit), puis la poignée de fichier dont elle est l'enfant. */
static int arc_close(int64_t arc, bool commit) {
  pthread_mutex_lock(&g_mu);
  slot *s = lookup(arc, T_ARC);
  if (!s || s->closing) {
    pthread_mutex_unlock(&g_mu);
    return FL_EINVAL;
  }
  ((jarc *)s->obj)->commit = commit;
  int64_t parent = s->parent;
  s->closing = true;
  int rc = finish_close_locked(s, arc, arc_destroy);
  pthread_mutex_unlock(&g_mu);
  if (parent) {
    int rf = fj_close_file(parent); /* FL_EINVAL : déjà en fermeture par ailleurs, qui le synchronise */
    if (rc == FL_OK && rf != FL_EINVAL) rc = rf;
  }
  return rc;
}

int fj_arc_close(int64_t arc) { return arc_close(arc, false); }
int fj_arcw_close(int64_t arc, bool commit) { return arc_close(arc, commit); }

int fj_arcw_add(int64_t arc, const char *json) {
  cJSON *o = json ? cJSON_Parse(json) : NULL;
  const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(o, "name"));
  const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(o, "type"));
  const cJSON *size = cJSON_GetObjectItem(o, "size"), *mtime = cJSON_GetObjectItem(o, "mtime"),
              *store = cJSON_GetObjectItem(o, "store");
  int rc = FL_EINVAL;
  if (name && type && (!strcmp(type, "f") || !strcmp(type, "d")) && cJSON_IsNumber(size) && size->valuedouble >= 0 &&
      size->valuedouble < 9.2e18 && (!mtime || cJSON_IsNumber(mtime)) && (!store || cJSON_IsBool(store))) {
    jarc *a = tab_acquire(arc, T_ARC);
    if (a) {
      if (a->w) {
        pthread_mutex_lock(&a->mu);
        rc = fl_arcw_add(a->w, name, *type == 'd' ? FL_ARC_DIR : FL_ARC_FILE, (int64_t)size->valuedouble,
                         mtime ? (int64_t)mtime->valuedouble : 0, cJSON_IsTrue(store));
        pthread_mutex_unlock(&a->mu);
      }
      tab_release(arc, T_ARC);
    }
  }
  cJSON_Delete(o);
  return rc;
}

int fj_arcw_write(int64_t arc, const void *buf, size_t len) {
  if (!buf && len) return FL_EINVAL;
  jarc *a = tab_acquire(arc, T_ARC);
  if (!a) return FL_EINVAL;
  int rc = FL_EINVAL;
  if (a->w) {
    pthread_mutex_lock(&a->mu);
    rc = fl_arcw_write(a->w, buf, len);
    pthread_mutex_unlock(&a->mu);
  }
  tab_release(arc, T_ARC);
  return rc;
}
