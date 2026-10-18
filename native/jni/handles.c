/* Moteur à poignées du pont JNI (voir handles.h). Sans jni.h : compilé aussi dans les tests PC.
 *
 * Protocole de la table (un seul mutex g_mu, une seule condition g_cv) :
 *   - acquire : entrée valide, du bon type et pas « en fermeture » → refs++ ; l'opération longue
 *     se fait ensuite HORS du mutex ; release : refs-- (et réveil si une fermeture attend).
 *   - fermeture : closing = vrai (plus aucun acquire ne réussit), attente de refs == 0, destruction
 *     de l'objet HORS du mutex, puis libération de l'entrée (génération + 1) et réveil.
 *   - une entrée en fermeture garde son type et son parent jusqu'à la fin de la destruction : un
 *     parent qui ferme ses enfants attend aussi ceux qu'un autre fil est en train de fermer.
 * Les enfants (volumes d'un disque, fichiers d'un volume) sont créés pendant que l'opération tient
 * une référence sur le parent : une fois les références du parent retombées à 0, plus aucun enfant
 * ne peut apparaître. */
#define _POSIX_C_SOURCE 200809L
#include "handles.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "fluluks/blockdev.h"
#include "fluluks/cache.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fluluks/luks.h"
#include "fluluks/partition.h"
#include "fluluks/probe.h"
#include "fluluks/usb.h"

#define CACHE_BLOCK 65536u
#define CACHE_BLOCKS 256u

enum { T_FREE = 0, T_DISK = 1, T_VOL = 2, T_FILE = 3 };

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

typedef void (*destroy_fn)(void *obj, int64_t h);

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
    slot *ps = lookup(parent, type - 1); /* T_VOL -> T_DISK, T_FILE -> T_VOL */
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
 * libère l'entrée. Rend la main avec g_mu tenu. */
static void finish_close_locked(slot *s, int64_t h, destroy_fn fn) {
  while (s->refs) pthread_cond_wait(&g_cv, &g_mu);
  void *obj = s->obj;
  pthread_mutex_unlock(&g_mu);
  fn(obj, h);
  pthread_mutex_lock(&g_mu);
  s->type = T_FREE;
  s->closing = false;
  s->parent = 0;
  s->obj = NULL;
  s->gen = s->gen == UINT32_MAX ? 1 : s->gen + 1;
  g_used--;
  pthread_cond_broadcast(&g_cv);
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
  finish_close_locked(s, h, fn);
  pthread_mutex_unlock(&g_mu);
  return FL_OK;
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
  for (int t = T_DISK; t <= T_FILE && rc != FL_OK; t++) {
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
  fl_bdev *dev;          /* cache au-dessus de base : seul utilisé */
} jdisk;

typedef struct {
  fl_bdev *slice, *plain; /* plain : déchiffré (NULL si en clair) */
  fl_luks *luks;
  fl_fs *fs;
} jvol;

static void file_destroy(void *obj, int64_t h) {
  (void)h;
  fl_file_close(obj);
}

/* Ordre de la phase 1 : fs, déchiffré, luks, tranche. */
static void vol_free(jvol *v) {
  if (!v) return;
  if (v->fs) fl_fs_unmount(v->fs); /* aucun fichier ouvert : close_children est passé avant */
  fl_bdev_close(v->plain);
  fl_luks_close(v->luks);
  fl_bdev_close(v->slice);
  free(v);
}

static void vol_destroy(void *obj, int64_t h) {
  close_children(h, T_FILE, file_destroy);
  vol_free(obj);
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

static void disk_destroy(void *obj, int64_t h) {
  close_children(h, T_VOL, vol_destroy);
  disk_free(obj);
}

/* d->base rempli : ajoute le cache, enregistre le disque. Libère tout en cas d'échec. */
static int64_t disk_register(jdisk *d) {
  int rc = fl_cache_bdev_new(d->base, CACHE_BLOCK, CACHE_BLOCKS, &d->dev);
  if (rc != FL_OK) {
    d->dev = NULL;
    disk_free(d);
    return rc;
  }
  int64_t h = tab_put(T_DISK, 0, d);
  if (h < 0) disk_free(d);
  return h;
}

int64_t fj_open_image(const char *path) {
  if (!path) return FL_EINVAL;
  jdisk *d = calloc(1, sizeof *d);
  if (!d) return FL_ENOMEM;
  int rc = fl_file_bdev_open(path, 512, true, &d->base);
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
  if (rc == FL_OK) rc = fl_usb_bdev_open(d->usb, 0, true /* lecture seule jusqu'au SCSI */, &d->base);
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

static int vol_open(fl_bdev *dev, uint32_t part_index, const void *pass, size_t passlen, jvol *v) {
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
  if (pr.content != FL_CONTENT_EXT4) return FL_ENOTSUP;
  return fl_fs_mount(target, FL_CONTENT_EXT4, false, &v->fs);
}

int64_t fj_mount(int64_t disk, uint32_t part_index, const void *pass, size_t passlen) {
  jdisk *d = tab_acquire(disk, T_DISK);
  if (!d) return FL_EINVAL;
  int64_t h;
  jvol *v = calloc(1, sizeof *v);
  if (!v) {
    h = FL_ENOMEM;
  } else {
    int rc = vol_open(d->dev, part_index, pass, passlen, v);
    if (rc != FL_OK) {
      vol_free(v);
      h = rc;
    } else {
      /* enregistré tant que l'on tient la référence du disque : fj_close_disk le verra */
      h = tab_put(T_VOL, disk, v);
      if (h < 0) vol_free(v);
    }
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
  finish_close_locked(s, vol, vol_destroy);
  pthread_mutex_unlock(&g_mu);
  return FL_OK;
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
  fl_json_obj_end(j);
}

int fj_statfs(int64_t vol, fl_json *out) {
  jvol *v = tab_acquire(vol, T_VOL);
  if (!v) return FL_EINVAL;
  fl_statfs sf;
  memset(&sf, 0, sizeof sf);
  int rc = fl_fs_statfs(v->fs, &sf);
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

int64_t fj_open_file(int64_t vol, const char *path) {
  if (!path) return FL_EINVAL;
  jvol *v = tab_acquire(vol, T_VOL);
  if (!v) return FL_EINVAL;
  fl_file *f = NULL;
  int64_t h = fl_fs_open(v->fs, path, FL_O_RDONLY, &f);
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

int fj_close_file(int64_t file) { return tab_close(file, T_FILE, file_destroy); }
