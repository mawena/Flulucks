/* NTFS par libntfs-3g 2022.10.3, branchée sur fl_bdev (phase 5).
 *
 * Fils d'exécution : libntfs-3g garde des variables globales (conversion des noms, journal) et n'est pas
 * réentrante. Tout appel passe donc sous le mutex global g_ntfs_lock, qui protège aussi l'état de chaque
 * volume. Aucun rappel utilisateur n'est appelé sous ce verrou.
 *
 * Inodes : chaque opération ouvre puis referme ses inodes (ntfs_inode_open / ntfs_inode_close), comme le
 * pilote FUSE de ntfs-3g. Deux copies en mémoire d'un même enregistrement MFT ne coexistent jamais : la
 * dernière fermée écraserait les changements de l'autre. Un fichier ouvert ne garde que son numéro MFT.
 *
 * Politique d'écriture (§ 4.2 de la spec), décidée sur un premier montage en lecture seule, volume lisible
 * dans tous les cas : drapeaux de $Volume (sale, chkdsk en cours… : FL_RO_DIRTY), Windows en veille
 * prolongée ou en démarrage rapide (hiberfil.sys : FL_RO_HIBER), $LogFile non vide (FL_RO_LOGFILE),
 * version autre que 3.0 / 3.1 (FL_RO_VERSION), secteur NTFS plus petit que celui du disque
 * (FL_RO_SECTOR). Si l'écriture est permise, le volume est démonté puis remonté en écriture, sans
 * NTFS_MNT_RECOVER : un $LogFile devenu non vide entre-temps fait échouer ce montage, et la colle retombe
 * en lecture seule (FL_RO_LOGFILE). Monter puis démonter sans rien modifier n'écrit rien.
 *
 * Écriture : libntfs-3g n'a pas de journal. La colle pose VOLUME_IS_DIRTY dans $Volume (et $MFTMirr) puis
 * vide le cache du disque avant la première modification d'une session propre, et ne l'efface qu'à la fin
 * d'un sync réussi (inodes système écrits, cache du disque vidé), puis vide encore : un disque arraché
 * pendant une écriture reste marqué, et Windows le vérifie (chkdsk) au branchement suivant. Une erreur du
 * disque rend le volume collant en lecture seule (FL_RO_IOERR), une incohérence vue par libntfs-3g aussi
 * (FL_RO_CORRUPT) ; plus rien n'est alors écrit, le drapeau reste posé.
 *
 * Noms (comme Windows) : insensibles à la casse à la recherche (nom exact d'abord), casse gardée à la
 * création ; un nom créé ou visé par un renommage qui contient : * ? " < > | \ ou un caractère de contrôle,
 * qui finit par « . » ou une espace, ou qui est réservé (CON, NUL, COM1…) est refusé (FL_EINVAL). Un
 * renommage est un nouveau lien puis le retrait de l'ancien nom (comme ntfs-3g) ; seule la casse change :
 * en passant par un nom temporaire. */

#define _GNU_SOURCE /* struct stat complet (st_mtime) pour les en-têtes de libntfs-3g */

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "fluluks/error.h"
#include "fs/fs_internal.h"
#include "fs/ntfs_dev.h"
#include "fs/ro_reasons.h"
#include "ntfs-3g/attrib.h"
#include "ntfs-3g/cache.h"
#include "ntfs-3g/dir.h"
#include "ntfs-3g/inode.h"
#include "ntfs-3g/logfile.h"
#include "ntfs-3g/mft.h"
#include "ntfs-3g/ntfstime.h"
#include "ntfs-3g/unistr.h"
#include "ntfs-3g/volume.h"

static pthread_mutex_t g_ntfs_lock = PTHREAD_MUTEX_INITIALIZER;

/* Fichier ouvert : un noeud par numéro MFT, partagé par toutes ses poignées (pour refuser de supprimer ou
 * de renommer un fichier ouvert). Aucun inode n'est gardé ouvert entre deux opérations. */
typedef struct ntfs_node {
  struct ntfs_node *next;
  MFT_REF mref;
  uint32_t refs;    /* poignées */
  uint32_t writers; /* poignées ouvertes en écriture */
} ntfs_node;

/* Poignée : fl_file.priv. */
typedef struct {
  ntfs_node *n;
  bool writer; /* ouverte avec FL_O_RDWR */
  bool wrote;  /* a écrit ou tronqué */
} ntfs_fh;

typedef struct {
  fl_fs *fs;
  ntfs_dev *dd;
  ntfs_volume *vol;
  uint32_t open_files; /* fichiers ouverts + parcours de dossier en cours (g_ntfs_lock) */
  ntfs_node *nodes;    /* fichiers ouverts (g_ntfs_lock) */
  bool dirty;          /* VOLUME_IS_DIRTY posé sur le disque par cette session (g_ntfs_lock) */
} ntfs_vol;

#define READDIR_BATCH 64

/* Heure des horodatages de libntfs-3g (correctif 0002) : fl_fs_now(), à la seconde. */
ntfs_time fl_ntfs_now(void) {
  struct timespec ts = {.tv_sec = (time_t)fl_fs_now(), .tv_nsec = 0};
  return timespec2ntfs(ts);
}

/* errno de libntfs-3g → fl_err. */
static int ntfs_err(int e) {
  switch (e) {
    case 0: return FL_EIO;
    case ENOENT: return FL_ENOENT;
    case ENOTDIR: return FL_ENOTDIR;
    case EISDIR: return FL_EISDIR;
    case EEXIST: return FL_EEXIST;
    case ENOTEMPTY: return FL_ENOTEMPTY;
    case ENOSPC: return FL_ENOSPC;
    case EROFS: return FL_EROFS;
    case ENOMEM: return FL_ENOMEM;
    case EIO: return FL_EIO;
    case ENODEV: return FL_ENODEV;
    case ETIMEDOUT: return FL_ETIMEDOUT;
    case EACCES:
    case EOPNOTSUPP: return FL_ENOTSUP;
    case EFBIG: return FL_EFBIG;
    case EBUSY: return FL_EBUSY;
    default: return FL_ECORRUPT;
  }
}

/* Lecture seule collante : plus rien n'est écrit (le drapeau « sale » reste sur le disque). Sous g_ntfs_lock. */
static void go_ro(ntfs_vol *v, const char *why) {
  ntfs_dev_set_writable(v->dd, false);
  fl_fs_go_read_only(v->fs, why);
}

/* Code d'une opération d'écriture de libntfs-3g en échec (errno e) : erreur du disque → FL_RO_IOERR,
 * incohérence → FL_RO_CORRUPT, sinon le code seul (nom pris, disque plein…). Sous g_ntfs_lock. */
static int wr_err(ntfs_vol *v, int e) {
  if (ntfs_dev_error(v->dd) != FL_OK) {
    go_ro(v, FL_RO_IOERR);
    return FL_EIO;
  }
  switch (e) {
    case ENOENT: case ENOTDIR: case EISDIR: case EEXIST: case ENOTEMPTY: case ENOSPC: case ENOMEM:
    case EACCES: case EOPNOTSUPP: case EFBIG: case EBUSY: case EINVAL: case ENAMETOOLONG: case EROFS:
      return e == EINVAL || e == ENAMETOOLONG ? FL_EINVAL : ntfs_err(e);
    default:
      go_ro(v, FL_RO_CORRUPT);
      return FL_ECORRUPT;
  }
}

/* Oublie les changements non écrits d'un inode (et de ses extensions) : libntfs-3g ne libère pas un inode
 * qu'elle n'a pas pu écrire. Volume déjà en lecture seule et marqué sale. */
static void drop_dirty(ntfs_inode *ni) {
  NInoClearDirty(ni);
  NInoFileNameClearDirty(ni);
  NInoAttrListClearDirty(ni);
  for (s32 i = 0; i < ni->nr_extents; i++) NInoClearDirty(ni->extent_nis[i]);
}

/* Ferme un inode (par ntfs_inode_close_in_dir si son dossier parent dir est ouvert). Échec de l'écriture :
 * lecture seule collante, changements abandonnés, inode libéré quand même. Sous g_ntfs_lock. */
static int close_ni(ntfs_vol *v, ntfs_inode *ni, ntfs_inode *dir) {
  if (!(dir ? ntfs_inode_close_in_dir(ni, dir) : ntfs_inode_close(ni))) return FL_OK;
  int rc = wr_err(v, errno);
  if (!v->fs->read_only) go_ro(v, FL_RO_CORRUPT);
  drop_dirty(ni);
  ntfs_inode_close(ni);
  return rc;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* --- politique de lecture seule ---------------------------------------------------------------- */

/* Vrai si $LogFile est vide ou indique un arrêt propre (comme ntfs_volume_check_logfile, qui est privée). */
static bool logfile_clean(ntfs_volume *vol) {
  ntfs_inode *ni = ntfs_inode_open(vol, FILE_LogFile);
  if (!ni) return false;
  ntfs_attr *na = ntfs_attr_open(ni, AT_DATA, AT_UNNAMED, 0);
  bool ok = false;
  if (na) {
    RESTART_PAGE_HEADER *rp = NULL;
    ok = ntfs_check_logfile(na, &rp) && ntfs_is_logfile_clean(na, rp);
    free(rp);
    ntfs_attr_close(na);
  }
  ntfs_inode_close(ni);
  return ok;
}

/* Raison de refuser l'écriture (NULL : écriture permise), sur un volume monté en lecture seule. */
static const char *write_policy(ntfs_volume *vol, uint32_t bps, const fl_bdev *dev) {
  if (vol->flags & (VOLUME_IS_DIRTY | VOLUME_RESIZE_LOG_FILE | VOLUME_UPGRADE_ON_MOUNT | VOLUME_DELETE_USN_UNDERWAY |
                    VOLUME_REPAIR_OBJECT_ID | VOLUME_CHKDSK_UNDERWAY))
    return FL_RO_DIRTY;
  if (ntfs_volume_check_hiberfile(vol, 0) < 0) return FL_RO_HIBER; /* veille, ou fichier illisible */
  if (!logfile_clean(vol)) return FL_RO_LOGFILE;
  if (vol->major_ver != 3 || vol->minor_ver > 1) return FL_RO_VERSION;
  if (bps < dev->sector_size) return FL_RO_SECTOR;
  return NULL;
}

/* --- montage ----------------------------------------------------------------------------------- */

/* Monte par libntfs-3g (sous g_ntfs_lock) ; caches, noms insensibles à la casse, espace libre calculé. */
static int mount_locked(fl_bdev *dev, bool rw, ntfs_dev **dd, ntfs_volume **vol) {
  *vol = NULL;
  int rc = ntfs_dev_new(dev, rw, dd);
  if (rc != FL_OK) return rc;
  errno = 0;
  *vol = ntfs_device_mount(ntfs_dev_device(*dd), rw ? NTFS_MNT_NONE : NTFS_MNT_RDONLY);
  if (!*vol) {
    int e = errno;
    ntfs_dev_free(*dd, true);
    *dd = NULL;
    return e == ENOMEM ? FL_ENOMEM : e == EIO || e == ENODEV || e == ETIMEDOUT ? ntfs_err(e) : FL_ECORRUPT;
  }
  /* Comme ntfs_mount() : sans ces caches, ntfs_inode_lookup_by_mbsname échoue (la colle ne s'en sert pas,
   * mais libntfs-3g, si). Volume laissé sensible à la casse : en mode insensible, ntfs_readdir rend les
   * noms en minuscules ; la colle cherche elle-même sans la casse en second essai (lookup). Fichiers
   * cachés montrés, fichiers système non. */
  ntfs_create_lru_caches(*vol);
  if (ntfs_set_shown_files(*vol, FALSE, TRUE, FALSE) || ntfs_volume_get_free_space(*vol)) {
    int e = errno;
    ntfs_umount(*vol, TRUE);
    ntfs_dev_free(*dd, false);
    *dd = NULL;
    *vol = NULL;
    return e == ENOMEM ? FL_ENOMEM : FL_ECORRUPT;
  }
  return FL_OK;
}

static void release(ntfs_vol *v) {
  if (v->vol) ntfs_umount(v->vol, TRUE);
  ntfs_dev_free(v->dd, false);
  v->vol = NULL;
  v->dd = NULL;
}

/* --- opérations -------------------------------------------------------------------------------- */

static int n_statfs(fl_fs *fs, fl_statfs *out) {
  ntfs_vol *v = fs->priv;
  pthread_mutex_lock(&g_ntfs_lock);
  ntfs_volume *vol = v->vol;
  out->total_bytes = (uint64_t)vol->nr_clusters * vol->cluster_size;
  out->free_bytes = vol->free_clusters > 0 ? (uint64_t)vol->free_clusters * vol->cluster_size : 0;
  const char *l = vol->vol_name ? vol->vol_name : "";
  size_t n = strlen(l);
  if (n > sizeof out->label - 1) {
    n = sizeof out->label - 1;
    while (n > 0 && ((unsigned char)l[n] & 0xC0) == 0x80) n--; /* pas de caractère UTF-8 coupé */
  }
  memcpy(out->label, l, n);
  out->label[n] = 0;
  pthread_mutex_unlock(&g_ntfs_lock);
  out->needs_recovery = false;
  return FL_OK;
}

/* --- noms et chemins ---------------------------------------------------------------------------- */

/* Numéro MFT de `name` (UTF-8) dans le dossier dir_ni : nom exact d'abord, puis sans la casse (comme
 * Windows). Les fichiers système (MFT < 16) sont absents. Sous g_ntfs_lock. */
static int lookup(ntfs_inode *dir_ni, const char *name, MFT_REF *out) {
  ntfschar *u = NULL;
  int len = ntfs_mbstoucs(name, &u);
  if (len <= 0 || len > 255) {
    free(u);
    return FL_ENOENT;
  }
  ntfs_volume *vol = dir_ni->vol;
  errno = 0;
  u64 r = ntfs_inode_lookup_by_name(dir_ni, u, len);
  if (r == (u64)-1 && errno == ENOENT) {
    NVolClearCaseSensitive(vol);
    errno = 0;
    r = ntfs_inode_lookup_by_name(dir_ni, u, len);
    NVolSetCaseSensitive(vol);
  }
  int e = errno;
  free(u);
  if (r == (u64)-1) return e == ENOENT ? FL_ENOENT : ntfs_err(e);
  if (MREF(r) < FILE_first_user) return FL_ENOENT;
  *out = r;
  return FL_OK;
}

static bool is_dir(const ntfs_inode *ni) {
  return (ni->mrec->flags & MFT_RECORD_IS_DIRECTORY) && !(ni->flags & FILE_ATTR_REPARSE_POINT);
}

/* Numéro MFT d'un chemin validé (et de son dossier parent si parent n'est pas NULL ; la racine est son
 * propre parent). Sous g_ntfs_lock. */
static int resolve(ntfs_vol *v, const char *path, MFT_REF *out, MFT_REF *parent) {
  MFT_REF cur = FILE_root, par = FILE_root;
  for (const char *p = path + 1; *p;) {
    const char *e = strchr(p, '/');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    char name[256];
    memcpy(name, p, n);
    name[n] = 0;
    ntfs_inode *ni = ntfs_inode_open(v->vol, cur);
    if (!ni) return ntfs_err(errno);
    int rc = is_dir(ni) ? lookup(ni, name, &cur) : FL_ENOTDIR;
    MFT_REF here = ni->mft_no;
    ntfs_inode_close(ni);
    if (rc != FL_OK) return rc;
    par = here;
    p += n;
    if (*p == '/') p++;
  }
  *out = cur;
  if (parent) *parent = par;
  return FL_OK;
}

/* --- attributs --------------------------------------------------------------------------------- */

/* Étiquette de point d'analyse (0 si illisible). */
static uint32_t reparse_tag(ntfs_inode *ni) {
  s64 sz = 0;
  uint8_t *rp = ntfs_attr_readall(ni, AT_REPARSE_POINT, AT_UNNAMED, 0, &sz);
  uint32_t tag = rp && sz >= 4 ? (uint32_t)(rp[0] | rp[1] << 8 | rp[2] << 16 | (uint32_t)rp[3] << 24) : 0;
  free(rp);
  return tag;
}

/* Type, taille, droits et date d'un inode ouvert (voir Global Constraints du plan, « Ce que voit l'API »). */
static int fill_stat(ntfs_inode *ni, fl_stat *out) {
  memset(out, 0, sizeof *out);
  out->mtime = (int64_t)ntfs2timespec(ni->last_data_change_time).tv_sec;
  if (ni->flags & FILE_ATTR_REPARSE_POINT) {
    uint32_t tag = reparse_tag(ni);
    bool link = tag == IO_REPARSE_TAG_SYMLINK || tag == IO_REPARSE_TAG_MOUNT_POINT;
    out->type = link ? FL_FT_SYMLINK : FL_FT_OTHER;
    out->mode = link ? 0120777 : 0100000;
    return FL_OK;
  }
  if (ni->mrec->flags & MFT_RECORD_IS_DIRECTORY) {
    out->type = FL_FT_DIR;
    out->mode = 040755;
    return FL_OK;
  }
  if (ni->flags & FILE_ATTR_SYSTEM) { /* fichiers spéciaux Interix (lien, tube, périphérique) */
    unsigned t = ntfs_interix_types(ni);
    if (t == NTFS_DT_LNK || t == NTFS_DT_FIFO || t == NTFS_DT_CHR || t == NTFS_DT_BLK || t == NTFS_DT_SOCK) {
      out->type = t == NTFS_DT_LNK ? FL_FT_SYMLINK : FL_FT_OTHER;
      out->mode = t == NTFS_DT_LNK ? 0120777 : 0100000;
      return FL_OK;
    }
  }
  out->type = FL_FT_FILE;
  out->mode = ni->flags & FILE_ATTR_READONLY ? 0100444 : 0100644;
  ntfs_attr *na = ntfs_attr_open(ni, AT_DATA, AT_UNNAMED, 0);
  if (!na) return errno == ENOENT ? FL_OK : ntfs_err(errno); /* fichier sans $DATA : vide */
  out->size = (uint64_t)na->data_size;
  ntfs_attr_close(na);
  return FL_OK;
}

static int stat_mref(ntfs_vol *v, MFT_REF m, fl_stat *out) {
  ntfs_inode *ni = ntfs_inode_open(v->vol, m);
  if (!ni) return ntfs_err(errno);
  int rc = fill_stat(ni, out);
  ntfs_inode_close(ni);
  return rc;
}

/* écriture (plus bas) */
static int mark_dirty(ntfs_vol *v);
static int sync_locked(ntfs_vol *v);
static int open_data(ntfs_vol *v, MFT_REF m, ntfs_inode **ni, ntfs_attr **na);
static int close_data(ntfs_vol *v, ntfs_inode *ni, ntfs_attr *na, int rc);
static int create_node(ntfs_vol *v, const char *path, mode_t type, MFT_REF *out);

/* --- opérations de lecture ---------------------------------------------------------------------- */

static int n_stat(fl_fs *fs, const char *path, fl_stat *out) {
  ntfs_vol *v = fs->priv;
  pthread_mutex_lock(&g_ntfs_lock);
  MFT_REF m;
  int rc = resolve(v, path, &m, NULL);
  if (rc == FL_OK) rc = stat_mref(v, m, out);
  pthread_mutex_unlock(&g_ntfs_lock);
  return rc;
}

typedef struct {
  char name[READDIR_BATCH][256];
  MFT_REF mref[READDIR_BATCH];
  size_t n;
  bool full;
} batch_t;

/* Rappel de ntfs_readdir (sous g_ntfs_lock) : « . », « .. », noms DOS et fichiers système sautés ; nom
 * illisible ou de plus de 255 octets en UTF-8 sauté. Paquet plein : arrêt (pos reste sur cette entrée,
 * rendue au paquet suivant). */
static int fill_batch(void *ctx, const ntfschar *name, const int len, const int type, const s64 pos,
                      const MFT_REF mref, const unsigned dt) {
  (void)pos;
  (void)dt;
  batch_t *b = ctx;
  if (type == FILE_NAME_DOS || MREF(mref) < FILE_first_user) return 0;
  if ((len == 1 || len == 2) && le16_to_cpu(name[0]) == '.' && (len == 1 || le16_to_cpu(name[1]) == '.')) return 0;
  if (b->n == READDIR_BATCH) {
    b->full = true;
    return 1;
  }
  char *s = NULL;
  int n = ntfs_ucstombs(name, len, &s, 0);
  if (n > 0 && n <= 255) {
    memcpy(b->name[b->n], s, (size_t)n + 1);
    b->mref[b->n++] = mref;
  }
  free(s);
  return 0;
}

static int n_readdir(fl_fs *fs, const char *path, fl_readdir_cb cb, void *ctx) {
  ntfs_vol *v = fs->priv;
  batch_t *b = malloc(sizeof *b);
  fl_dirent *ents = malloc(READDIR_BATCH * sizeof *ents);
  if (!b || !ents) {
    free(b);
    free(ents);
    return FL_ENOMEM;
  }
  pthread_mutex_lock(&g_ntfs_lock);
  MFT_REF dir;
  int rc = resolve(v, path, &dir, NULL);
  if (rc == FL_OK) {
    ntfs_inode *ni = ntfs_inode_open(v->vol, dir);
    rc = !ni ? ntfs_err(errno) : is_dir(ni) ? FL_OK : FL_ENOTDIR;
    if (ni) ntfs_inode_close(ni);
  }
  if (rc == FL_OK) v->open_files++;
  pthread_mutex_unlock(&g_ntfs_lock);
  bool opened = rc == FL_OK, fin = false;
  s64 pos = 0;
  while (rc == FL_OK && !fin) {
    b->n = 0;
    b->full = false;
    size_t n = 0;
    pthread_mutex_lock(&g_ntfs_lock);
    ntfs_inode *ni = ntfs_inode_open(v->vol, dir);
    if (!ni) rc = ntfs_err(errno);
    else if (ntfs_readdir(ni, &pos, b, fill_batch) && !b->full) rc = ntfs_err(errno); /* arrêt du rappel : -1 aussi */
    if (ni) ntfs_inode_close(ni);
    fin = !b->full;
    for (size_t i = 0; rc == FL_OK && i < b->n; i++) {
      memcpy(ents[n].name, b->name[i], sizeof ents[n].name);
      if (stat_mref(v, b->mref[i], &ents[n].st) != FL_OK) { /* inode illisible : listé, inutilisable */
        memset(&ents[n].st, 0, sizeof ents[n].st);
        ents[n].st.type = FL_FT_OTHER;
        ents[n].st.mode = 0100000;
      }
      n++;
    }
    pthread_mutex_unlock(&g_ntfs_lock);
    for (size_t i = 0; rc == FL_OK && i < n; i++) {
      int r = cb(ctx, &ents[i]);
      if (r != 0) {
        rc = r;
        fin = true;
      }
    }
  }
  if (opened) {
    pthread_mutex_lock(&g_ntfs_lock);
    v->open_files--;
    pthread_mutex_unlock(&g_ntfs_lock);
  }
  free(b);
  free(ents);
  return rc;
}

static ntfs_node *find_node(const ntfs_vol *v, MFT_REF m) {
  for (ntfs_node *n = v->nodes; n; n = n->next)
    if (MREF(n->mref) == MREF(m)) return n;
  return NULL;
}

static int n_open(fl_fs *fs, const char *path, int flags, fl_file **out) {
  ntfs_vol *v = fs->priv;
  fl_file *f = calloc(1, sizeof *f);
  ntfs_fh *h = calloc(1, sizeof *h);
  ntfs_node *fresh = calloc(1, sizeof *fresh);
  if (!f || !h || !fresh) {
    free(f);
    free(h);
    free(fresh);
    return FL_ENOMEM;
  }
  bool rw = flags & FL_O_RDWR;
  pthread_mutex_lock(&g_ntfs_lock);
  MFT_REF m;
  int rc = resolve(v, path, &m, NULL);
  if (rc == FL_ENOENT && (flags & FL_O_CREAT)) rc = create_node(v, path, S_IFREG, &m);
  if (rc == FL_OK) {
    ntfs_inode *ni = ntfs_inode_open(v->vol, m);
    if (!ni) {
      rc = ntfs_err(errno);
    } else {
      fl_stat st;
      rc = fill_stat(ni, &st);
      if (rc == FL_OK && st.type == FL_FT_DIR) rc = FL_EISDIR;
      else if (rc == FL_OK && st.type != FL_FT_FILE) rc = FL_ENOTSUP;
      else if (rc == FL_OK && (ni->flags & FILE_ATTR_ENCRYPTED)) rc = FL_ENOTSUP; /* EFS */
      else if (rc == FL_OK && rw && (ni->flags & FILE_ATTR_READONLY)) rc = FL_EROFS;
      if (rc == FL_OK && rw) { /* flux compressé par Windows : lecture seulement (compression désactivée) */
        ntfs_attr *na = ntfs_attr_open(ni, AT_DATA, AT_UNNAMED, 0);
        if (na && (na->data_flags & ATTR_COMPRESSION_MASK)) rc = FL_ENOTSUP;
        if (na) ntfs_attr_close(na);
      }
      ntfs_inode_close(ni);
    }
  }
  bool trunc = false;
  if (rc == FL_OK && rw && (flags & FL_O_TRUNC)) {
    ntfs_inode *ni = NULL;
    ntfs_attr *na = NULL;
    rc = mark_dirty(v);
    if (rc == FL_OK) rc = open_data(v, m, &ni, &na);
    if (rc == FL_OK) {
      if (na->data_size && ntfs_attr_truncate(na, 0)) rc = wr_err(v, errno);
      rc = close_data(v, ni, na, rc);
      trunc = true;
    }
  }
  if (rc == FL_OK) {
    ntfs_node *n = find_node(v, m);
    if (!n) {
      n = fresh;
      fresh = NULL;
      n->mref = m;
      n->next = v->nodes;
      v->nodes = n;
    }
    n->refs++;
    h->n = n;
    h->writer = rw;
    h->wrote = trunc;
    if (h->writer) n->writers++;
    v->open_files++;
  }
  pthread_mutex_unlock(&g_ntfs_lock);
  free(fresh);
  if (rc != FL_OK) {
    free(f);
    free(h);
    return rc;
  }
  f->fs = fs;
  f->flags = flags;
  f->priv = h;
  *out = f;
  return FL_OK;
}

static int64_t n_pread(fl_file *f, void *buf, size_t len, uint64_t off) {
  ntfs_fh *h = f->priv;
  ntfs_vol *v = f->fs->priv;
  int64_t got = 0;
  pthread_mutex_lock(&g_ntfs_lock);
  ntfs_inode *ni = ntfs_inode_open(v->vol, h->n->mref);
  ntfs_attr *na = ni ? ntfs_attr_open(ni, AT_DATA, AT_UNNAMED, 0) : NULL;
  if (!na) {
    got = ntfs_err(errno);
  } else if (off < (uint64_t)na->data_size) {
    s64 want = (uint64_t)na->data_size - off < len ? na->data_size - (s64)off : (s64)len;
    while (got < want) {
      s64 r = ntfs_attr_pread(na, (s64)off + got, want - got, (char *)buf + got);
      if (r < 0) {
        got = got ? got : ntfs_err(errno);
        break;
      }
      if (r == 0) break;
      got += r;
    }
  }
  if (na) ntfs_attr_close(na);
  if (ni) ntfs_inode_close(ni);
  pthread_mutex_unlock(&g_ntfs_lock);
  return got;
}

/* Retire la poignée de son noeud (sous g_ntfs_lock). */
static void node_put(ntfs_vol *v, ntfs_fh *h) {
  ntfs_node *n = h->n;
  if (h->writer) n->writers--;
  if (--n->refs) return;
  for (ntfs_node **p = &v->nodes; *p; p = &(*p)->next)
    if (*p == n) {
      *p = n->next;
      break;
    }
  free(n);
}

static int n_close(fl_file *f) {
  ntfs_fh *h = f->priv;
  ntfs_vol *v = f->fs->priv;
  pthread_mutex_lock(&g_ntfs_lock);
  int rc = h->wrote ? sync_locked(v) : FL_OK;
  node_put(v, h);
  v->open_files--;
  pthread_mutex_unlock(&g_ntfs_lock);
  free(h);
  free(f);
  return rc;
}

/* --- écriture (T4) ------------------------------------------------------------------------------ */

/* Écrit l'enregistrement de $Volume (modifié en mémoire) dans $MFT, ou dans sa copie de $MFTMirr, puis vide
 * le cache du disque (mode direct, sous g_ntfs_lock). */
static int write_volume_record(ntfs_vol *v, bool mirror) {
  ntfs_volume *vol = v->vol;
  if (mirror && vol->mftmirr_size <= FILE_Volume) return FL_OK;
  void *b = malloc(vol->mft_record_size);
  if (!b) return FL_ENOMEM;
  memcpy(b, vol->vol_ni->mrec, vol->mft_record_size);
  s64 w = ntfs_attr_mst_pwrite(mirror ? vol->mftmirr_na : vol->mft_na, (s64)FILE_Volume << vol->mft_record_size_bits,
                               1, vol->mft_record_size, b);
  int e = errno;
  free(b);
  if (w != 1) return wr_err(v, e);
  if (ntfs_device_sync(vol->dev)) {
    go_ro(v, FL_RO_IOERR);
    return FL_EIO;
  }
  return FL_OK;
}

/* Pose ou efface VOLUME_IS_DIRTY dans $Volume (sous g_ntfs_lock, aucun inode ouvert). Deux copies : $MFT
 * (qui fait foi) et $MFTMirr, chacune suivie d'un vidage du cache du disque. Posé : $MFT d'abord ; effacé :
 * $MFT en dernier. Une coupure entre les deux laisse donc toujours $MFT marqué sale (et les copies
 * différentes, ce que chkdsk répare). */
static int set_dirty_flag(ntfs_vol *v, bool sale) {
  ntfs_volume *vol = v->vol;
  /* en mode direct : rien d'autre n'attend, et chaque copie va au disque avant son vidage */
  if (ntfs_dev_set_direct(v->dd, true) != FL_OK) {
    ntfs_dev_set_direct(v->dd, false);
    go_ro(v, FL_RO_IOERR);
    return FL_EIO;
  }
  int rc = FL_OK;
  ntfs_attr_search_ctx *ctx = ntfs_attr_get_search_ctx(vol->vol_ni, NULL);
  if (!ctx || ntfs_attr_lookup(AT_VOLUME_INFORMATION, AT_UNNAMED, 0, CASE_SENSITIVE, 0, NULL, 0, ctx) ||
      ctx->attr->non_resident) {
    rc = ctx ? FL_ECORRUPT : FL_ENOMEM;
    if (rc == FL_ECORRUPT) go_ro(v, FL_RO_CORRUPT);
  } else {
    VOLUME_INFORMATION *vi = (VOLUME_INFORMATION *)((u8 *)ctx->attr + le16_to_cpu(ctx->attr->value_offset));
    vi->flags = sale ? (le16)(vi->flags | VOLUME_IS_DIRTY) : (le16)(vi->flags & ~VOLUME_IS_DIRTY);
    vol->flags = vi->flags;
  }
  if (ctx) ntfs_attr_put_search_ctx(ctx);
  if (rc == FL_OK) rc = write_volume_record(v, !sale);
  if (rc == FL_OK) rc = write_volume_record(v, sale);
  if (rc == FL_OK) NInoClearDirty(vol->vol_ni); /* les deux copies sont écrites */
  ntfs_dev_set_direct(v->dd, false);
  return rc;
}

/* Avant toute modification (sous g_ntfs_lock, aucun inode ouvert). */
static int mark_dirty(ntfs_vol *v) {
  if (v->dirty) return FL_OK;
  int rc = set_dirty_flag(v, true);
  if (rc == FL_OK) v->dirty = true;
  return rc;
}

/* Inodes système écrits, cache du disque vidé, drapeau effacé (sous g_ntfs_lock, aucun inode ouvert). */
static int sync_locked(ntfs_vol *v) {
  if (v->fs->read_only) return atomic_load(&v->fs->went_ro) ? FL_EIO : FL_OK;
  if (!v->dirty) return FL_OK;
  ntfs_volume *vol = v->vol;
  ntfs_inode *sys[] = {vol->mft_ni, vol->mftmirr_ni, vol->lcnbmp_ni};
  for (size_t i = 0; i < sizeof sys / sizeof *sys; i++)
    if (sys[i] && NInoDirty(sys[i]) && ntfs_inode_sync(sys[i])) return wr_err(v, errno);
  if (ntfs_device_sync(vol->dev)) {
    go_ro(v, FL_RO_IOERR);
    return FL_EIO;
  }
  int rc = set_dirty_flag(v, false);
  if (rc == FL_OK) v->dirty = false;
  return rc;
}

/* Inode et $DATA d'un fichier ouvert (sous g_ntfs_lock). */
static int open_data(ntfs_vol *v, MFT_REF m, ntfs_inode **ni, ntfs_attr **na) {
  *na = NULL;
  *ni = ntfs_inode_open(v->vol, m);
  if (!*ni) return wr_err(v, errno);
  *na = ntfs_attr_open(*ni, AT_DATA, AT_UNNAMED, 0);
  if (!*na) {
    int rc = wr_err(v, errno);
    ntfs_inode_close(*ni);
    *ni = NULL;
    return rc;
  }
  return FL_OK;
}

/* Referme après une modification : horodatages mis à jour, inode écrit (sous g_ntfs_lock). */
static int close_data(ntfs_vol *v, ntfs_inode *ni, ntfs_attr *na, int rc) {
  ntfs_attr_close(na);
  ntfs_inode_update_times(ni, NTFS_UPDATE_MCTIME);
  int c = close_ni(v, ni, NULL);
  return rc == FL_OK ? c : rc;
}

static int64_t n_pwrite(fl_file *f, const void *buf, size_t len, uint64_t off) {
  ntfs_fh *h = f->priv;
  ntfs_vol *v = f->fs->priv;
  if (len == 0) return 0;
  if (off > (uint64_t)INT64_MAX - len) return FL_EFBIG;
  pthread_mutex_lock(&g_ntfs_lock);
  ntfs_inode *ni = NULL;
  ntfs_attr *na = NULL;
  int rc = mark_dirty(v);
  if (rc == FL_OK) rc = open_data(v, h->n->mref, &ni, &na);
  if (rc == FL_OK) {
    h->wrote = true;
    s64 done = 0;
    while (rc == FL_OK && (size_t)done < len) {
      s64 w = ntfs_attr_pwrite(na, (s64)off + done, (s64)len - done, (const char *)buf + done);
      if (w <= 0) rc = wr_err(v, w < 0 ? errno : ENOSPC);
      else done += w;
    }
    rc = close_data(v, ni, na, rc);
  }
  pthread_mutex_unlock(&g_ntfs_lock);
  return rc == FL_OK ? (int64_t)len : rc;
}

static int n_truncate(fl_file *f, uint64_t size) {
  ntfs_fh *h = f->priv;
  ntfs_vol *v = f->fs->priv;
  if (size > (uint64_t)INT64_MAX) return FL_EFBIG;
  pthread_mutex_lock(&g_ntfs_lock);
  ntfs_inode *ni = NULL;
  ntfs_attr *na = NULL;
  int rc = mark_dirty(v);
  if (rc == FL_OK) rc = open_data(v, h->n->mref, &ni, &na);
  if (rc == FL_OK) {
    h->wrote = true;
    if ((uint64_t)na->data_size != size && ntfs_attr_truncate(na, (s64)size)) rc = wr_err(v, errno);
    rc = close_data(v, ni, na, rc);
  }
  pthread_mutex_unlock(&g_ntfs_lock);
  return rc;
}

/* --- dossiers et noms (T5) ------------------------------------------------------------------------ */

/* Nom créable sous Windows (dernier composant d'un chemin validé) en UTF-16 dans *u (à libérer) :
 * FL_EINVAL pour : * ? " < > | \, un caractère de contrôle, un nom réservé (CON, NUL, COM1…), un « . » ou
 * une espace finale, plus de 255 unités UTF-16 (sous g_ntfs_lock). */
static int leaf_name(ntfs_vol *v, const char *leaf, ntfschar **u, int *len) {
  *u = NULL;
  *len = ntfs_mbstoucs(leaf, u);
  if (*len <= 0 || *len > 255 || ntfs_forbidden_names(v->vol, *u, *len, TRUE)) {
    free(*u);
    *u = NULL;
    return FL_EINVAL;
  }
  return FL_OK;
}

/* Dossier parent (chemin) et dernier composant d'un chemin validé autre que "/". */
static const char *split(const char *path, char parent[4096]) {
  const char *leaf = strrchr(path, '/') + 1;
  size_t pl = (size_t)(leaf - path) - 1;
  memcpy(parent, path, pl ? pl : 1);
  parent[pl ? pl : 1] = 0;
  return leaf;
}

/* Vrai si le dossier `dir` est `m` ou l'un de ses descendants (remonte les parents ; sous g_ntfs_lock). */
static int is_under(ntfs_vol *v, MFT_REF dir, MFT_REF m, bool *under) {
  *under = false;
  for (int steps = 0; steps < 4096; steps++) {
    if (MREF(dir) == MREF(m)) {
      *under = true;
      return FL_OK;
    }
    if (MREF(dir) == FILE_root) return FL_OK;
    ntfs_inode *ni = ntfs_inode_open(v->vol, dir);
    if (!ni) return ntfs_err(errno);
    ntfs_inode *p = ntfs_dir_parent_inode(ni);
    ntfs_inode_close(ni);
    if (!p) return FL_ECORRUPT;
    dir = MK_MREF(p->mft_no, le16_to_cpu(p->mrec->sequence_number));
    ntfs_inode_close(p);
  }
  return FL_ECORRUPT; /* chaîne de parents trop longue : disque incohérent */
}

/* Ajoute le nom (u, len) à l'inode m dans le dossier dir (sous g_ntfs_lock). */
static int link_name(ntfs_vol *v, MFT_REF m, MFT_REF dir, const ntfschar *u, int len) {
  ntfs_inode *dn = ntfs_inode_open(v->vol, dir);
  if (!dn) return wr_err(v, errno);
  ntfs_inode *ni = ntfs_inode_open(v->vol, m);
  int rc = ni ? FL_OK : wr_err(v, errno);
  if (rc == FL_OK && ntfs_link(ni, dn, (ntfschar *)u, (u8)len)) rc = wr_err(v, errno);
  if (rc == FL_OK) ntfs_inode_update_times(dn, NTFS_UPDATE_MCTIME);
  if (ni) {
    int c = close_ni(v, ni, dn);
    if (rc == FL_OK) rc = c;
  }
  int c = close_ni(v, dn, NULL);
  return rc == FL_OK ? c : rc;
}

/* Retire le nom `name` (UTF-8 : nom exact d'abord, puis sans la casse) de l'inode m dans le dossier dir ;
 * l'inode est supprimé s'il n'a plus de nom (sous g_ntfs_lock). */
static int unlink_name(ntfs_vol *v, MFT_REF m, MFT_REF dir, const char *name) {
  ntfschar *u = NULL;
  int len = ntfs_mbstoucs(name, &u);
  if (len <= 0) {
    free(u);
    return FL_ENOENT;
  }
  int rc = FL_OK;
  ntfs_inode *dn = ntfs_inode_open(v->vol, dir);
  ntfs_inode *ni = dn ? ntfs_inode_open(v->vol, m) : NULL;
  if (!dn || !ni) {
    rc = wr_err(v, errno);
    if (dn) ntfs_inode_close(dn);
  } else {
    /* ntfs_delete cherche le nom exact puis sans la casse, sauf pour un nom POSIX sur un volume sensible à
     * la casse : le volume l'est le temps de l'appel. Il referme ni et dn dans tous les cas. */
    NVolClearCaseSensitive(v->vol);
    int r = ntfs_delete(v->vol, NULL, ni, dn, u, (u8)len);
    int e = errno;
    NVolSetCaseSensitive(v->vol);
    if (r) rc = wr_err(v, e);
  }
  free(u);
  return rc;
}

/* Crée le fichier ou dossier `path` (absent) ; type S_IFREG ou S_IFDIR ; numéro MFT dans *out (sous
 * g_ntfs_lock). */
static int create_node(ntfs_vol *v, const char *path, mode_t type, MFT_REF *out) {
  char parent[4096];
  const char *leaf = split(path, parent);
  MFT_REF dir;
  int rc = resolve(v, parent, &dir, NULL);
  if (rc != FL_OK) return rc;
  ntfschar *u = NULL;
  int len = 0;
  if ((rc = leaf_name(v, leaf, &u, &len)) != FL_OK) return rc;
  rc = mark_dirty(v);
  ntfs_inode *dir_ni = rc == FL_OK ? ntfs_inode_open(v->vol, dir) : NULL;
  if (rc == FL_OK && !dir_ni) rc = wr_err(v, errno);
  if (rc == FL_OK && !is_dir(dir_ni)) rc = FL_ENOTDIR;
  if (rc == FL_OK) {
    ntfs_inode *ni = ntfs_create(dir_ni, 0, u, (u8)len, type);
    if (!ni) {
      rc = wr_err(v, errno);
    } else {
      *out = MK_MREF(ni->mft_no, le16_to_cpu(ni->mrec->sequence_number));
      ntfs_inode_update_times(dir_ni, NTFS_UPDATE_MCTIME); /* comme Windows : le dossier a changé */
      /* le parent est ouvert : sans _in_dir, libntfs-3g en ouvrirait une seconde copie */
      rc = close_ni(v, ni, dir_ni);
    }
  }
  if (dir_ni) {
    int c = close_ni(v, dir_ni, NULL);
    if (rc == FL_OK) rc = c;
  }
  free(u);
  return rc;
}

static int n_mkdir(fl_fs *fs, const char *path) {
  ntfs_vol *v = fs->priv;
  if (!strcmp(path, "/")) return FL_EEXIST;
  pthread_mutex_lock(&g_ntfs_lock);
  MFT_REF m;
  int rc = resolve(v, path, &m, NULL); /* sans la casse : « P1 » existe si « p1 » existe */
  if (rc == FL_OK) rc = FL_EEXIST;
  else if (rc == FL_ENOENT) rc = create_node(v, path, S_IFDIR, &m);
  pthread_mutex_unlock(&g_ntfs_lock);
  return rc;
}

/* unlink (dir faux) ou rmdir (dir vrai). */
static int remove_op(fl_fs *fs, const char *path, bool dir) {
  ntfs_vol *v = fs->priv;
  if (!strcmp(path, "/")) return dir ? FL_EINVAL : FL_EISDIR;
  pthread_mutex_lock(&g_ntfs_lock);
  MFT_REF m, parent;
  fl_stat st;
  int rc = resolve(v, path, &m, &parent);
  if (rc == FL_OK) rc = stat_mref(v, m, &st);
  if (rc == FL_OK && dir && st.type != FL_FT_DIR) rc = FL_ENOTDIR;
  if (rc == FL_OK && !dir && st.type == FL_FT_DIR) rc = FL_EISDIR;
  if (rc == FL_OK && find_node(v, m)) rc = FL_EBUSY;
  if (rc == FL_OK && dir) {
    ntfs_inode *ni = ntfs_inode_open(v->vol, m);
    int e = !ni ? -1 : ntfs_check_empty_dir(ni);
    int err = errno;
    if (ni) ntfs_inode_close(ni);
    if (!ni) rc = ntfs_err(err);
    else if (e) rc = err == ENOTEMPTY ? FL_ENOTEMPTY : ntfs_err(err);
  }
  if (rc == FL_OK) rc = mark_dirty(v);
  if (rc == FL_OK) rc = unlink_name(v, m, parent, strrchr(path, '/') + 1);
  pthread_mutex_unlock(&g_ntfs_lock);
  return rc;
}

static int n_unlink(fl_fs *fs, const char *path) { return remove_op(fs, path, false); }
static int n_rmdir(fl_fs *fs, const char *path) { return remove_op(fs, path, true); }

static int n_rename(fl_fs *fs, const char *from, const char *to) {
  ntfs_vol *v = fs->priv;
  if (!strcmp(from, "/") || !strcmp(to, "/")) return FL_EINVAL;
  pthread_mutex_lock(&g_ntfs_lock);
  MFT_REF m, from_dir, to_dir, existing;
  char to_parent[4096];
  const char *to_leaf = split(to, to_parent);
  int rc = resolve(v, from, &m, &from_dir);
  if (rc == FL_OK) rc = resolve(v, to_parent, &to_dir, NULL);
  if (rc == FL_OK) {
    ntfs_inode *ni = ntfs_inode_open(v->vol, to_dir);
    rc = !ni ? ntfs_err(errno) : is_dir(ni) ? FL_OK : FL_ENOTDIR;
    if (ni) ntfs_inode_close(ni);
  }
  bool under = false;
  if (rc == FL_OK) rc = is_under(v, to_dir, m, &under);
  if (rc == FL_OK && under) rc = FL_EINVAL; /* vers lui-même ou l'un de ses descendants */
  bool same = false;
  if (rc == FL_OK) {
    int r = resolve(v, to, &existing, NULL);
    if (r == FL_OK && MREF(existing) == MREF(m) && MREF(to_dir) == MREF(from_dir)) same = true; /* casse */
    else if (r == FL_OK) rc = FL_EEXIST;
    else if (r != FL_ENOENT) rc = r;
  }
  ntfschar *u = NULL;
  int len = 0;
  if (rc == FL_OK) rc = leaf_name(v, to_leaf, &u, &len);
  const char *from_leaf = strrchr(from, '/') + 1;
  if (rc == FL_OK && same) {
    /* même inode, même dossier : même nom (rien à faire), autre nom du même fichier (lien dur : rien à
     * faire non plus, comme rename(2)), ou seule la casse change */
    ntfschar *uf = NULL;
    int lf = ntfs_mbstoucs(from_leaf, &uf);
    bool casse = lf > 0 && strcmp(from_leaf, to_leaf) &&
                 ntfs_names_are_equal(uf, (size_t)lf, u, (size_t)len, IGNORE_CASE, v->vol->upcase, v->vol->upcase_len);
    free(uf);
    if (!casse) {
      free(u);
      pthread_mutex_unlock(&g_ntfs_lock);
      return FL_OK;
    }
  }
  if (rc == FL_OK) rc = mark_dirty(v);
  if (rc == FL_OK && same) {
    /* Seule la casse change : l'index refuserait le nouveau nom (égal sans la casse). Nom temporaire,
     * ancien nom retiré, nouveau nom, nom temporaire retiré ; le drapeau sale couvre une coupure. */
    char tmp[64];
    ntfschar *ut = NULL;
    int lt = 0;
    MFT_REF x;
    for (unsigned n = 0; rc == FL_OK; n++) {
      snprintf(tmp, sizeof tmp, "~fluluks-%u.tmp", n);
      ntfs_inode *dn = ntfs_inode_open(v->vol, to_dir);
      int r = dn ? lookup(dn, tmp, &x) : ntfs_err(errno);
      if (dn) ntfs_inode_close(dn);
      if (r == FL_ENOENT) break;
      if (r != FL_OK) rc = r;
    }
    if (rc == FL_OK) {
      lt = ntfs_mbstoucs(tmp, &ut);
      rc = lt > 0 ? link_name(v, m, to_dir, ut, lt) : FL_ENOMEM;
    }
    if (rc == FL_OK) rc = unlink_name(v, m, from_dir, from_leaf);
    if (rc == FL_OK) rc = link_name(v, m, to_dir, u, len);
    if (rc == FL_OK) rc = unlink_name(v, m, to_dir, tmp);
    free(ut);
  } else if (rc == FL_OK) {
    rc = link_name(v, m, to_dir, u, len);
    if (rc == FL_OK) rc = unlink_name(v, m, from_dir, from_leaf);
  }
  free(u);
  pthread_mutex_unlock(&g_ntfs_lock);
  return rc;
}

static int n_sync(fl_fs *fs) {
  ntfs_vol *v = fs->priv;
  pthread_mutex_lock(&g_ntfs_lock);
  int rc = sync_locked(v);
  pthread_mutex_unlock(&g_ntfs_lock);
  return rc;
}

static int n_unmount(fl_fs *fs) {
  ntfs_vol *v = fs->priv;
  pthread_mutex_lock(&g_ntfs_lock);
  if (v->open_files) {
    pthread_mutex_unlock(&g_ntfs_lock);
    return FL_EINVAL;
  }
  /* Comme ext4 et FAT : tout est libéré, et l'éjection ne se dit pas réussie si des données ont pu être
   * perdues (lecture seule prise en cours de session, ou démontage en échec). */
  int rc = !fs->read_only ? sync_locked(v) : atomic_load(&fs->went_ro) ? FL_EIO : FL_OK;
  if (atomic_load(&fs->went_ro)) { /* plus rien ne s'écrit : libntfs-3g ne libérerait pas ses inodes sales */
    ntfs_inode *sys[] = {v->vol->vol_ni, v->vol->mft_ni, v->vol->mftmirr_ni, v->vol->lcnbmp_ni};
    for (size_t i = 0; i < sizeof sys / sizeof *sys; i++)
      if (sys[i]) drop_dirty(sys[i]);
  }
  if (ntfs_umount(v->vol, FALSE) && rc == FL_OK) rc = FL_EIO;
  v->vol = NULL;
  ntfs_dev_free(v->dd, false);
  v->dd = NULL;
  pthread_mutex_unlock(&g_ntfs_lock);
  free(v);
  free(fs);
  return rc;
}

static const fl_fs_ops k_ntfs_ops = {
    .statfs = n_statfs,
    .stat = n_stat,
    .readdir = n_readdir,
    .open = n_open,
    .pread = n_pread,
    .close = n_close,
    .pwrite = n_pwrite,
    .truncate = n_truncate,
    .sync = n_sync,
    .mkdir = n_mkdir,
    .unlink = n_unlink,
    .rmdir = n_rmdir,
    .rename = n_rename,
    .unmount = n_unmount,
};

int fl_ntfs_mount(fl_bdev *dev, bool want_rw, fl_fs **out) {
  *out = NULL;
  uint8_t bs[512];
  int rc = fl_bdev_read_bytes(dev, 0, sizeof bs, bs);
  if (rc != FL_OK) return rc;
  uint32_t bps = rd16(bs + 11);
  if (memcmp(bs + 3, "NTFS    ", 8) || bps < 256 || bps > 4096 || (bps & (bps - 1))) return FL_ECORRUPT;

  fl_fs *fs = calloc(1, sizeof *fs);
  ntfs_vol *v = calloc(1, sizeof *v);
  if (!fs || !v) {
    free(fs);
    free(v);
    return FL_ENOMEM;
  }
  fs->ops = &k_ntfs_ops;
  fs->dev = dev;
  fs->priv = v;
  v->fs = fs;

  pthread_mutex_lock(&g_ntfs_lock);
  rc = mount_locked(dev, false, &v->dd, &v->vol);
  const char *why = NULL;
  if (rc == FL_OK && want_rw) {
    why = write_policy(v->vol, bps, dev);
    if (!why) {
      release(v);
      rc = mount_locked(dev, true, &v->dd, &v->vol);
      if (rc == FL_ECORRUPT) { /* refusé par libntfs-3g en écriture ($MFTMirr différent, $LogFile, veille…) */
        rc = mount_locked(dev, false, &v->dd, &v->vol);
        if (rc == FL_OK) why = write_policy(v->vol, bps, dev);
        if (rc == FL_OK && !why) why = FL_RO_DIRTY; /* à vérifier par chkdsk */
      }
    }
  }
  if (rc == FL_OK && why) {
    fs->read_only = true;
    snprintf(fs->ro_reason, sizeof fs->ro_reason, "%s", why);
  }
  if (rc != FL_OK) release(v);
  pthread_mutex_unlock(&g_ntfs_lock);
  if (rc != FL_OK) {
    free(v);
    free(fs);
    return rc;
  }
  *out = fs;
  return FL_OK;
}
