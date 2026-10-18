/* ext4 par lwext4 (corrigé), branché sur fl_bdev, en lecture seule ou en écriture.
 *
 * Fils d'exécution : lwext4 garde des registres globaux (périphériques, points de
 * montage, règles de création) et des caches par montage, sans aucun verrou. Tout appel
 * à lwext4 passe donc sous le mutex global g_lock (initialisé statiquement), qui protège
 * aussi le compteur de noms et les fichiers ouverts de chaque volume. Aucun rappel
 * utilisateur n'est appelé sous ce verrou. Une phase ultérieure ajoutera un verrou par
 * volume au-dessus.
 *
 * Chaque montage enregistre son propre périphérique "fl<n>" et son point de montage
 * "/fl<n>/" (n unique, tiré du compteur) ; les deux sont libérés au démontage et sur tout
 * échec de montage, pour ne jamais épuiser les 8 emplacements de lwext4.
 *
 * Le disque est hostile : le superbloc est validé ici avant que lwext4 ne s'en serve,
 * les numéros d'inode des entrées de dossier sont vérifiés, et le journal n'est jamais
 * rejoué (needs_recovery le signale).
 *
 * Écriture (§ 4.2 de la spec) : seulement si ext4_policy.c le permet (sinon lecture seule, avec
 * la raison), et lecture seule collante en cours de session sur une erreur d'écriture ou une somme
 * fausse. Le journal est démarré au montage et arrêté au
 * démontage ; écriture immédiate, sans cache différé. Chaque opération est vérifiée ici
 * avant d'appeler lwext4, dont les fonctions de haut niveau ont des effets que l'API
 * n'admet pas : ext4_fopen2 avec O_CREAT crée les dossiers intermédiaires, ext4_dir_rm
 * supprime récursivement, ext4_fremove sur un fichier ouvert libère son inode sous le
 * descripteur. Propriétaire, droits et horodatages : règles posées par ext4_set_policy
 * (correctif 0003), appliquées par lwext4 dans la transaction de chaque opération. */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ext4.h"
#include "ext4_blockdev.h"
#include "ext4_config.h"
#include "ext4_errno.h"
#include "ext4_misc.h"
#include "ext4_types.h"
#include "fluluks/error.h"
#include "fs/ext4_policy.h"
#include "fs/fs_internal.h"
#include "fs/ro_reasons.h"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_counter; /* protégé par g_lock */

#define MP_CAP 24                 /* "/fl4294967295/" + NUL tient largement */
#define FULL_CAP (MP_CAP + 4096)  /* point de montage + chemin validé (≤ 4095 octets) */
#define MAX_DIR_STEPS (1u << 24)  /* entrées ignorées consécutives au plus avant FL_ECORRUPT */

typedef struct ext4_fh ext4_fh;

typedef struct {
  struct ext4_blockdev bd;
  struct ext4_blockdev_iface bdif;
  uint8_t *ph_buf;
  fl_bdev *dev;
  char dev_name[MP_CAP];
  char mp[MP_CAP];
  uint32_t open_files; /* fichiers ouverts + parcours de dossier en cours (g_lock) */
  ext4_fh *files;      /* fichiers ouverts (g_lock) */
  uint32_t block_size;
  uint32_t inodes_count;
  uint64_t max_bytes; /* au-delà, les numéros de bloc logique (32 bits) débordent */
  bool needs_recovery;
  bool rw;            /* monté en écriture, journal démarré */
  bool io_err;        /* une écriture ou une barrière a échoué : plus rien n'est écrit (g_lock) */
  fl_fs *fs;          /* pour la lecture seule collante */
  uint8_t sb[1024];   /* superbloc validé, octets [1024, 2048) du disque (tenu à jour) */
} ext4_vol;

struct ext4_fh {
  ext4_file f;
  ext4_fh *next;
  bool written; /* écrit ou tronqué : sync à la fermeture */
};

/* Règles de création du § 4.2 : propriétaire du parent, 0644 / 0755 (setgid hérité). */
static const struct ext4_policy k_policy = {fl_fs_now, 0644, 0755};

static int map_err(int e) {
  switch (e) {
    case EOK: return FL_OK;
    case ENOENT: return FL_ENOENT;
    case ENOTDIR: return FL_ENOTDIR;
    case ENOMEM: return FL_ENOMEM;
    case EEXIST: return FL_EEXIST;
    case EISDIR: return FL_EISDIR;
    case ENOTEMPTY: return FL_ENOTEMPTY;
    case ENOSPC: return FL_ENOSPC;
    case EROFS: return FL_EROFS;
    case ENOTSUP: return FL_ENOTSUP;
    case EINVAL:
    case EFBIG: return FL_EINVAL;
    /* structure corrompue (trou de dossier, entrée mal formée, inode hors bornes) : correctif
     * lwext4 0002 ; EIO reste réservé aux erreurs du périphérique */
    case EFSCORRUPTED: return FL_ECORRUPT;
    case EBADMSG: return FL_ECORRUPT; /* somme de contrôle fausse (correctif 0005) */
    default: return FL_EIO;
  }
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static bool pow2(uint64_t x) { return x && !(x & (x - 1)); }

/* ---------- périphérique bloc lwext4 → fl_bdev ---------- */

static int bd_open(struct ext4_blockdev *bd) {
  (void)bd;
  return EOK;
}

static int bd_close(struct ext4_blockdev *bd) {
  (void)bd;
  return EOK;
}

static int bd_bread(struct ext4_blockdev *bd, void *buf, uint64_t blk_id, uint32_t blk_cnt) {
  ext4_vol *v = bd->bdif->p_user;
  if (blk_cnt == 0) return EOK;
  /* taille du tampon en octets représentable (fl_bdev vérifie les bornes du disque) */
  if ((uint64_t)blk_cnt > (uint64_t)SIZE_MAX / v->dev->sector_size) return EIO;
  if (fl_bdev_read(v->dev, blk_id, blk_cnt, buf) != FL_OK) return EIO;
  /* lwext4 relit le superbloc : il doit voir exactement celui qui a été validé, même si
   * un périphérique malveillant répond autre chose à la deuxième lecture. */
  uint64_t start = blk_id * v->dev->sector_size; /* < taille du disque, vérifiée ci-dessus */
  uint64_t end = start + (uint64_t)blk_cnt * v->dev->sector_size;
  uint64_t lo = start > 1024 ? start : 1024, hi = end < 2048 ? end : 2048;
  if (lo < hi) memcpy((uint8_t *)buf + (lo - start), v->sb + (lo - 1024), (size_t)(hi - lo));
  return EOK;
}

/* En lecture seule pour de vrai : lwext4 ne doit jamais écrire, et s'il essaie, il échoue. */
static int bd_bwrite(struct ext4_blockdev *bd, const void *buf, uint64_t blk_id, uint32_t blk_cnt) {
  ext4_vol *v = bd->bdif->p_user;
  if (!v->rw) return EROFS;
  if (v->io_err) return EIO;
  if (blk_cnt == 0) return EOK;
  if ((uint64_t)blk_cnt > (uint64_t)SIZE_MAX / v->dev->sector_size) return EIO;
  if (fl_bdev_write(v->dev, blk_id, blk_cnt, buf) != FL_OK) {
    v->io_err = true;
    return EIO;
  }
  /* le superbloc écrit devient celui que bd_bread sert */
  uint64_t start = blk_id * v->dev->sector_size;
  uint64_t end = start + (uint64_t)blk_cnt * v->dev->sector_size;
  uint64_t lo = start > 1024 ? start : 1024, hi = end < 2048 ? end : 2048;
  if (lo < hi) memcpy(v->sb + (lo - 1024), (const uint8_t *)buf + (lo - start), (size_t)(hi - lo));
  return EOK;
}

/* Barrière d'écriture de lwext4 (correctif 0004) : vidage du cache du périphérique (sur USB,
 * SYNCHRONIZE CACHE). */
static int bd_bflush(struct ext4_blockdev *bd) {
  ext4_vol *v = bd->bdif->p_user;
  if (v->io_err) return EIO;
  if (fl_bdev_flush(v->dev) != FL_OK) {
    v->io_err = true;
    return EIO;
  }
  return EOK;
}

/* Après une erreur d'écriture, rien n'est plus écrit (bd_bwrite, bd_bflush) : le disque reste
 * figé dans l'état du moment de la panne, que le journal sait récupérer, au lieu de recevoir
 * l'état de lwext4, qui diverge de celui du disque dès qu'une écriture a échoué (lwext4 ignore
 * les erreurs d'écriture de son cache de blocs). Les opérations d'écriture rendent alors FL_EIO ;
 * la lecture continue. */

/* Lecture seule collante (§ 4.2) : une erreur d'écriture → RO_IOERR ; une somme fausse vue par
 * lwext4 (EBADMSG) sur un volume en écriture → RO_CSUM. Les écritures suivantes rendent FL_EROFS
 * (fs.c), la lecture continue. rd_err : code lwext4 d'une lecture ; wr_err : d'une écriture, qui
 * rend FL_EIO si une écriture a échoué pendant l'opération (lwext4 ignore les erreurs d'écriture de
 * son cache). Sous g_lock. */
static void sticky(ext4_vol *v, int e) {
  if (!v->rw) return;
  if (v->io_err) fl_fs_go_read_only(v->fs, FL_RO_IOERR);
  else if (e == EBADMSG) fl_fs_go_read_only(v->fs, FL_RO_CSUM);
}
static int rd_err(ext4_vol *v, int e) {
  sticky(v, e);
  return map_err(e);
}
static int wr_fl(ext4_vol *v, int rc) {
  sticky(v, EOK);
  return v->io_err && (rc == FL_OK || rc == FL_EIO) ? FL_EIO : rc;
}
static int wr_err(ext4_vol *v, int e) {
  sticky(v, e);
  return wr_fl(v, map_err(e));
}

/* ---------- validation du superbloc (avant lwext4) ---------- */

typedef struct {
  uint32_t block_size, inodes_count;
  bool recover;
} sb_info;

/* Lit et valide le superbloc dans sb[1024] (copie conservée pour bd_bread). */
static int sb_validate(fl_bdev *dev, uint8_t sb[1024], sb_info *out) {
  uint32_t ss = dev->sector_size;
  if (ss < 512 || ss > 4096 || !pow2(ss)) return FL_ENOTSUP;
  if (dev->sector_count > UINT64_MAX / ss) return FL_EINVAL;
  uint64_t dev_bytes = dev->sector_count * ss;
  if (dev_bytes < 2048) return FL_ECORRUPT;

  int rc = fl_bdev_read_bytes(dev, 1024, 1024, sb);
  if (rc != FL_OK) return rc;
  if (le16(sb + 0x38) != EXT4_SUPERBLOCK_MAGIC) return FL_ECORRUPT;

  uint32_t log = le32(sb + 0x18);
  if (log > 6) return FL_ECORRUPT; /* 1 Kio à 64 Kio */
  uint32_t bs = 1024u << log;
  if (bs % ss) return FL_ENOTSUP; /* bloc ext4 plus petit qu'un secteur */

  uint32_t incompat = le32(sb + 0x60);
  if (incompat & ~(uint32_t)CONFIG_SUPPORTED_FINCOM) return FL_ENOTSUP;

  uint32_t icount = le32(sb + 0x00), first = le32(sb + 0x14);
  uint32_t bpg = le32(sb + 0x20), ipg = le32(sb + 0x28);
  uint16_t isz = le16(sb + 0x58);
  if (!icount || !bpg || bpg > 8u * bs || !ipg || ipg > 8u * bs) return FL_ECORRUPT;
  if (isz < 128 || isz > bs || !pow2(isz)) return FL_ECORRUPT;

  uint64_t bcount = le32(sb + 0x04);
  if (incompat & EXT4_FINCOM_64BIT) {
    bcount |= (uint64_t)le32(sb + 0x150) << 32;
    uint16_t ds = le16(sb + 0xFE);
    if (ds < 32 || ds > bs || !pow2(ds)) return FL_ECORRUPT;
    if (ds > EXT4_MAX_BLOCK_GROUP_DESCRIPTOR_SIZE) return FL_ENOTSUP;
  }
  if (first >= bcount) return FL_ECORRUPT;
  if (bcount > dev_bytes / bs) return FL_ECORRUPT; /* système plus grand que le disque */
  uint64_t groups = (bcount - first + bpg - 1) / bpg;
  if ((icount + (uint64_t)ipg - 1) / ipg > groups) return FL_ECORRUPT;

  out->block_size = bs;
  out->inodes_count = icount;
  out->recover = (incompat & EXT4_FINCOM_RECOVER) != 0;
  return FL_OK;
}

/* ---------- utilitaires ---------- */

/* "/fl<n>/" + chemin sans son "/" initial (la racine donne le point de montage seul). */
static void full_path(const ext4_vol *v, const char *path, char *buf) {
  snprintf(buf, FULL_CAP, "%s%s", v->mp, path + 1);
}

static void fill_stat(const struct ext4_inode *in, fl_stat *st) {
  uint32_t mode = to_le16(in->mode);
  memset(st, 0, sizeof *st);
  switch (mode & EXT4_INODE_MODE_TYPE_MASK) {
    case EXT4_INODE_MODE_FILE: st->type = FL_FT_FILE; break;
    case EXT4_INODE_MODE_DIRECTORY: st->type = FL_FT_DIR; break;
    case EXT4_INODE_MODE_SOFTLINK: st->type = FL_FT_SYMLINK; break;
    default: st->type = FL_FT_OTHER; break;
  }
  st->mode = mode;
  st->uid = to_le16(in->uid) | (uint32_t)to_le16(in->osd2.linux2.uid_high) << 16;
  st->gid = to_le16(in->gid) | (uint32_t)to_le16(in->osd2.linux2.gid_high) << 16;
  st->mtime = (int32_t)to_le32(in->modification_time);
  /* bits d'époque (après 2038) ; les octets au-delà de la taille d'inode du disque
   * sont nuls dans la copie, extra_isize vaut donc 0 pour des inodes de 128 octets */
  if (to_le16(in->extra_isize) >= 12) st->mtime += (int64_t)(to_le32(in->mtime_extra) & 3) << 32;
  st->size = to_le32(in->size_lo);
  /* comme le noyau (ext4_isize) : 64 bits pour les fichiers ordinaires */
  if (st->type == FL_FT_FILE) st->size |= (uint64_t)to_le32(in->size_hi) << 32;
}

/* Inode d'un chemin (sous g_lock). */
static int inode_of(ext4_vol *v, const char *path, struct ext4_inode *in) {
  char full[FULL_CAP];
  full_path(v, path, full);
  return rd_err(v, ext4_raw_inode_fill(full, NULL, in));
}

/* ---------- opérations ---------- */

static int e_statfs(fl_fs *fs, fl_statfs *out) {
  ext4_vol *v = fs->priv;
  struct ext4_mount_stats st;
  pthread_mutex_lock(&g_lock);
  int r = ext4_mount_point_stats(v->mp, &st);
  pthread_mutex_unlock(&g_lock);
  if (r != EOK) return rd_err(v, r);
  uint64_t free_blocks = st.free_blocks_count < st.blocks_count ? st.free_blocks_count : st.blocks_count;
  /* blocks_count × block_size ≤ taille du disque (vérifié au montage) */
  out->total_bytes = st.blocks_count * st.block_size;
  out->free_bytes = free_blocks * st.block_size;
  memcpy(out->label, st.volume_name, sizeof st.volume_name);
  out->label[sizeof st.volume_name] = 0;
  out->needs_recovery = v->needs_recovery;
  return FL_OK;
}

static int e_stat(fl_fs *fs, const char *path, fl_stat *out) {
  ext4_vol *v = fs->priv;
  struct ext4_inode in;
  pthread_mutex_lock(&g_lock);
  int rc = inode_of(v, path, &in);
  pthread_mutex_unlock(&g_lock);
  if (rc != FL_OK) return rc;
  fill_stat(&in, out);
  return FL_OK;
}

/* Entrée suivante utilisable (sous g_lock). FL_OK et *fin = true en fin de dossier.
 * Plus de MAX_DIR_STEPS entrées ignorées d'affilée → FL_ECORRUPT. */
static int next_entry(ext4_vol *v, ext4_dir *d, fl_dirent *e, bool *fin) {
  for (uint32_t steps = 0; steps < MAX_DIR_STEPS; steps++) {
    const ext4_direntry *de = NULL;
    int r = ext4_dir_entry_get(d, &de);
    if (r != EOK) return rd_err(v, r);
    if (!de) {
      *fin = true;
      return FL_OK;
    }
    size_t n = de->name_length; /* ≤ 255 (uint8_t) */
    /* entrée libre, ".", "..", nom inadressable par un chemin, inode hors bornes : ignorés */
    if (de->inode == 0 || de->inode > v->inodes_count || n == 0) continue;
    if ((n == 1 && de->name[0] == '.') || (n == 2 && de->name[0] == '.' && de->name[1] == '.')) continue;
    if (memchr(de->name, '/', n) || memchr(de->name, 0, n)) continue;
    struct ext4_inode in;
    r = ext4_raw_inode_fill_ino(v->mp, de->inode, &in);
    if (r != EOK) return rd_err(v, r);
    memcpy(e->name, de->name, n);
    e->name[n] = 0;
    fill_stat(&in, &e->st);
    *fin = false;
    return FL_OK;
  }
  return FL_ECORRUPT;
}

static int e_readdir(fl_fs *fs, const char *path, fl_readdir_cb cb, void *ctx) {
  ext4_vol *v = fs->priv;
  char full[FULL_CAP];
  struct ext4_inode in;
  ext4_dir d;
  full_path(v, path, full);

  pthread_mutex_lock(&g_lock);
  int rc = inode_of(v, path, &in);
  if (rc == FL_OK && (to_le16(in.mode) & EXT4_INODE_MODE_TYPE_MASK) != EXT4_INODE_MODE_DIRECTORY)
    rc = FL_ENOTDIR;
  if (rc == FL_OK) rc = rd_err(v, ext4_dir_open(&d, full));
  if (rc == FL_OK) v->open_files++; /* un démontage depuis le rappel est refusé */
  pthread_mutex_unlock(&g_lock);
  if (rc != FL_OK) return rc;

  fl_dirent e;
  for (;;) {
    bool fin = false;
    pthread_mutex_lock(&g_lock);
    rc = next_entry(v, &d, &e, &fin);
    pthread_mutex_unlock(&g_lock);
    if (rc != FL_OK || fin) break;
    rc = cb(ctx, &e); /* hors verrou : le rappel peut rappeler l'API */
    if (rc != 0) break;
  }

  pthread_mutex_lock(&g_lock);
  ext4_dir_close(&d);
  v->open_files--;
  pthread_mutex_unlock(&g_lock);
  return rc;
}

/* Parent de path ("/a/b" → "/a", "/a" → "/") dans buf (FULL_CAP), nom final dans *name. */
static void split_parent(const char *path, char *buf, const char **name) {
  const char *slash = strrchr(path, '/');
  size_t n = (size_t)(slash - path);
  if (n == 0) n = 1; /* parent : la racine */
  memcpy(buf, path, n);
  buf[n] = 0;
  *name = slash + 1;
}

/* Le dossier parent de path doit exister (sous g_lock) : FL_OK, FL_ENOENT ou FL_ENOTDIR.
 * Jamais de création intermédiaire. */
static int parent_ok(ext4_vol *v, const char *path) {
  char parent[FULL_CAP];
  const char *name;
  struct ext4_inode in;
  split_parent(path, parent, &name);
  int rc = inode_of(v, parent, &in);
  if (rc == FL_OK && (to_le16(in.mode) & EXT4_INODE_MODE_TYPE_MASK) != EXT4_INODE_MODE_DIRECTORY)
    rc = FL_ENOTDIR;
  return rc;
}

/* Inode et numéro d'un chemin (sous g_lock). */
static int inode_ino_of(ext4_vol *v, const char *path, struct ext4_inode *in, uint32_t *ino) {
  char full[FULL_CAP];
  full_path(v, path, full);
  return rd_err(v, ext4_raw_inode_fill(full, ino, in));
}

static uint32_t inode_type(const struct ext4_inode *in) {
  return to_le16(in->mode) & EXT4_INODE_MODE_TYPE_MASK;
}

/* Fichier ouvert sur l'inode ino (sous g_lock). */
static bool ino_open(const ext4_vol *v, uint32_t ino) {
  for (const ext4_fh *h = v->files; h; h = h->next)
    if (h->f.inode == ino) return true;
  return false;
}

/* Vide le cache de lwext4 puis celui du périphérique (sous g_lock). */
static int sync_locked(ext4_vol *v) {
  int rc = map_err(ext4_cache_flush(v->mp));
  int rf = fl_bdev_flush(v->dev);
  return rc != FL_OK ? rc : rf;
}

static int e_close(fl_file *f);

static int e_open(fl_fs *fs, const char *path, int flags, fl_file **out) {
  ext4_vol *v = fs->priv;
  char full[FULL_CAP];
  struct ext4_inode in;
  full_path(v, path, full);
  fl_file *f = calloc(1, sizeof *f);
  ext4_fh *h = calloc(1, sizeof *h);
  if (!f || !h) {
    free(f);
    free(h);
    return FL_ENOMEM;
  }

  pthread_mutex_lock(&g_lock);
  int rc = inode_of(v, path, &in);
  bool creer = false, ouvert = false;
  if (rc == FL_OK) {
    uint32_t t = inode_type(&in);
    if (t == EXT4_INODE_MODE_DIRECTORY) rc = FL_EISDIR;
    else if (t != EXT4_INODE_MODE_FILE) rc = FL_ENOTSUP; /* pas de suivi de lien */
  } else if (rc == FL_ENOENT && (flags & FL_O_CREAT)) {
    rc = parent_ok(v, path);
    creer = rc == FL_OK;
  }
  if (rc == FL_OK && (creer || (flags & FL_O_TRUNC)) && v->io_err) rc = FL_EIO;
  if (rc == FL_OK) {
    int r = ext4_fopen2(&h->f, full, creer ? O_RDWR | O_CREAT : flags & FL_O_RDWR ? O_RDWR : O_RDONLY);
    rc = creer ? wr_err(v, r) : rd_err(v, r);
  }
  if (rc == FL_OK) {
    h->next = v->files;
    v->files = h;
    v->open_files++;
    ouvert = true;
    if (flags & FL_O_TRUNC) {
      /* toujours, même vide : mtime et ctime sont mis à jour (POSIX) */
      h->written = true;
      rc = wr_err(v, (ext4_ftruncate(&h->f, 0)));
    }
  }
  pthread_mutex_unlock(&g_lock);

  f->fs = fs;
  f->flags = flags;
  f->priv = h;
  if (rc != FL_OK) {
    if (ouvert) e_close(f); /* ouvert, puis la troncature a échoué */
    else {
      free(f);
      free(h);
    }
    return rc;
  }
  *out = f;
  return FL_OK;
}

/* Positionnement puis lecture sous le même verrou : la position partagée de ext4_file
 * n'est jamais observée entre deux appels, les pread entrelacés restent justes. */
static int64_t e_pread(fl_file *f, void *buf, size_t len, uint64_t off) {
  ext4_vol *v = f->fs->priv;
  ext4_fh *h = f->priv;
  if (off >= v->max_bytes) return 0;
  if (len > v->max_bytes - off) len = (size_t)(v->max_bytes - off); /* max_bytes ≤ 2^48 */
  size_t got = 0;
  pthread_mutex_lock(&g_lock);
  int r = ext4_fseek(&h->f, (int64_t)off, SEEK_SET);
  if (r == EOK) r = ext4_fread(&h->f, buf, len, &got); /* rien au-delà de la fin */
  pthread_mutex_unlock(&g_lock);
  if (r != EOK) return rd_err(v, r);
  return (int64_t)got;
}

/* Tout ou rien vu de l'appelant : une écriture interrompue rend l'erreur (le fichier garde ce
 * qui a été écrit, taille comprise). */
static int64_t e_pwrite(fl_file *f, const void *buf, size_t len, uint64_t off) {
  ext4_vol *v = f->fs->priv;
  ext4_fh *h = f->priv;
  if (len == 0) return 0;
  if (off > v->max_bytes || len > v->max_bytes - off || len > INT64_MAX) return FL_EINVAL;
  size_t done = 0;
  pthread_mutex_lock(&g_lock);
  h->written = true;
  int r = v->io_err ? EIO : ext4_fseek(&h->f, (int64_t)off, SEEK_SET);
  if (r == EOK) r = ext4_fwrite(&h->f, buf, len, &done);
  int rc = wr_err(v, r);
  pthread_mutex_unlock(&g_lock);
  if (rc != FL_OK) return rc;
  if (done != len) return FL_EIO;
  return (int64_t)len;
}

static int e_truncate(fl_file *f, uint64_t size) {
  ext4_vol *v = f->fs->priv;
  ext4_fh *h = f->priv;
  if (size > v->max_bytes) return FL_EINVAL;
  pthread_mutex_lock(&g_lock);
  h->written = true;
  int rc = v->io_err ? FL_EIO : wr_err(v, (ext4_ftruncate(&h->f, size)));
  pthread_mutex_unlock(&g_lock);
  return rc;
}

/* § 4.2 : sync à chaque fermeture d'un fichier ouvert en écriture (écrit ou tronqué). */
static int e_close(fl_file *f) {
  ext4_vol *v = f->fs->priv;
  ext4_fh *h = f->priv;
  int rc = FL_OK;
  pthread_mutex_lock(&g_lock);
  ext4_fclose(&h->f);
  for (ext4_fh **p = &v->files; *p; p = &(*p)->next)
    if (*p == h) {
      *p = h->next;
      break;
    }
  v->open_files--;
  if (h->written) rc = wr_fl(v, v->io_err ? FL_EIO : sync_locked(v));
  pthread_mutex_unlock(&g_lock);
  free(h);
  free(f);
  return rc;
}

static int e_mkdir(fl_fs *fs, const char *path) {
  ext4_vol *v = fs->priv;
  char full[FULL_CAP];
  struct ext4_inode in;
  full_path(v, path, full);
  pthread_mutex_lock(&g_lock);
  int rc = inode_of(v, path, &in);
  if (rc == FL_OK) rc = FL_EEXIST;
  else if (rc == FL_ENOENT) {
    rc = parent_ok(v, path);
    if (rc == FL_OK && v->io_err) rc = FL_EIO;
    if (rc == FL_OK) rc = wr_err(v, (ext4_dir_mk(full)));
  }
  pthread_mutex_unlock(&g_lock);
  return rc;
}

static int e_unlink(fl_fs *fs, const char *path) {
  ext4_vol *v = fs->priv;
  char full[FULL_CAP];
  struct ext4_inode in;
  uint32_t ino = 0;
  if (!strcmp(path, "/")) return FL_EISDIR;
  full_path(v, path, full);
  pthread_mutex_lock(&g_lock);
  int rc = inode_ino_of(v, path, &in, &ino);
  if (rc == FL_OK && inode_type(&in) == EXT4_INODE_MODE_DIRECTORY) rc = FL_EISDIR;
  /* lwext4 libérerait l'inode sous le descripteur ouvert */
  if (rc == FL_OK && ino_open(v, ino)) rc = FL_EBUSY;
  if (rc == FL_OK && v->io_err) rc = FL_EIO;
  if (rc == FL_OK) rc = wr_err(v, (ext4_fremove(full)));
  pthread_mutex_unlock(&g_lock);
  return rc;
}

/* Dossier vide : seulement « . », « .. » et des entrées libres (sous g_lock). */
static int dir_empty(ext4_vol *v, const char *full, bool *empty) {
  ext4_dir d;
  int rc = rd_err(v, ext4_dir_open(&d, full));
  if (rc != FL_OK) return rc;
  *empty = true;
  for (uint32_t steps = 0; steps < MAX_DIR_STEPS; steps++) {
    const ext4_direntry *de = NULL;
    int r = ext4_dir_entry_get(&d, &de);
    if (r != EOK) {
      rc = rd_err(v, r);
      break;
    }
    if (!de) break;
    size_t n = de->name_length;
    if (de->inode == 0 || (n == 1 && de->name[0] == '.') || (n == 2 && de->name[0] == '.' && de->name[1] == '.'))
      continue;
    *empty = false; /* toute autre entrée, même inadressable : non vide (comme lwext4) */
    break;
  }
  ext4_dir_close(&d);
  return rc;
}

static int e_rmdir(fl_fs *fs, const char *path) {
  ext4_vol *v = fs->priv;
  char full[FULL_CAP];
  struct ext4_inode in;
  bool empty = false;
  if (!strcmp(path, "/")) return FL_EINVAL;
  full_path(v, path, full);
  pthread_mutex_lock(&g_lock);
  int rc = inode_of(v, path, &in);
  if (rc == FL_OK && inode_type(&in) != EXT4_INODE_MODE_DIRECTORY) rc = FL_ENOTDIR;
  if (rc == FL_OK) rc = dir_empty(v, full, &empty);
  if (rc == FL_OK && !empty) rc = FL_ENOTEMPTY;
  /* vide : ext4_dir_rm (récursif) ne supprime que lui */
  if (rc == FL_OK && v->io_err) rc = FL_EIO;
  if (rc == FL_OK) rc = wr_err(v, (ext4_dir_rm(full)));
  pthread_mutex_unlock(&g_lock);
  return rc;
}

static int e_rename(fl_fs *fs, const char *from, const char *to) {
  ext4_vol *v = fs->priv;
  char ffrom[FULL_CAP], fto[FULL_CAP];
  struct ext4_inode in;
  size_t nf = strlen(from);
  if (!strcmp(from, "/")) return FL_EINVAL;
  /* to sous from : le dossier deviendrait son propre descendant */
  if (!strncmp(to, from, nf) && to[nf] == '/') return FL_EINVAL;
  full_path(v, from, ffrom);
  full_path(v, to, fto);
  pthread_mutex_lock(&g_lock);
  int rc = inode_of(v, from, &in);
  if (rc == FL_OK) {
    rc = inode_of(v, to, &in);
    if (rc == FL_OK) rc = FL_EEXIST; /* pas de remplacement */
    else if (rc == FL_ENOENT) rc = parent_ok(v, to);
  }
  if (rc == FL_OK && v->io_err) rc = FL_EIO;
  if (rc == FL_OK) rc = wr_err(v, (ext4_frename(ffrom, fto)));
  pthread_mutex_unlock(&g_lock);
  return rc;
}

static int e_sync(fl_fs *fs) {
  ext4_vol *v = fs->priv;
  pthread_mutex_lock(&g_lock);
  int rc = v->io_err ? FL_EIO : wr_fl(v, sync_locked(v));
  pthread_mutex_unlock(&g_lock);
  return rc;
}

static void vol_free(fl_fs *fs, ext4_vol *v) {
  if (v) free(v->ph_buf);
  free(v);
  free(fs);
}

/* En écriture : arrêt du journal (point de contrôle, journal propre), démontage (état
 * « propre » du superbloc), puis vidage du périphérique. */
static int e_unmount(fl_fs *fs) {
  ext4_vol *v = fs->priv;
  pthread_mutex_lock(&g_lock);
  if (v->open_files) {
    pthread_mutex_unlock(&g_lock);
    return FL_EINVAL;
  }
  /* Disque mort : tout est libéré quand même (correctif 0004 : ext4_umount libère le point de
   * montage sur erreur), et la première erreur est rendue. */
  int rc = v->rw ? map_err(ext4_journal_stop(v->mp)) : FL_OK;
  int r = map_err(ext4_umount(v->mp));
  if (rc == FL_OK) rc = r;
  ext4_device_unregister(v->dev_name);
  if (v->rw && rc == FL_OK) rc = v->io_err ? FL_EIO : fl_bdev_flush(v->dev);
  if (v->io_err && rc == FL_OK) rc = FL_EIO;
  pthread_mutex_unlock(&g_lock);
  vol_free(fs, v);
  return rc;
}

static const fl_fs_ops k_ext4_ops = {
    .statfs = e_statfs,
    .stat = e_stat,
    .readdir = e_readdir,
    .open = e_open,
    .pread = e_pread,
    .close = e_close,
    .pwrite = e_pwrite,
    .truncate = e_truncate,
    .mkdir = e_mkdir,
    .unlink = e_unlink,
    .rmdir = e_rmdir,
    .rename = e_rename,
    .sync = e_sync,
    .unmount = e_unmount,
};

/* want_rw : écriture demandée sur un périphérique inscriptible (fs.c a déjà traité la lecture
 * seule demandée ou imposée par le périphérique). L'écriture n'est permise que si
 * fl_ext4_write_policy le permet ; sinon lecture seule avec la raison. */
int fl_ext4_mount(fl_bdev *dev, bool want_rw, fl_fs **out) {
  *out = NULL;
  fl_fs *fs = calloc(1, sizeof *fs);
  ext4_vol *v = calloc(1, sizeof *v);
  if (!fs || !v) {
    vol_free(fs, v);
    return FL_ENOMEM;
  }
  sb_info sbi;
  int rc = sb_validate(dev, v->sb, &sbi);
  if (rc == FL_OK && !(v->ph_buf = malloc(dev->sector_size))) rc = FL_ENOMEM;
  if (rc != FL_OK) {
    vol_free(fs, v);
    return rc;
  }
  /* règles du § 4.2 (ext4_policy.c), avant que lwext4 n'écrive quoi que ce soit */
  bool permis = false;
  char raison[128] = "";
  if (want_rw) rc = fl_ext4_write_policy(dev, v->sb, &permis, raison);
  if (rc != FL_OK) {
    vol_free(fs, v);
    return rc;
  }
  v->dev = dev;
  v->fs = fs;
  v->block_size = sbi.block_size;
  v->inodes_count = sbi.inodes_count;
  v->max_bytes = (uint64_t)sbi.block_size << 32;
  v->needs_recovery = sbi.recover;
  v->rw = want_rw && permis;
  v->bdif.open = bd_open;
  v->bdif.bread = bd_bread;
  v->bdif.bwrite = bd_bwrite;
  v->bdif.close = bd_close;
  v->bdif.bflush = bd_bflush;
  v->bdif.ph_bsize = dev->sector_size;
  v->bdif.ph_bcnt = dev->sector_count;
  v->bdif.ph_bbuf = v->ph_buf;
  v->bdif.p_user = v;
  v->bd.bdif = &v->bdif;
  v->bd.part_offset = 0;
  v->bd.part_size = dev->sector_count * dev->sector_size; /* sans débordement (sb_validate) */

  pthread_mutex_lock(&g_lock);
  uint32_t id = g_counter++;
  snprintf(v->dev_name, sizeof v->dev_name, "fl%u", (unsigned)id);
  snprintf(v->mp, sizeof v->mp, "/fl%u/", (unsigned)id);
  int r = ext4_device_register(&v->bd, v->dev_name);
  if (r == EOK) {
    if (v->rw) ext4_set_policy(&k_policy);
    r = ext4_mount(v->dev_name, v->mp, !v->rw);
    if (r == EOK && v->rw) {
      r = ext4_journal_start(v->mp);
      if (r != EOK) ext4_umount(v->mp);
    }
    if (r != EOK) ext4_device_unregister(v->dev_name);
  }
  pthread_mutex_unlock(&g_lock);
  if (r != EOK) {
    vol_free(fs, v);
    return map_err(r);
  }

  fs->ops = &k_ext4_ops;
  fs->dev = dev;
  fs->read_only = !v->rw;
  if (want_rw && !permis) snprintf(fs->ro_reason, sizeof fs->ro_reason, "%s", raison);
  fs->priv = v;
  *out = fs;
  return FL_OK;
}
