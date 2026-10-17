/* ext4 en lecture seule, par lwext4, branché sur fl_bdev.
 *
 * Fils d'exécution : lwext4 garde des registres globaux (périphériques, points de
 * montage) et des caches par montage, sans aucun verrou. Tout appel à lwext4 passe donc
 * sous le mutex global g_lock (initialisé statiquement), qui protège aussi le compteur
 * de noms et le nombre de fichiers ouverts de chaque volume. Aucun rappel utilisateur
 * n'est appelé sous ce verrou. Une phase ultérieure ajoutera un verrou par volume
 * au-dessus.
 *
 * Chaque montage enregistre son propre périphérique "fl<n>" et son point de montage
 * "/fl<n>/" (n unique, tiré du compteur) ; les deux sont libérés au démontage et sur tout
 * échec de montage, pour ne jamais épuiser les 8 emplacements de lwext4.
 *
 * Le disque est hostile : le superbloc est validé ici avant que lwext4 ne s'en serve,
 * les numéros d'inode des entrées de dossier sont vérifiés, et le journal n'est jamais
 * rejoué (needs_recovery le signale). */

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
#include "fs/fs_internal.h"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_counter; /* protégé par g_lock */

#define MP_CAP 24                 /* "/fl4294967295/" + NUL tient largement */
#define FULL_CAP (MP_CAP + 4096)  /* point de montage + chemin validé (≤ 4095 octets) */
#define MAX_DIR_STEPS (1u << 24)  /* garde-fou contre un dossier hostile interminable */

typedef struct {
  struct ext4_blockdev bd;
  struct ext4_blockdev_iface bdif;
  uint8_t *ph_buf;
  fl_bdev *dev;
  char dev_name[MP_CAP];
  char mp[MP_CAP];
  uint32_t open_files; /* fichiers ouverts + parcours de dossier en cours (g_lock) */
  uint32_t block_size;
  uint32_t inodes_count;
  uint64_t max_bytes; /* au-delà, les numéros de bloc logique (32 bits) débordent */
  bool needs_recovery;
  uint8_t sb[1024];   /* superbloc validé, octets [1024, 2048) du disque */
} ext4_vol;

typedef struct {
  ext4_file f;
  uint64_t size; /* taille lisible : min(taille de l'inode, max_bytes) */
} ext4_fh;

static int map_err(int e) {
  switch (e) {
    case EOK: return FL_OK;
    case ENOENT: return FL_ENOENT;
    case ENOTDIR: return FL_ENOTDIR;
    case ENOMEM: return FL_ENOMEM;
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

/* Lecture seule pour de vrai : lwext4 ne doit jamais écrire, et s'il essaie, il échoue. */
static int bd_bwrite(struct ext4_blockdev *bd, const void *buf, uint64_t blk_id, uint32_t blk_cnt) {
  (void)bd, (void)buf, (void)blk_id, (void)blk_cnt;
  return EROFS;
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
  return map_err(ext4_raw_inode_fill(full, NULL, in));
}

/* ---------- opérations ---------- */

static int e_statfs(fl_fs *fs, fl_statfs *out) {
  ext4_vol *v = fs->priv;
  struct ext4_mount_stats st;
  pthread_mutex_lock(&g_lock);
  int r = ext4_mount_point_stats(v->mp, &st);
  pthread_mutex_unlock(&g_lock);
  if (r != EOK) return map_err(r);
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

/* Entrée suivante utilisable (sous g_lock). FL_OK et *fin = true en fin de dossier. */
static int next_entry(ext4_vol *v, ext4_dir *d, fl_dirent *e, bool *fin) {
  for (uint32_t steps = 0; steps < MAX_DIR_STEPS; steps++) {
    const ext4_direntry *de = NULL;
    int r = ext4_dir_entry_get(d, &de);
    if (r != EOK) return map_err(r);
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
    if (r != EOK) return map_err(r);
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
  if (rc == FL_OK) rc = map_err(ext4_dir_open(&d, full));
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
  if (rc == FL_OK) {
    uint32_t t = to_le16(in.mode) & EXT4_INODE_MODE_TYPE_MASK;
    if (t == EXT4_INODE_MODE_DIRECTORY) rc = FL_EISDIR;
    else if (t != EXT4_INODE_MODE_FILE) rc = FL_ENOTSUP; /* pas de suivi de lien en phase 1 */
  }
  if (rc == FL_OK) rc = map_err(ext4_fopen2(&h->f, full, O_RDONLY));
  if (rc == FL_OK) v->open_files++;
  pthread_mutex_unlock(&g_lock);

  if (rc != FL_OK) {
    free(f);
    free(h);
    return rc;
  }
  h->size = ext4_fsize(&h->f) < v->max_bytes ? ext4_fsize(&h->f) : v->max_bytes;
  f->fs = fs;
  f->flags = flags;
  f->priv = h;
  *out = f;
  return FL_OK;
}

/* Positionnement puis lecture sous le même verrou : la position partagée de ext4_file
 * n'est jamais observée entre deux appels, les pread entrelacés restent justes. */
static int64_t e_pread(fl_file *f, void *buf, size_t len, uint64_t off) {
  ext4_fh *h = f->priv;
  if (off >= h->size) return 0;
  if (len > h->size - off) len = (size_t)(h->size - off); /* h->size < 2^48 */
  size_t got = 0;
  pthread_mutex_lock(&g_lock);
  int r = ext4_fseek(&h->f, (int64_t)off, SEEK_SET);
  if (r == EOK) r = ext4_fread(&h->f, buf, len, &got);
  pthread_mutex_unlock(&g_lock);
  if (r != EOK) return map_err(r);
  return (int64_t)got;
}

static int e_close(fl_file *f) {
  ext4_vol *v = f->fs->priv;
  ext4_fh *h = f->priv;
  pthread_mutex_lock(&g_lock);
  ext4_fclose(&h->f);
  v->open_files--;
  pthread_mutex_unlock(&g_lock);
  free(h);
  free(f);
  return FL_OK;
}

static void vol_free(fl_fs *fs, ext4_vol *v) {
  if (v) free(v->ph_buf);
  free(v);
  free(fs);
}

static int e_unmount(fl_fs *fs) {
  ext4_vol *v = fs->priv;
  pthread_mutex_lock(&g_lock);
  if (v->open_files) {
    pthread_mutex_unlock(&g_lock);
    return FL_EINVAL;
  }
  int r = ext4_umount(v->mp);
  if (r != EOK) {
    /* lwext4 garde encore un pointeur vers v : ne rien libérer */
    pthread_mutex_unlock(&g_lock);
    return map_err(r);
  }
  ext4_device_unregister(v->dev_name);
  pthread_mutex_unlock(&g_lock);
  vol_free(fs, v);
  return FL_OK;
}

static const fl_fs_ops k_ext4_ops = {
    .statfs = e_statfs,
    .stat = e_stat,
    .readdir = e_readdir,
    .open = e_open,
    .pread = e_pread,
    .close = e_close,
    .unmount = e_unmount,
    /* écriture : phase ultérieure (NULL → FL_EROFS) */
};

int fl_ext4_mount(fl_bdev *dev, bool want_rw, fl_fs **out) {
  (void)want_rw; /* phase 1 : toujours en lecture seule */
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
  v->dev = dev;
  v->block_size = sbi.block_size;
  v->inodes_count = sbi.inodes_count;
  v->max_bytes = (uint64_t)sbi.block_size << 32;
  v->needs_recovery = sbi.recover;
  v->bdif.open = bd_open;
  v->bdif.bread = bd_bread;
  v->bdif.bwrite = bd_bwrite;
  v->bdif.close = bd_close;
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
    r = ext4_mount(v->dev_name, v->mp, true);
    if (r != EOK) ext4_device_unregister(v->dev_name);
  }
  pthread_mutex_unlock(&g_lock);
  if (r != EOK) {
    vol_free(fs, v);
    return map_err(r);
  }

  fs->ops = &k_ext4_ops;
  fs->dev = dev;
  fs->read_only = true;
  fs->priv = v;
  *out = fs;
  return FL_OK;
}
