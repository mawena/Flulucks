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
 * démontage. Écriture différée par fichier (wbuf.c, correctif 0.2.1) : chaque fl_file_pwrite devenait
 * une transaction de journal complète (≈ 3,6 vidages de cache, ~20 ms chacun sur un disque dur) ; les
 * écritures contiguës d'un fichier sont désormais regroupées par 4 Mio. Un seul tampon actif par
 * volume (v->wb_fh), vidé au début de toute autre opération (voir flush_all) ; un vidage en échec
 * laisse son erreur sur son fichier (collante), rendue par l'écriture, la troncature et la fermeture
 * suivantes de ce fichier et par e_sync. Chaque opération est vérifiée ici
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
#include "fs/wbuf.h"

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
  ext4_fh *wb_fh;      /* fichier dont le tampon d'écriture contient des données, ou NULL (g_lock) */
  uint32_t block_size;
  uint32_t inodes_count;
  uint64_t max_bytes; /* au-delà, les numéros de bloc logique (32 bits) débordent */
  uint64_t max_write; /* fin d'écriture au plus : lwext4 refuse le dernier bloc logique (EFBIG) */
  bool needs_recovery;
  bool rw;            /* monté en écriture, journal démarré */
  bool io_err;        /* une écriture ou une barrière a échoué : plus rien n'est écrit (g_lock) */
  bool io_wp;         /* l'échec venait d'une protection en écriture signalée (FL_EROFS) */
  bool frozen;        /* passé en lecture seule en cours de session : plus rien n'est écrit, démontage
                       * compris (g_lock) */
  bool rd_fail;       /* une lecture a échoué sur le volume en écriture (g_lock) : voir wr_err */
  fl_fs *fs;          /* pour la lecture seule collante */
  uint8_t sb[1024];   /* superbloc validé, octets [1024, 2048) du disque (tenu à jour) */
} ext4_vol;

struct ext4_fh {
  ext4_file f;
  ext4_fh *next;
  bool written; /* écrit ou tronqué : sync à la fermeture */
  fl_wbuf wb;   /* écriture différée (g_lock) ; wb.err : erreur différée, collante */
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
  if (fl_bdev_read(v->dev, blk_id, blk_cnt, buf) != FL_OK) {
    v->rd_fail = true; /* sous g_lock : lwext4 n'est appelé que sous ce verrou */
    return EIO;
  }
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
  if (v->io_err || v->frozen) return EIO;
  if (blk_cnt == 0) return EOK;
  if ((uint64_t)blk_cnt > (uint64_t)SIZE_MAX / v->dev->sector_size) return EIO;
  int w = fl_bdev_write(v->dev, blk_id, blk_cnt, buf);
  if (w != FL_OK) {
    v->io_err = true;
    v->io_wp = w == FL_EROFS; /* disque qui signale sa protection en cours de session (USB) */
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
  if (v->io_err || v->frozen) return EIO;
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

/* Lecture seule collante (§ 4.2), jusqu'au démontage compris : une erreur d'écriture → RO_IOERR
 * (RO_WP si le disque signale sa protection) ; une somme fausse vue par lwext4 (EBADMSG) sur un volume
 * en écriture → RO_CSUM ; un échec d'une opération d'écriture (autre que FL_ENOSPC) → RO_IOERR,
 * RO_CORRUPT ou RO_ABORT. Le volume est alors gelé (frozen) : plus rien n'atteint le disque, ni
 * fermeture, ni sync, ni démontage. Après un échec, l'état de lwext4 n'est plus sûr (une opération
 * partiellement faite, une restauration impossible depuis le journal, ext4_fwrite qui valide sa
 * transaction même en échec) ; le disque garde l'état de la dernière transaction validée, que le
 * journal sait récupérer. Les écritures suivantes
 * rendent FL_EROFS (fs.c), la lecture continue. rd_err : code lwext4 d'une lecture ; wr_err : d'une
 * écriture, qui rend FL_EIO si une écriture a échoué pendant l'opération (lwext4 ignore les erreurs
 * d'écriture de son cache). Sous g_lock. */
static void go_ro(ext4_vol *v, const char *why) {
  fl_fs_go_read_only(v->fs, why);
  v->frozen = true;
}
static void sticky(ext4_vol *v, int e) {
  if (!v->rw) return;
  if (v->io_err) go_ro(v, v->io_wp ? FL_RO_WP : FL_RO_IOERR);
  else if (e == EBADMSG) go_ro(v, FL_RO_CSUM);
}
/* Plus rien ne doit être écrit (sous g_lock). */
static bool stopped(const ext4_vol *v) { return v->io_err || v->frozen; }
static int rd_err(ext4_vol *v, int e) {
  sticky(v, e);
  return map_err(e);
}
static int wr_fl(ext4_vol *v, int rc) {
  sticky(v, EOK);
  return stopped(v) && (rc == FL_OK || rc == FL_EIO) ? FL_EIO : rc;
}
/* Échecs sans conséquence sur le disque, qui laissent le volume en écriture : ENOTSUP et EFBIG (refus
 * de ext4_fwrite avant toute modification : fichier à blocs indirects, dernier bloc logique) ; disque
 * plein (ENOSPC : à l'annulation, lwext4 jette du cache les blocs modifiés et restaure depuis le journal
 * ceux d'une transaction validée — test_batterie_disque_plein), sauf si une lecture a échoué pendant
 * la session (rd_fail : une restauration depuis le journal a pu échouer, le cache n'est plus sûr). Tout
 * autre échec gèle le volume. */
static int wr_err(ext4_vol *v, int e) {
  sticky(v, e);
  bool sans_effet = e == EOK || e == ENOTSUP || e == EFBIG || (e == ENOSPC && !v->rd_fail);
  if (v->rw && !sans_effet && !v->frozen)
    go_ro(v, e == EIO ? FL_RO_IOERR : e == EFSCORRUPTED ? FL_RO_CORRUPT : FL_RO_ABORT);
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

/* ---------- écriture différée (wbuf.c) ---------- */

typedef struct {
  ext4_vol *v;
  ext4_fh *h;
} wb_ctx;

/* Chemin d'écriture réel (sous g_lock) : positionnement, ext4_fwrite (une transaction), wr_err. Volume
 * arrêté ou gelé : rien n'est écrit, FL_EIO. */
static int wb_write(void *ctx, const void *p, size_t len, uint64_t off) {
  wb_ctx *c = ctx;
  size_t done = 0;
  int r = stopped(c->v) ? EIO : ext4_fseek(&c->h->f, (int64_t)off, SEEK_SET);
  if (r == EOK) r = ext4_fwrite(&c->h->f, p, len, &done);
  int rc = wr_err(c->v, r);
  if (rc == FL_OK && done != len) rc = FL_EIO;
  return rc;
}

/* Vide le tampon de h (sous g_lock) ; une erreur reste mémorisée dans h->wb.err. */
static int wb_flush(ext4_vol *v, ext4_fh *h) {
  wb_ctx c = {v, h};
  if (v->wb_fh == h) v->wb_fh = NULL;
  return fl_wbuf_flush(&h->wb, wb_write, &c);
}

/* Vide le tampon du volume (au plus un), au début de toute opération autre qu'une écriture qui le
 * prolonge : lectures (d'un autre descripteur aussi), stat, statfs, readdir, ouverture, opérations de
 * dossier, sync, fermeture, démontage voient ainsi les données écrites. L'erreur d'un vidage reste sur
 * son fichier : l'opération en cours ne la rend pas (elle échoue d'elle-même si le volume est gelé). Sous
 * g_lock. */
static void flush_all(ext4_vol *v) {
  if (v->wb_fh) (void)wb_flush(v, v->wb_fh);
}

/* Erreur différée d'un fichier ouvert (sous g_lock), FL_OK sinon. */
static int pending_err(const ext4_vol *v) {
  for (const ext4_fh *h = v->files; h; h = h->next)
    if (h->wb.err != FL_OK) return h->wb.err;
  return FL_OK;
}

/* Près du disque plein, écriture directe (sous g_lock) : FL_ENOSPC est alors rendu par l'écriture qui ne
 * tient pas, comme sans tampon, et non par un vidage différé qui perdrait des données déjà acquittées (le
 * client ne voit pas l'erreur d'une fermeture, onRelease). Marge : deux fois les données en jeu
 * (métadonnées largement comprises) plus un tampon. */
static bool near_full(ext4_vol *v, uint64_t pending) {
  struct ext4_mount_stats st;
  if (ext4_mount_point_stats(v->mp, &st) != EOK) return true;
  uint64_t libres = st.free_blocks_count < st.blocks_count ? st.free_blocks_count : st.blocks_count;
  return libres * st.block_size < 2 * pending + FL_WBUF_CAP; /* ≤ taille du disque ; pending < 2^45 */
}

/* ---------- opérations ---------- */

static int e_statfs(fl_fs *fs, fl_statfs *out) {
  ext4_vol *v = fs->priv;
  struct ext4_mount_stats st;
  pthread_mutex_lock(&g_lock);
  flush_all(v);
  int r = ext4_mount_point_stats(v->mp, &st);
  int rc = r != EOK ? rd_err(v, r) : FL_OK; /* sticky lit et pose l'état du volume : sous g_lock */
  pthread_mutex_unlock(&g_lock);
  if (rc != FL_OK) return rc;
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
  flush_all(v); /* taille et contenu à jour */
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
  flush_all(v);
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
    flush_all(v); /* le rappel a pu écrire */
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
  flush_all(v);
  int rc = inode_of(v, path, &in);
  bool creer = false, ouvert = false;
  if (rc == FL_OK) {
    uint32_t t = inode_type(&in);
    if (t == EXT4_INODE_MODE_DIRECTORY) rc = FL_EISDIR;
    else if (t != EXT4_INODE_MODE_FILE) rc = FL_ENOTSUP; /* pas de suivi de lien */
    /* Fichier à blocs indirects (volume converti depuis ext3) : ext4_fwrite le refuse (ENOTSUP). Refus
     * dès l'ouverture en écriture, avant toute troncature : « w » viderait le fichier puis
     * n'écrirait rien. */
    else if ((flags & (FL_O_RDWR | FL_O_TRUNC)) && !(to_le32(in.flags) & EXT4_INODE_FLAG_EXTENTS))
      rc = FL_ENOTSUP;
  } else if (rc == FL_ENOENT && (flags & FL_O_CREAT)) {
    rc = parent_ok(v, path);
    creer = rc == FL_OK;
  }
  if (rc == FL_OK && (creer || (flags & FL_O_TRUNC)) && stopped(v)) rc = FL_EIO;
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
  flush_all(v); /* ce descripteur ou un autre du même fichier : les données écrites sont lues */
  int r = ext4_fseek(&h->f, (int64_t)off, SEEK_SET);
  if (r == EOK) r = ext4_fread(&h->f, buf, len, &got); /* rien au-delà de la fin */
  int rc = r != EOK ? rd_err(v, r) : FL_OK;
  pthread_mutex_unlock(&g_lock);
  if (rc != FL_OK) return rc;
  return (int64_t)got;
}

/* Écriture différée (wbuf.c) : les bornes, le volume arrêté ou gelé et l'erreur différée du fichier
 * sont vérifiés tout de suite ; puis les données vont dans le tampon du fichier (len rendu), vidé par le
 * chemin d'écriture réel (wb_write) quand l'écriture ne le prolonge pas ou n'y tient pas. Un vidage en
 * échec rend son erreur, qui reste collante. Le tampon d'un autre fichier est vidé d'abord (un seul par
 * volume : 4 Mio au plus) ; son erreur reste sur son fichier. Une écriture directe (≥ 4 Mio, mémoire
 * refusée, disque presque plein) qui échoue rend son erreur sans la rendre collante : tout ou rien vu de
 * l'appelant, le fichier garde ce qui a été écrit, taille comprise. */
static int64_t e_pwrite(fl_file *f, const void *buf, size_t len, uint64_t off) {
  ext4_vol *v = f->fs->priv;
  ext4_fh *h = f->priv;
  if (len == 0) return 0;
  if (off > v->max_write || len > v->max_write - off || len > INT64_MAX) return FL_EINVAL;
  pthread_mutex_lock(&g_lock);
  h->written = true;
  int rc = h->wb.err;
  if (rc == FL_OK && v->wb_fh && v->wb_fh != h) flush_all(v);
  if (rc == FL_OK && stopped(v)) rc = wr_err(v, EIO);
  if (rc == FL_OK) {
    wb_ctx c = {v, h};
    if (near_full(v, (uint64_t)h->wb.len + len)) {
      rc = wb_flush(v, h);
      if (rc == FL_OK) rc = wb_write(&c, buf, len, off);
    } else {
      rc = fl_wbuf_write(&h->wb, buf, len, off, wb_write, &c);
    }
    v->wb_fh = h->wb.len ? h : NULL;
  }
  pthread_mutex_unlock(&g_lock);
  if (rc != FL_OK) return rc;
  return (int64_t)len;
}

static int e_truncate(fl_file *f, uint64_t size) {
  ext4_vol *v = f->fs->priv;
  ext4_fh *h = f->priv;
  if (size > v->max_write) return FL_EINVAL;
  pthread_mutex_lock(&g_lock);
  flush_all(v);
  h->written = true;
  int rc = h->wb.err != FL_OK ? h->wb.err : stopped(v) ? FL_EIO : wr_err(v, (ext4_ftruncate(&h->f, size)));
  pthread_mutex_unlock(&g_lock);
  return rc;
}

/* § 4.2 : sync à chaque fermeture d'un fichier ouvert en écriture (écrit ou tronqué), après le vidage
 * de son tampon. L'erreur différée du fichier l'emporte : des données acquittées ont été perdues. */
static int e_close(fl_file *f) {
  ext4_vol *v = f->fs->priv;
  ext4_fh *h = f->priv;
  int rc = FL_OK;
  pthread_mutex_lock(&g_lock);
  flush_all(v);
  int differee = h->wb.err;
  fl_wbuf_free(&h->wb);
  ext4_fclose(&h->f);
  for (ext4_fh **p = &v->files; *p; p = &(*p)->next)
    if (*p == h) {
      *p = h->next;
      break;
    }
  v->open_files--;
  if (h->written) rc = wr_fl(v, stopped(v) ? FL_EIO : sync_locked(v));
  if (differee != FL_OK) rc = differee;
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
  flush_all(v);
  int rc = inode_of(v, path, &in);
  if (rc == FL_OK) rc = FL_EEXIST;
  else if (rc == FL_ENOENT) {
    rc = parent_ok(v, path);
    if (rc == FL_OK && stopped(v)) rc = FL_EIO;
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
  flush_all(v);
  int rc = inode_ino_of(v, path, &in, &ino);
  if (rc == FL_OK && inode_type(&in) == EXT4_INODE_MODE_DIRECTORY) rc = FL_EISDIR;
  /* lwext4 libérerait l'inode sous le descripteur ouvert */
  if (rc == FL_OK && ino_open(v, ino)) rc = FL_EBUSY;
  if (rc == FL_OK && stopped(v)) rc = FL_EIO;
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
  bool steps_out = true; /* boucle terminée sans fin de dossier ni entrée */
  for (uint32_t steps = 0; steps < MAX_DIR_STEPS; steps++) {
    const ext4_direntry *de = NULL;
    int r = ext4_dir_entry_get(&d, &de);
    if (r != EOK) {
      rc = rd_err(v, r);
      steps_out = false;
      break;
    }
    if (!de) {
      steps_out = false;
      break;
    }
    size_t n = de->name_length;
    if (de->inode == 0 || (n == 1 && de->name[0] == '.') || (n == 2 && de->name[0] == '.' && de->name[1] == '.'))
      continue;
    *empty = false; /* toute autre entrée, même inadressable : non vide (comme lwext4) */
    steps_out = false;
    break;
  }
  /* Disque hostile : des entrées libres sans fin ne prouvent pas que le dossier est vide (ext4_dir_rm,
   * récursif, supprimerait ce qui suit) */
  if (rc == FL_OK && *empty && steps_out) rc = FL_ECORRUPT;
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
  flush_all(v);
  int rc = inode_of(v, path, &in);
  if (rc == FL_OK && inode_type(&in) != EXT4_INODE_MODE_DIRECTORY) rc = FL_ENOTDIR;
  if (rc == FL_OK) rc = dir_empty(v, full, &empty);
  if (rc == FL_OK && !empty) rc = FL_ENOTEMPTY;
  /* vide : ext4_dir_rm (récursif) ne supprime que lui */
  if (rc == FL_OK && stopped(v)) rc = FL_EIO;
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
  flush_all(v);
  int rc = inode_of(v, from, &in);
  if (rc == FL_OK) {
    rc = inode_of(v, to, &in);
    if (rc == FL_OK) rc = FL_EEXIST; /* pas de remplacement */
    else if (rc == FL_ENOENT) rc = parent_ok(v, to);
  }
  if (rc == FL_OK && stopped(v)) rc = FL_EIO;
  if (rc == FL_OK) rc = wr_err(v, (ext4_frename(ffrom, fto)));
  pthread_mutex_unlock(&g_lock);
  return rc;
}

/* Vide le tampon, puis les caches ; rend aussi l'erreur différée d'un fichier encore ouvert (onFsync, sync
 * de l'éjection et sync périodique : jamais « réussi » quand des données acquittées ont été perdues). */
static int e_sync(fl_fs *fs) {
  ext4_vol *v = fs->priv;
  pthread_mutex_lock(&g_lock);
  flush_all(v);
  int rc = stopped(v) ? FL_EIO : wr_fl(v, sync_locked(v));
  if (rc == FL_OK) rc = pending_err(v);
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
  /* sans fichier ouvert, aucun tampon (vidé à la fermeture) ; gelé : rien n'est écrit (wb_write) */
  flush_all(v);
  /* Disque mort : tout est libéré quand même (correctif 0004 : ext4_umount libère le point de
   * montage sur erreur), et la première erreur est rendue. */
  int rc = v->rw ? map_err(ext4_journal_stop(v->mp)) : FL_OK;
  int r = map_err(ext4_umount(v->mp));
  if (rc == FL_OK) rc = r;
  ext4_device_unregister(v->dev_name);
  if (v->rw && rc == FL_OK) rc = stopped(v) ? FL_EIO : fl_bdev_flush(v->dev);
  if (stopped(v) && rc == FL_OK) rc = FL_EIO; /* gelé : journal laissé à rejouer */
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
  if (rc == FL_OK && want_rw && !permis && !strcmp(raison, FL_RO_CSUM)) fl_ext4_policy_log(dev, v->sb);
  if (rc != FL_OK) {
    vol_free(fs, v);
    return rc;
  }
  v->dev = dev;
  v->fs = fs;
  v->block_size = sbi.block_size;
  v->inodes_count = sbi.inodes_count;
  v->max_bytes = (uint64_t)sbi.block_size << 32;
  v->max_write = (uint64_t)sbi.block_size * UINT32_MAX;
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
