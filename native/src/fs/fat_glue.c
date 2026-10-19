/* FAT12, FAT16, FAT32 et exFAT par FatFs R0.16 (ChaN), branchés sur fl_bdev (phase 4).
 *
 * Fils d'exécution : FatFs garde des variables globales (table des volumes, compteur d'identifiants) et
 * n'est pas réentrant (FF_FS_REENTRANT 0). Tout appel à FatFs passe donc sous le mutex global g_fat_lock,
 * qui protège aussi la table des emplacements (g_slot) et l'état de chaque volume. Aucun rappel
 * utilisateur n'est appelé sous ce verrou.
 *
 * Chaque montage prend un emplacement FatFs libre n (0..FF_VOLUMES-1), désigné par le préfixe de chemin
 * « n: » ; fat_diskio.c rattache le fl_bdev au disque physique n. L'emplacement est libéré au démontage
 * et sur tout échec de montage.
 *
 * Politique d'écriture (§ 4.2 de la spec), décidée au montage, volume lisible dans tous les cas :
 * somme du secteur de démarrage exFAT fausse (FL_RO_CSUM), secteur du système de fichiers plus petit que
 * celui du disque (FL_RO_SECTOR), volume « sale » (FL_RO_DIRTY) ou erreurs de support signalées
 * (FL_RO_MEDIA). FatFs ne regarde aucun de ces drapeaux. */

#define _POSIX_C_SOURCE 200809L /* localtime_r */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ff.h"     /* avant diskio.h, qui en utilise les types */
#include "diskio.h"
#include "fluluks/error.h"
#include "fs/fat_diskio.h"
#include "fs/fs_internal.h"
#include "fs/ro_reasons.h"

static pthread_mutex_t g_fat_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_slot[FF_VOLUMES]; /* emplacements FatFs pris (g_fat_lock) */

typedef struct {
  fl_fs *fs;
  FATFS ff;
  fat_disk *disk;
  uint8_t slot;
  char drv[4];  /* "n:" */
  uint32_t ss;  /* taille de secteur du système de fichiers */
  uint32_t open_files; /* fichiers ouverts + parcours de dossier en cours (g_fat_lock) */
} fat_vol;

/* --- outils ---------------------------------------------------------------------------------- */

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int fr_err(FRESULT r) {
  switch (r) {
    case FR_OK: return FL_OK;
    case FR_NO_FILE:
    case FR_NO_PATH: return FL_ENOENT;
    case FR_INVALID_NAME: return FL_EINVAL;
    case FR_EXIST: return FL_EEXIST;
    case FR_WRITE_PROTECTED: return FL_EROFS;
    case FR_INT_ERR: return FL_ECORRUPT;
    case FR_NO_FILESYSTEM: return FL_ECORRUPT;
    case FR_NOT_ENOUGH_CORE: return FL_ENOMEM;
    default: return FL_EIO;
  }
}

/* Heure des horodatages FAT : heure locale du téléphone, bornée à 1980..2107 (appelé par FatFs, sous
 * g_fat_lock). */
DWORD get_fattime(void) {
  time_t t = (time_t)fl_fs_now();
  struct tm lt;
  if (!localtime_r(&t, &lt)) return (DWORD)(1u << 21 | 1u << 16); /* 1980-01-01 */
  int an = lt.tm_year + 1900;
  if (an < 1980) return (DWORD)(1u << 21 | 1u << 16);
  if (an > 2107) return (DWORD)(127u << 25 | 12u << 21 | 31u << 16 | 23u << 11 | 59u << 5 | 29u);
  return (DWORD)((unsigned)(an - 1980) << 25 | (unsigned)(lt.tm_mon + 1) << 21 | (unsigned)lt.tm_mday << 16 |
                 (unsigned)lt.tm_hour << 11 | (unsigned)lt.tm_min << 5 | (unsigned)lt.tm_sec / 2);
}

/* --- politique de lecture seule ---------------------------------------------------------------- */

/* Somme du secteur de démarrage exFAT (secteurs 0 à 10, hors VolumeFlags et PercentInUse) comparée au
 * secteur 11. */
static int exfat_sum_ok(fl_bdev *dev, uint32_t ss, bool *ok) {
  uint8_t *b = malloc((size_t)12 * ss);
  if (!b) return FL_ENOMEM;
  int rc = fl_bdev_read_bytes(dev, 0, (size_t)12 * ss, b);
  if (rc == FL_OK) {
    uint32_t s = 0;
    for (size_t i = 0; i < (size_t)11 * ss; i++) {
      if (i == 106 || i == 107 || i == 112) continue;
      s = ((s & 1) ? 0x80000000u : 0) + (s >> 1) + b[i];
    }
    *ok = true;
    for (size_t k = 0; k < ss; k += 4)
      if (le32(b + (size_t)11 * ss + k) != s) *ok = false;
  }
  free(b);
  return rc;
}

/* Raison de refuser l'écriture (NULL : écriture permise). Lit les drapeaux d'état que FatFs ignore. */
static int write_policy(fat_vol *v, fl_bdev *dev, const char **why) {
  *why = NULL;
  uint8_t bs[512];
  int rc = fl_bdev_read_bytes(dev, 0, sizeof bs, bs);
  if (rc != FL_OK) return rc;
  if (v->ff.fs_type == FS_EXFAT) {
    bool ok = false;
    if ((rc = exfat_sum_ok(dev, v->ss, &ok)) != FL_OK) return rc;
    uint16_t flags = le16(bs + 106);
    if (!ok) *why = FL_RO_CSUM;
    else if (v->ss < dev->sector_size) *why = FL_RO_SECTOR;
    else if (flags & 0x0002) *why = FL_RO_DIRTY;
    else if (flags & 0x0004) *why = FL_RO_MEDIA;
    return FL_OK;
  }
  if (v->ss < dev->sector_size) {
    *why = FL_RO_SECTOR;
    return FL_OK;
  }
  if (v->ff.fs_type == FS_FAT12) return FL_OK; /* pas de bits d'état */
  /* FAT[1] : bit « arrêt propre » (15 ou 27) et bit « pas d'erreur de support » (14 ou 26) */
  bool f32 = v->ff.fs_type == FS_FAT32;
  uint8_t e[4];
  rc = fl_bdev_read_bytes(dev, (uint64_t)v->ff.fatbase * v->ss + (f32 ? 4 : 2), f32 ? 4 : 2, e);
  if (rc != FL_OK) return rc;
  uint32_t fat1 = f32 ? le32(e) : le16(e);
  uint32_t propre = f32 ? 1u << 27 : 1u << 15, sain = f32 ? 1u << 26 : 1u << 14;
  if (bs[f32 ? 0x41 : 0x25] & 0x01 || !(fat1 & propre)) *why = FL_RO_DIRTY;
  else if (!(fat1 & sain)) *why = FL_RO_MEDIA;
  return FL_OK;
}

/* --- opérations -------------------------------------------------------------------------------- */

static int f_statfs(fl_fs *fs, fl_statfs *out) {
  fat_vol *v = fs->priv;
  pthread_mutex_lock(&g_fat_lock);
  DWORD nfree = 0;
  FATFS *pf = NULL;
  FRESULT r = f_getfree(v->drv, &nfree, &pf);
  char label[64] = "";
  if (r == FR_OK) r = f_getlabel(v->drv, label, NULL);
  uint64_t clsz = (uint64_t)v->ff.csize * v->ss;
  out->total_bytes = (uint64_t)(v->ff.n_fatent - 2) * clsz;
  out->free_bytes = (uint64_t)nfree * clsz;
  pthread_mutex_unlock(&g_fat_lock);
  if (r != FR_OK) return fr_err(r);
  size_t n = strlen(label);
  while (n > 0 && label[n - 1] == ' ') label[--n] = 0;
  snprintf(out->label, sizeof out->label, "%s", label);
  out->needs_recovery = false;
  return FL_OK;
}

static int f_stat_op(fl_fs *fs, const char *path, fl_stat *out) {
  (void)fs, (void)path, (void)out;
  return FL_ENOTSUP;
}

static int f_readdir_op(fl_fs *fs, const char *path, fl_readdir_cb cb, void *ctx) {
  (void)fs, (void)path, (void)cb, (void)ctx;
  return FL_ENOTSUP;
}

static int f_open_op(fl_fs *fs, const char *path, int flags, fl_file **out) {
  (void)fs, (void)path, (void)flags, (void)out;
  return FL_ENOTSUP;
}

static int64_t f_pread_op(fl_file *f, void *buf, size_t len, uint64_t off) {
  (void)f, (void)buf, (void)len, (void)off;
  return FL_ENOTSUP;
}

static int f_close_op(fl_file *f) {
  free(f);
  return FL_OK;
}

/* Libère l'emplacement et le volume (sous g_fat_lock). */
static void release(fat_vol *v) {
  f_mount(NULL, v->drv, 0);
  fat_disk_detach(v->disk);
  g_slot[v->slot] = false;
}

static int f_unmount_op(fl_fs *fs) {
  fat_vol *v = fs->priv;
  pthread_mutex_lock(&g_fat_lock);
  if (v->open_files) {
    pthread_mutex_unlock(&g_fat_lock);
    return FL_EINVAL;
  }
  release(v);
  pthread_mutex_unlock(&g_fat_lock);
  free(v);
  free(fs);
  return FL_OK;
}

static const fl_fs_ops k_fat_ops = {
    .statfs = f_statfs,
    .stat = f_stat_op,
    .readdir = f_readdir_op,
    .open = f_open_op,
    .pread = f_pread_op,
    .close = f_close_op,
    .unmount = f_unmount_op,
};

int fl_fat_mount(fl_bdev *dev, bool want_rw, fl_fs **out) {
  *out = NULL;
  uint8_t bs[512];
  int rc = fl_bdev_read_bytes(dev, 0, sizeof bs, bs);
  if (rc != FL_OK) return rc;
  uint32_t ss = !memcmp(bs + 3, "EXFAT   ", 8) ? (bs[108] >= 9 && bs[108] <= 12 ? 1u << bs[108] : 0) : le16(bs + 11);
  if (ss < 512 || ss > 4096 || (ss & (ss - 1))) return FL_ECORRUPT;

  fl_fs *fs = calloc(1, sizeof *fs);
  fat_vol *v = calloc(1, sizeof *v);
  if (!fs || !v) {
    free(fs);
    free(v);
    return FL_ENOMEM;
  }
  fs->ops = &k_fat_ops;
  fs->dev = dev;
  fs->priv = v;
  v->fs = fs;
  v->ss = ss;

  pthread_mutex_lock(&g_fat_lock);
  int slot = -1;
  for (int i = 0; i < FF_VOLUMES && slot < 0; i++)
    if (!g_slot[i]) slot = i;
  if (slot < 0) {
    pthread_mutex_unlock(&g_fat_lock);
    free(v);
    free(fs);
    return FL_EBUSY;
  }
  v->slot = (uint8_t)slot;
  snprintf(v->drv, sizeof v->drv, "%d:", slot);
  rc = fat_disk_attach(v->slot, dev, ss, want_rw, &v->disk);
  if (rc == FL_OK) {
    g_slot[slot] = true;
    FRESULT r = f_mount(&v->ff, v->drv, 1);
    rc = r == FR_OK ? FL_OK : r == FR_DISK_ERR ? FL_EIO : r == FR_NOT_ENOUGH_CORE ? FL_ENOMEM : FL_ECORRUPT;
    const char *why = NULL;
    if (rc == FL_OK && want_rw) rc = write_policy(v, dev, &why);
    if (rc == FL_OK && why) {
      fat_disk_set_writable(v->disk, false);
      fs->read_only = true;
      snprintf(fs->ro_reason, sizeof fs->ro_reason, "%s", why);
    }
    if (rc != FL_OK) release(v);
  }
  pthread_mutex_unlock(&g_fat_lock);
  if (rc != FL_OK) {
    free(v);
    free(fs);
    return rc;
  }
  *out = fs;
  return FL_OK;
}
