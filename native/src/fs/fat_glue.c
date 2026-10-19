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
 * (FL_RO_MEDIA). FatFs ne regarde aucun de ces drapeaux.
 *
 * Écriture : FatFs ne gère pas non plus le drapeau « sale » ; la colle le pose dans le secteur de
 * démarrage (octet 0x25 ou 0x41 bit 0 en FAT16/32, VolumeFlags bit 1 en exFAT ; rien en FAT12) avant la
 * première modification d'une session propre, vide le cache du disque, et l'efface à la fin de chaque sync
 * réussi et au démontage. Le secteur 0 est écrit directement par fl_bdev : FatFs ne l'écrit jamais en
 * FAT, et le relit lui-même avant d'y écrire PercentInUse en exFAT ; la fenêtre de FatFs est invalidée si
 * elle le contenait. FAT[1], que FatFs garde dans sa fenêtre, n'est jamais touché. FatFs laisse indéfini
 * le contenu d'un fichier agrandi par f_lseek : la colle écrit des zéros. Une erreur d'écriture du disque
 * rend le volume collant en lecture seule (FL_RO_IOERR), une incohérence vue par FatFs aussi
 * (FL_RO_CORRUPT). */

#define _POSIX_C_SOURCE 200809L /* localtime_r */

#include <pthread.h>
#include <stdatomic.h>
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

/* Fichier ouvert : un seul FIL par fichier, partagé par toutes ses poignées (FatFs interdit deux FIL en
 * écriture sur un même fichier, et un lecteur à part ne verrait ni la taille ni le tampon de l'écrivain).
 * Clé = emplacement de l'entrée de dossier : (secteur, offset dans le secteur) en FAT, (cluster du dossier,
 * offset dans le dossier) en exFAT. Elle ne change pas tant que le fichier est ouvert : renommer ou
 * supprimer un fichier ouvert est refusé (FL_EBUSY). */
typedef struct fat_node {
  struct fat_node *next;
  uint64_t key_a, key_b;
  FIL fil;
  uint32_t refs;      /* poignées */
  uint32_t writers;   /* poignées ouvertes en écriture */
  DWORD *cltbl;       /* table de clusters (recherche rapide) d'un noeud sans écrivain, ou NULL */
  bool cltbl_tried;
  bool modified;      /* écrit ou tronqué depuis le dernier f_sync */
} fat_node;

/* Poignée : fl_file.priv. */
typedef struct {
  fat_node *n;
  bool writer;  /* ouverte avec FL_O_RDWR */
  bool wrote;   /* a écrit ou tronqué */
} fat_fh;

typedef struct {
  fl_fs *fs;
  FATFS ff;
  fat_disk *disk;
  uint8_t slot;
  char drv[4];  /* "n:" */
  uint32_t ss;  /* taille de secteur du système de fichiers */
  uint32_t open_files; /* fichiers ouverts + parcours de dossier en cours (g_fat_lock) */
  fat_node *nodes;     /* fichiers ouverts (g_fat_lock) */
  bool dirty;          /* drapeau « sale » posé sur le disque par cette session (g_fat_lock) */
} fat_vol;

#define FULL_CAP (4 + 4096) /* "n:" + chemin validé (≤ 4095 octets) + NUL */
#define READDIR_BATCH 64

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

/* Chemin FatFs « n:/… » d'un chemin validé. */
static void full_path(const fat_vol *v, const char *path, char out[FULL_CAP]) {
  snprintf(out, FULL_CAP, "%s%s", v->drv, path);
}

/* Date et heure FAT (heure locale) en secondes Unix ; 0 si la date est invalide. */
static int64_t fat_time(WORD d, WORD t) {
  struct tm lt = {0};
  lt.tm_year = 80 + (d >> 9);
  lt.tm_mon = ((d >> 5) & 15) - 1;
  lt.tm_mday = d & 31;
  lt.tm_hour = t >> 11;
  lt.tm_min = (t >> 5) & 63;
  lt.tm_sec = (t & 31) * 2;
  lt.tm_isdst = -1;
  if (lt.tm_mon < 0 || lt.tm_mon > 11 || lt.tm_mday == 0) return 0;
  time_t r = mktime(&lt);
  return r == (time_t)-1 ? 0 : (int64_t)r;
}

static void fill_stat(const FILINFO *fi, fl_stat *out) {
  memset(out, 0, sizeof *out);
  bool dir = fi->fattrib & AM_DIR;
  out->type = dir ? FL_FT_DIR : FL_FT_FILE;
  out->size = dir ? 0 : (uint64_t)fi->fsize;
  out->mtime = fat_time(fi->fdate, fi->ftime);
  out->mode = dir ? 040755 : (fi->fattrib & AM_RDO) ? 0100444 : 0100644;
}

static void root_stat(fl_stat *out) {
  memset(out, 0, sizeof *out);
  out->type = FL_FT_DIR;
  out->mode = 040755;
}

/* FR_NO_PATH : un composant intermédiaire manque (FL_ENOENT) ou n'est pas un dossier (FL_ENOTDIR). Sous
 * g_fat_lock. */
static int nopath_err(const fat_vol *v, const char *path) {
  char p[FULL_CAP];
  size_t base = strlen(v->drv);
  full_path(v, path, p);
  for (char *c = p + base + 1; (c = strchr(c, '/')) != NULL; c++) {
    *c = 0;
    FILINFO fi;
    FRESULT r = f_stat(p, &fi);
    *c = '/';
    if (r != FR_OK) return FL_ENOENT;
    if (!(fi.fattrib & AM_DIR)) return FL_ENOTDIR;
  }
  return FL_ENOENT;
}

/* f_stat d'un chemin validé, racine comprise (sous g_fat_lock). */
static int stat_locked(const fat_vol *v, const char *path, FILINFO *fi, bool *root) {
  *root = !strcmp(path, "/");
  if (strchr(path, '\\')) return FL_ENOENT; /* séparateur pour FatFs, interdit dans un nom FAT */
  if (*root) {
    memset(fi, 0, sizeof *fi);
    fi->fattrib = AM_DIR;
    return FL_OK;
  }
  char p[FULL_CAP];
  full_path(v, path, p);
  FRESULT r = f_stat(p, fi);
  if (r == FR_NO_PATH) return nopath_err(v, path);
  return fr_err(r);
}

static int f_stat_op(fl_fs *fs, const char *path, fl_stat *out) {
  fat_vol *v = fs->priv;
  FILINFO fi;
  bool root;
  pthread_mutex_lock(&g_fat_lock);
  int rc = stat_locked(v, path, &fi, &root);
  pthread_mutex_unlock(&g_fat_lock);
  if (rc != FL_OK) return rc;
  if (root) root_stat(out);
  else fill_stat(&fi, out);
  return FL_OK;
}

static int f_readdir_op(fl_fs *fs, const char *path, fl_readdir_cb cb, void *ctx) {
  fat_vol *v = fs->priv;
  DIR *dir = malloc(sizeof *dir);
  fl_dirent *batch = malloc(READDIR_BATCH * sizeof *batch);
  FILINFO *fi = malloc(sizeof *fi);
  if (!dir || !batch || !fi) {
    free(dir);
    free(batch);
    free(fi);
    return FL_ENOMEM;
  }
  char p[FULL_CAP];
  full_path(v, path, p);
  pthread_mutex_lock(&g_fat_lock);
  bool root;
  int rc = stat_locked(v, path, fi, &root);
  if (rc == FL_OK && !(fi->fattrib & AM_DIR)) rc = FL_ENOTDIR;
  if (rc == FL_OK) rc = fr_err(f_opendir(dir, p));
  if (rc == FL_OK) v->open_files++;
  pthread_mutex_unlock(&g_fat_lock);
  bool opened = rc == FL_OK, fin = false;
  while (rc == FL_OK && !fin) {
    size_t n = 0;
    pthread_mutex_lock(&g_fat_lock);
    while (n < READDIR_BATCH) {
      FRESULT r = f_readdir(dir, fi);
      if (r != FR_OK) {
        rc = fr_err(r);
        break;
      }
      if (!fi->fname[0]) {
        fin = true;
        break;
      }
      if (!strcmp(fi->fname, ".") || !strcmp(fi->fname, "..")) continue;
      snprintf(batch[n].name, sizeof batch[n].name, "%s", fi->fname);
      fill_stat(fi, &batch[n].st);
      n++;
    }
    pthread_mutex_unlock(&g_fat_lock);
    for (size_t i = 0; rc == FL_OK && i < n; i++) {
      int r = cb(ctx, &batch[i]);
      if (r != 0) {
        rc = r;
        fin = true;
      }
    }
  }
  if (opened) {
    pthread_mutex_lock(&g_fat_lock);
    f_closedir(dir);
    v->open_files--;
    pthread_mutex_unlock(&g_fat_lock);
  }
  free(dir);
  free(batch);
  free(fi);
  return rc;
}

/* Clé d'un fichier ouvert par f_open (voir fat_node). */
static void node_key(const fat_vol *v, const FIL *f, uint64_t *a, uint64_t *b) {
  if (v->ff.fs_type == FS_EXFAT) {
    *a = f->obj.c_scl;
    *b = f->obj.c_ofs;
  } else {
    *a = (uint64_t)f->dir_sect;
    *b = (uint64_t)(f->dir_ptr - v->ff.win);
  }
}

static fat_node *find_node(const fat_vol *v, uint64_t a, uint64_t b) {
  for (fat_node *n = v->nodes; n; n = n->next)
    if (n->key_a == a && n->key_b == b) return n;
  return NULL;
}

/* Ouvre (ou rejoint) le noeud du fichier ordinaire `path` (sous g_fat_lock). */
static int node_get(fat_vol *v, const char *path, fat_node **out) {
  FILINFO fi;
  bool root;
  int rc = stat_locked(v, path, &fi, &root);
  if (rc != FL_OK) return rc;
  if (fi.fattrib & AM_DIR) return FL_EISDIR;
  fat_node *n = calloc(1, sizeof *n);
  if (!n) return FL_ENOMEM;
  char p[FULL_CAP];
  full_path(v, path, p);
  BYTE mode = FA_READ | (!v->fs->read_only && !(fi.fattrib & AM_RDO) ? FA_WRITE : 0);
  FRESULT r = f_open(&n->fil, p, mode);
  if (r != FR_OK) {
    free(n);
    return r == FR_NO_PATH ? nopath_err(v, path) : fr_err(r);
  }
  node_key(v, &n->fil, &n->key_a, &n->key_b);
  fat_node *e = find_node(v, n->key_a, n->key_b);
  if (e) {
    f_close(&n->fil); /* jamais modifié : rien n'est écrit */
    free(n);
    n = e;
  } else {
    n->next = v->nodes;
    v->nodes = n;
  }
  n->refs++;
  *out = n;
  return FL_OK;
}

/* Lâche une poignée du noeud ; le ferme à la dernière (sous g_fat_lock). Rend l'erreur de f_close. */
static int node_put(fat_vol *v, fat_node *n) {
  if (--n->refs > 0) return FL_OK;
  for (fat_node **pp = &v->nodes; *pp; pp = &(*pp)->next)
    if (*pp == n) {
      *pp = n->next;
      break;
    }
  n->fil.cltbl = NULL;
  int rc = fr_err(f_close(&n->fil));
  free(n->cltbl);
  free(n);
  return rc;
}

/* --- écriture -------------------------------------------------------------------------------- */

static int name_ok(const char *path);

/* Lecture seule collante (sous g_fat_lock). */
static void go_ro(fat_vol *v, const char *why) {
  fat_disk_set_writable(v->disk, false);
  fl_fs_go_read_only(v->fs, why);
  /* Fenêtre de FatFs non écrite : abandonnée (elle ne pourra plus l'être), sinon chaque lecture qui
   * déplace la fenêtre échouerait en voulant l'écrire. La suite relit le disque tel qu'il est. */
  v->ff.wflag = 0;
  v->ff.winsect = (LBA_t)0 - 1;
}

/* Code d'une opération d'écriture de FatFs ; erreur du disque ou incohérence : lecture seule collante
 * (sous g_fat_lock). */
static int wr_err(fat_vol *v, FRESULT r) {
  if (r == FR_DISK_ERR || (r != FR_OK && fat_disk_error(v->disk) != FL_OK)) {
    go_ro(v, FL_RO_IOERR);
    return FL_EIO;
  }
  if (r == FR_INT_ERR) {
    go_ro(v, FL_RO_CORRUPT);
    return FL_ECORRUPT;
  }
  return fr_err(r);
}

/* Pose (sale = vrai) ou efface le drapeau « sale » du secteur de démarrage, puis vide le cache du disque
 * (sous g_fat_lock, fenêtre de FatFs propre). */
static int set_dirty_flag(fat_vol *v, bool sale) {
  if (v->ff.fs_type == FS_FAT12) return FL_OK;
  fl_bdev *dev = v->fs->dev;
  uint8_t *b = malloc(v->ss);
  if (!b) return FL_ENOMEM;
  int rc = fat_disk_drain(v->disk); /* écriture directe : rien ne doit rester dans le tampon */
  if (rc == FL_OK) rc = fl_bdev_read_bytes(dev, 0, v->ss, b);
  if (rc == FL_OK) {
    size_t o = v->ff.fs_type == FS_EXFAT ? 106 : v->ff.fs_type == FS_FAT32 ? 0x41 : 0x25;
    uint8_t bit = v->ff.fs_type == FS_EXFAT ? 0x02 : 0x01;
    b[o] = sale ? (uint8_t)(b[o] | bit) : (uint8_t)(b[o] & ~bit);
    rc = fl_bdev_write(dev, 0, v->ss / dev->sector_size, b);
    if (rc == FL_OK) rc = fl_bdev_flush(dev);
  }
  free(b);
  if (v->ff.winsect == v->ff.volbase && !v->ff.wflag) v->ff.winsect = (LBA_t)0 - 1; /* fenêtre périmée */
  if (rc != FL_OK) go_ro(v, FL_RO_IOERR);
  return rc == FL_OK ? FL_OK : FL_EIO;
}

/* Avant toute modification (sous g_fat_lock). */
static int mark_dirty(fat_vol *v) {
  if (v->dirty) return FL_OK;
  /* Compte de clusters libres exact (FF_FS_NOFSINFO 3 : non lu au montage) : FatFs le tient ensuite à jour
   * et l'écrit au sync (FSInfo en FAT32, PercentInUse en exFAT). */
  DWORD nfree;
  FATFS *pf;
  FRESULT r = f_getfree(v->drv, &nfree, &pf);
  if (r != FR_OK) return wr_err(v, r);
  int rc = set_dirty_flag(v, true);
  if (rc == FL_OK) v->dirty = true;
  return rc;
}

/* Fichiers modifiés écrits, cache vidé, drapeau effacé (sous g_fat_lock). */
static int sync_locked(fat_vol *v) {
  if (v->fs->read_only) return atomic_load(&v->fs->went_ro) ? FL_EIO : FL_OK;
  int rc = FL_OK;
  for (fat_node *n = v->nodes; n; n = n->next) {
    if (!n->modified) continue;
    int r = wr_err(v, f_sync(&n->fil));
    if (r == FL_OK) n->modified = false;
    else if (rc == FL_OK) rc = r;
  }
  if (rc != FL_OK) return rc;
  if (disk_ioctl(v->slot, CTRL_SYNC, NULL) != RES_OK) {
    go_ro(v, FL_RO_IOERR);
    return FL_EIO;
  }
  if (!v->dirty) return FL_OK;
  rc = set_dirty_flag(v, false);
  if (rc == FL_OK) v->dirty = false;
  return rc;
}

/* Écrit des zéros dans n de `from` à `to` (from ≤ to ; sous g_fat_lock). FL_ENOSPC si le disque est plein. */
static int fill_zeros(fat_vol *v, fat_node *n, FSIZE_t from, FSIZE_t to) {
  static const uint8_t zeros[64 * 1024];
  FRESULT r = f_lseek(&n->fil, from);
  while (r == FR_OK && from < to) {
    UINT k = to - from < sizeof zeros ? (UINT)(to - from) : (UINT)sizeof zeros, bw = 0;
    r = f_write(&n->fil, zeros, k, &bw);
    from += bw;
    if (r == FR_OK && bw < k) return FL_ENOSPC;
  }
  return wr_err(v, r);
}

/* Limite de taille d'un fichier : 4 Gio - 1 en FAT, aucune (64 bits) en exFAT. */
static bool too_big(const fat_vol *v, uint64_t end) { return v->ff.fs_type != FS_EXFAT && end > 0xFFFFFFFFull; }

/* Le fichier change : plus de table de clusters (sous g_fat_lock). */
static void node_drop_fastseek(fat_node *n) {
  n->fil.cltbl = NULL;
  free(n->cltbl);
  n->cltbl = NULL;
  n->cltbl_tried = false;
}

static int f_open_op(fl_fs *fs, const char *path, int flags, fl_file **out) {
  fat_vol *v = fs->priv;
  bool rw = flags & FL_O_RDWR;
  fl_file *f = calloc(1, sizeof *f);
  fat_fh *h = calloc(1, sizeof *h);
  if (!f || !h) {
    free(f);
    free(h);
    return FL_ENOMEM;
  }
  pthread_mutex_lock(&g_fat_lock);
  fat_node *n = NULL;
  int rc = node_get(v, path, &n);
  if (rc == FL_ENOENT && (flags & FL_O_CREAT)) rc = name_ok(path) == FL_OK ? FL_ENOENT : FL_EINVAL;
  if (rc == FL_ENOENT && (flags & FL_O_CREAT)) {
    char p[FULL_CAP];
    full_path(v, path, p);
    rc = mark_dirty(v);
    FIL tmp;
    if (rc == FL_OK) {
      FRESULT r = f_open(&tmp, p, FA_CREATE_NEW | FA_READ | FA_WRITE);
      rc = r == FR_NO_PATH ? nopath_err(v, path) : wr_err(v, r);
    }
    if (rc == FL_OK) rc = wr_err(v, f_close(&tmp));
    if (rc == FL_OK) rc = node_get(v, path, &n);
  }
  if (rc == FL_OK && rw && !(n->fil.flag & FA_WRITE)) rc = FL_EROFS; /* attribut lecture seule */
  if (rc == FL_OK && rw) {
    n->writers++;
    h->writer = true;
    node_drop_fastseek(n);
    if ((flags & FL_O_TRUNC) && f_size(&n->fil) > 0) {
      rc = mark_dirty(v);
      if (rc == FL_OK) rc = wr_err(v, f_lseek(&n->fil, 0));
      if (rc == FL_OK) rc = wr_err(v, f_truncate(&n->fil));
      n->modified = true;
      h->wrote = true;
    }
    if (rc != FL_OK) n->writers--;
  }
  if (rc != FL_OK && n) node_put(v, n);
  if (rc == FL_OK) v->open_files++;
  pthread_mutex_unlock(&g_fat_lock);
  if (rc != FL_OK) {
    free(f);
    free(h);
    return rc;
  }
  h->n = n;
  f->fs = fs;
  f->flags = flags;
  f->priv = h;
  *out = f;
  return FL_OK;
}

static int64_t f_pwrite_op(fl_file *f, const void *buf, size_t len, uint64_t off) {
  fat_vol *v = f->fs->priv;
  fat_fh *h = f->priv;
  fat_node *n = h->n;
  if (len == 0) return 0;
  if (off > UINT64_MAX - len || too_big(v, off + len)) return FL_EFBIG;
  pthread_mutex_lock(&g_fat_lock);
  int rc = n->fil.err ? FL_EIO : mark_dirty(v);
  if (rc == FL_OK) {
    h->wrote = true;
    n->modified = true;
    FSIZE_t size = f_size(&n->fil);
    if (off > size) rc = fill_zeros(v, n, size, (FSIZE_t)off);
  }
  size_t done = 0;
  if (rc == FL_OK) rc = wr_err(v, f_lseek(&n->fil, (FSIZE_t)off));
  while (rc == FL_OK && done < len) {
    UINT k = len - done > (1u << 30) ? 1u << 30 : (UINT)(len - done), bw = 0;
    rc = wr_err(v, f_write(&n->fil, (const uint8_t *)buf + done, k, &bw));
    done += bw;
    if (rc == FL_OK && bw < k) rc = FL_ENOSPC;
  }
  pthread_mutex_unlock(&g_fat_lock);
  return rc == FL_OK ? (int64_t)len : rc;
}

static int f_truncate_op(fl_file *f, uint64_t size) {
  fat_vol *v = f->fs->priv;
  fat_fh *h = f->priv;
  fat_node *n = h->n;
  if (too_big(v, size)) return FL_EFBIG;
  pthread_mutex_lock(&g_fat_lock);
  int rc = n->fil.err ? FL_EIO : FL_OK;
  FSIZE_t cur = f_size(&n->fil);
  if (rc == FL_OK && size != cur) {
    rc = mark_dirty(v);
    h->wrote = true;
    n->modified = true;
    if (rc == FL_OK && size > cur) rc = fill_zeros(v, n, cur, (FSIZE_t)size);
    else if (rc == FL_OK) {
      rc = wr_err(v, f_lseek(&n->fil, (FSIZE_t)size));
      if (rc == FL_OK) rc = wr_err(v, f_truncate(&n->fil));
    }
  }
  pthread_mutex_unlock(&g_fat_lock);
  return rc;
}

/* --- dossiers et noms (T5) --------------------------------------------------------------------- */

/* Dernier composant créable sur FAT : FatFs retirerait en silence un « . » ou une espace finale, et refuse
 * : * ? " < > | \ et les caractères de contrôle (vérifié ici, indépendamment de sa version). */
static int name_ok(const char *path) {
  if (strchr(path, '\\')) return FL_EINVAL; /* séparateur pour FatFs, sur tout le chemin */
  const char *n = strrchr(path, '/');
  n = n ? n + 1 : path;
  size_t len = strlen(n);
  if (len == 0 || n[len - 1] == '.' || n[len - 1] == ' ') return FL_EINVAL;
  for (const unsigned char *c = (const unsigned char *)n; *c; c++)
    if (*c < 0x20 || strchr(":*?\"<>|\\", *c)) return FL_EINVAL;
  return FL_OK;
}

/* Vrai si le fichier ordinaire `path` est ouvert (sous g_fat_lock). */
static bool is_open(fat_vol *v, const char *path) {
  if (!v->nodes) return false;
  char p[FULL_CAP];
  full_path(v, path, p);
  FIL tmp;
  if (f_open(&tmp, p, FA_READ) != FR_OK) return false;
  uint64_t a, b;
  node_key(v, &tmp, &a, &b);
  f_close(&tmp);
  return find_node(v, a, b) != NULL;
}

/* Cluster de départ du dossier `path` (0 pour la racine ; sous g_fat_lock). */
static int dir_cluster(const fat_vol *v, const char *path, DWORD *out) {
  char p[FULL_CAP];
  full_path(v, path, p);
  DIR d;
  FRESULT r = f_opendir(&d, p);
  if (r != FR_OK) return fr_err(r);
  *out = d.obj.sclust;
  f_closedir(&d);
  return FL_OK;
}

static int f_mkdir_op(fl_fs *fs, const char *path) {
  fat_vol *v = fs->priv;
  int rc = name_ok(path);
  if (rc != FL_OK) return rc;
  char p[FULL_CAP];
  full_path(v, path, p);
  pthread_mutex_lock(&g_fat_lock);
  rc = mark_dirty(v);
  if (rc == FL_OK) {
    FRESULT r = f_mkdir(p);
    rc = r == FR_NO_PATH ? nopath_err(v, path) : wr_err(v, r);
  }
  pthread_mutex_unlock(&g_fat_lock);
  return rc;
}

static int f_unlink_op(fl_fs *fs, const char *path) {
  fat_vol *v = fs->priv;
  char p[FULL_CAP];
  full_path(v, path, p);
  pthread_mutex_lock(&g_fat_lock);
  FILINFO fi;
  bool root;
  int rc = stat_locked(v, path, &fi, &root);
  if (rc == FL_OK && (root || (fi.fattrib & AM_DIR))) rc = FL_EISDIR; /* f_unlink supprimerait un dossier vide */
  if (rc == FL_OK && is_open(v, path)) rc = FL_EBUSY;
  if (rc == FL_OK) rc = mark_dirty(v);
  if (rc == FL_OK) rc = wr_err(v, f_unlink(p));
  pthread_mutex_unlock(&g_fat_lock);
  return rc;
}

static int f_rmdir_op(fl_fs *fs, const char *path) {
  fat_vol *v = fs->priv;
  if (!strcmp(path, "/")) return FL_EINVAL;
  char p[FULL_CAP];
  full_path(v, path, p);
  pthread_mutex_lock(&g_fat_lock);
  FILINFO fi;
  bool root;
  int rc = stat_locked(v, path, &fi, &root);
  if (rc == FL_OK && !(fi.fattrib & AM_DIR)) rc = FL_ENOTDIR;
  if (rc == FL_OK) {
    DIR d;
    FRESULT r = f_opendir(&d, p);
    while (r == FR_OK) {
      r = f_readdir(&d, &fi);
      if (r != FR_OK || !fi.fname[0]) break;
      if (strcmp(fi.fname, ".") && strcmp(fi.fname, "..")) {
        rc = FL_ENOTEMPTY;
        break;
      }
    }
    if (r == FR_OK) f_closedir(&d);
    else if (rc == FL_OK) rc = fr_err(r);
  }
  if (rc == FL_OK) rc = mark_dirty(v);
  if (rc == FL_OK) rc = wr_err(v, f_unlink(p));
  pthread_mutex_unlock(&g_fat_lock);
  return rc;
}

static int f_rename_op(fl_fs *fs, const char *from, const char *to) {
  fat_vol *v = fs->priv;
  if (!strcmp(from, "/") || !strcmp(to, "/")) return FL_EINVAL;
  int rc = name_ok(to);
  if (rc != FL_OK) return rc;
  char pf[FULL_CAP], pt[FULL_CAP];
  full_path(v, from, pf);
  full_path(v, to, pt);
  pthread_mutex_lock(&g_fat_lock);
  FILINFO fi;
  bool root;
  rc = stat_locked(v, from, &fi, &root);
  if (rc == FL_OK && !(fi.fattrib & AM_DIR) && is_open(v, from)) rc = FL_EBUSY;
  if (rc == FL_OK && (fi.fattrib & AM_DIR)) {
    /* `to` sous `from` : comparaison des clusters de départ des ancêtres de `to` (la casse peut différer),
     * FatFs ne le vérifie pas */
    DWORD cf = 0;
    rc = dir_cluster(v, from, &cf);
    char anc[4096];
    snprintf(anc, sizeof anc, "%s", to);
    for (char *c = strrchr(anc, '/'); rc == FL_OK && c && c > anc; c = strrchr(anc, '/')) {
      *c = 0;
      DWORD ca = 0;
      if (dir_cluster(v, anc, &ca) != FL_OK) break; /* parent absent : f_rename le dira */
      if (ca == cf) rc = FL_EINVAL;
    }
  }
  if (rc == FL_OK) rc = mark_dirty(v);
  if (rc == FL_OK) {
    FRESULT r = f_rename(pf, pt);
    rc = r == FR_NO_PATH ? nopath_err(v, to) : wr_err(v, r);
  }
  pthread_mutex_unlock(&g_fat_lock);
  return rc;
}

static int f_sync_op(fl_fs *fs) {
  fat_vol *v = fs->priv;
  pthread_mutex_lock(&g_fat_lock);
  int rc = sync_locked(v);
  pthread_mutex_unlock(&g_fat_lock);
  return rc;
}

/* Table de clusters d'un noeud sans écrivain, construite une fois (sous g_fat_lock). Sans place, la
 * lecture s'en passe. */
#define CLTBL_WORDS 4096
static void node_fastseek(fat_node *n) {
  if (n->writers || n->cltbl_tried) return;
  n->cltbl_tried = true;
  DWORD *t = malloc(CLTBL_WORDS * sizeof *t);
  if (!t) return;
  t[0] = CLTBL_WORDS;
  n->fil.cltbl = t;
  if (f_lseek(&n->fil, CREATE_LINKMAP) != FR_OK) {
    n->fil.cltbl = NULL;
    free(t);
    return;
  }
  n->cltbl = t;
}

static int64_t f_pread_op(fl_file *f, void *buf, size_t len, uint64_t off) {
  fat_node *n = ((fat_fh *)f->priv)->n;
  pthread_mutex_lock(&g_fat_lock);
  int64_t rc;
  FSIZE_t size = f_size(&n->fil);
  if (n->fil.err) {
    rc = FL_EIO;
  } else if (off >= size) {
    rc = 0;
  } else {
    if (len > size - off) len = (size_t)(size - off);
    if (len > (1u << 30)) len = 1u << 30;
    node_fastseek(n);
    UINT br = 0;
    FRESULT r = f_lseek(&n->fil, (FSIZE_t)off);
    if (r == FR_OK) r = f_read(&n->fil, buf, (UINT)len, &br);
    rc = r == FR_OK ? (int64_t)br : fr_err(r);
  }
  pthread_mutex_unlock(&g_fat_lock);
  return rc;
}

static int f_close_op(fl_file *f) {
  fat_vol *v = f->fs->priv;
  fat_fh *h = f->priv;
  fat_node *n = h->n;
  pthread_mutex_lock(&g_fat_lock);
  int rc = FL_OK;
  if (h->wrote && n->modified && !v->fs->read_only) {
    rc = wr_err(v, f_sync(&n->fil));
    if (rc == FL_OK) n->modified = false;
  }
  if (h->writer && --n->writers == 0) node_drop_fastseek(n);
  int pr = node_put(v, n);
  if (rc == FL_OK && h->wrote) rc = pr != FL_OK ? pr : sync_locked(v);
  v->open_files--;
  pthread_mutex_unlock(&g_fat_lock);
  free(h);
  free(f);
  return rc;
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
  /* Comme ext4 : tout est libéré, et l'éjection ne se dit pas réussie si des données ont pu être perdues
   * (sync final en échec, ou lecture seule prise en cours de session). */
  int rc = !fs->read_only ? sync_locked(v) : atomic_load(&fs->went_ro) ? FL_EIO : FL_OK;
  release(v);
  pthread_mutex_unlock(&g_fat_lock);
  free(v);
  free(fs);
  return rc;
}

static const fl_fs_ops k_fat_ops = {
    .statfs = f_statfs,
    .stat = f_stat_op,
    .readdir = f_readdir_op,
    .open = f_open_op,
    .pread = f_pread_op,
    .close = f_close_op,
    .pwrite = f_pwrite_op,
    .truncate = f_truncate_op,
    .sync = f_sync_op,
    .mkdir = f_mkdir_op,
    .unlink = f_unlink_op,
    .rmdir = f_rmdir_op,
    .rename = f_rename_op,
    .unmount = f_unmount_op,
};

int fl_fat_mount(fl_bdev *dev, bool want_rw, fl_fs **out) {
  *out = NULL;
  uint8_t bs[512];
  int rc = fl_bdev_read_bytes(dev, 0, sizeof bs, bs);
  if (rc != FL_OK) return rc;
  bool exfat = !memcmp(bs + 3, "EXFAT   ", 8);
  /* exFAT : FatFs décale par BytesPerSectorShift et SectorsPerClusterShift sans les borner (décalage
   * indéfini sur un disque hostile) ; la spécification impose 9..12 et une somme d'au plus 25. */
  if (exfat && (bs[108] < 9 || bs[108] > 12 || bs[108] + bs[109] > 25)) return FL_ECORRUPT;
  uint32_t ss = exfat ? 1u << bs[108] : le16(bs + 11);
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
