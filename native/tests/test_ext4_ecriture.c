#define _GNU_SOURCE
/* ext4 en écriture (phase 3, T4) : API fichiers complète, propriétaire hérité, horodatages, et les
 * correctifs lwext4 0003 qui la rendent sûre.
 *
 * Chaque test part d'une COPIE d'une image du banc (T3), la monte en écriture, opère, démonte
 * proprement, puis juge : « e2fsck -fn » doit sortir avec 0 et debugfs (indépendant de lwext4) doit
 * voir exactement le modèle attendu (tree_model). Une horloge figée sert aux horodatages.
 *
 * Les tests « interne_* » appellent lwext4 directement (sur une copie, par un périphérique fichier) :
 * ils visent des défauts de lwext4 que l'API publique ne peut pas atteindre ou ne peut pas observer
 * (compteurs de références du cache, ajout d'entrée sans recherche préalable).
 *
 * Réservé au PC : sauté (code 77) si e2fsck/debugfs/dumpe2fs sont absents, et sur Android. */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "fault_bdev.h"
#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fs/ro_reasons.h"
#include "fsck_util.h"
#include "tree_model.h"
#include "tt.h"

/* lwext4 corrigé (tests internes) */
#include "ext4.h"
#include "ext4_bcache.h"
#include "ext4_blockdev.h"
#include "ext4_dir.h"
#include "ext4_errno.h"
#include "ext4_fs.h"
#include "ext4_types.h"

#define T0 1760000000LL
#define MIB ((size_t)1 << 20)

/* ---------- fichiers temporaires, horloge, modèle de base ---------- */

static char *temporaires[256];
static size_t ntemp;

static void noter(const char *chemin) {
  if (ntemp < sizeof temporaires / sizeof *temporaires) temporaires[ntemp++] = strdup(chemin);
}

static void nettoyer(void) {
  for (size_t i = 0; i < ntemp; i++) {
    unlink(temporaires[i]);
    free(temporaires[i]);
  }
  ntemp = 0;
}

static int64_t g_t = T0;
static int64_t horloge(void) { return g_t; }

static tm *g_base; /* arborescence de tree/, condensats calculés une fois */

static int copie_de(const char *fixture, char *out, size_t cap) {
  char src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture(fixture));
  if (fu_copy_image(src, out, cap) != 0) return -1;
  noter(out);
  return 0;
}

/* Contenu déterministe (xorshift). */
static void remplir(uint8_t *b, size_t n, uint64_t graine) {
  uint64_t x = graine * 0x9E3779B97F4A7C15ull + 1;
  for (size_t i = 0; i < n; i += 8) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    size_t k = n - i < 8 ? n - i : 8;
    memcpy(b + i, &x, k);
  }
}

static bool tout_zero(const uint8_t *b, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (b[i]) return false;
  return true;
}

typedef struct {
  fl_bdev *file, *dev; /* dev = file, ou une enveloppe (fault_bdev, garde) */
  fl_fs *fs;
} vol;

static int vol_monter_sur(vol *v, bool want_rw) {
  v->fs = NULL;
  int rc = fl_fs_mount(v->dev, FL_CONTENT_EXT4, want_rw, &v->fs);
  if (rc != FL_OK) {
    if (v->dev != v->file) fl_bdev_close(v->dev);
    fl_bdev_close(v->file);
    v->dev = v->file = NULL;
  }
  return rc;
}

/* bdev_ro : périphérique en lecture seule. */
static int vol_monter(vol *v, const char *img, bool want_rw, bool bdev_ro) {
  memset(v, 0, sizeof *v);
  int rc = fl_file_bdev_open(img, 512, bdev_ro, &v->file);
  if (rc != FL_OK) return rc;
  v->dev = v->file;
  return vol_monter_sur(v, want_rw);
}

static int vol_demonter(vol *v) {
  int rc = fl_fs_unmount(v->fs);
  if (rc != FL_OK) return rc;
  if (v->dev != v->file) fl_bdev_close(v->dev);
  fl_bdev_close(v->file);
  memset(v, 0, sizeof *v);
  return FL_OK;
}

static bool lecture_seule(fl_fs *fs, const char *raison) {
  fl_statfs s;
  if (fl_fs_statfs(fs, &s) != FL_OK) return false;
  if (!s.read_only || strcmp(s.ro_reason, raison) != 0) {
    fprintf(stderr, "read_only=%d, raison « %s », attendu « %s »\n", s.read_only, s.ro_reason, raison);
    return false;
  }
  return true;
}

static bool en_ecriture(fl_fs *fs) {
  fl_statfs s;
  if (fl_fs_statfs(fs, &s) != FL_OK) return false;
  if (s.read_only) fprintf(stderr, "monté en lecture seule : %s\n", s.ro_reason);
  return !s.read_only && s.ro_reason[0] == 0;
}

/* Le juge : e2fsck -fn propre, et debugfs voit le modèle (m NULL : e2fsck seul). */
static int juger(const char *img, const tm *m) {
  static char out[1 << 16];
  int rc = fu_e2fsck_n(img, out, sizeof out);
  if (rc != 0) fprintf(stderr, "e2fsck -fn = %d\n%s\n", rc, out);
  int e = m ? tm_check_debugfs(m, img) : 0;
  if (e != 0) fprintf(stderr, "tm_check_debugfs : %d écarts\n", e);
  return rc == 0 && e == 0 ? 0 : -1;
}

/* Crée (ou remplace) un fichier entier. */
static int ecrire_fichier(fl_fs *fs, const char *chemin, const void *data, size_t n) {
  fl_file *f = NULL;
  int rc = fl_fs_open(fs, chemin, FL_O_RDWR | FL_O_CREAT | FL_O_TRUNC, &f);
  if (rc != FL_OK) return rc;
  int64_t w = n ? fl_file_pwrite(f, data, n, 0) : 0;
  rc = fl_file_close(f);
  if (w != (int64_t)n) return w < 0 ? (int)w : FL_EIO;
  return rc;
}

/* Relit [off, off + n) d'un fichier ouvert et le compare à attendu (NULL = des zéros). */
static bool relire(fl_file *f, uint64_t off, const uint8_t *attendu, size_t n) {
  uint8_t *b = malloc(MIB);
  bool ok = b != NULL;
  for (size_t o = 0; ok && o < n; o += MIB) {
    size_t k = n - o < MIB ? n - o : MIB;
    if (fl_file_pread(f, b, k, off + o) != (int64_t)k) ok = false;
    else if (attendu ? memcmp(b, attendu + o, k) != 0 : !tout_zero(b, k)) ok = false;
  }
  free(b);
  return ok;
}

/* ---------- debugfs ---------- */

/* Lance debugfs [-w] -f <cmds> img ; sortie dans out. */
static int debugfs_f(const char *img, bool ecrire, const char *cmds, char *out, size_t cap) {
  char td[4096], cmdf[4200];
  if (fu_tmpdir(td, sizeof td) != 0) return -1;
  snprintf(cmdf, sizeof cmdf, "%s/dbg-XXXXXX", td);
  int fd = mkstemp(cmdf);
  if (fd < 0) return -1;
  size_t n = strlen(cmds);
  bool ok = write(fd, cmds, n) == (ssize_t)n;
  close(fd);
  char *const argw[] = {(char *)"debugfs", (char *)"-w", (char *)"-f", cmdf, (char *)img, NULL};
  char *const argr[] = {(char *)"debugfs", (char *)"-f", cmdf, (char *)img, NULL};
  int rc = ok ? fu_run(ecrire ? argw : argr, out, cap) : -1;
  unlink(cmdf);
  return rc;
}

/* Valeur numérique qui suit `cle` dans la sortie de debugfs (base 0 : décimal ou 0x...). */
static bool champ(const char *out, const char *cle, unsigned long long *v) {
  const char *p = strstr(out, cle);
  if (!p) return false;
  char *fin;
  *v = strtoull(p + strlen(cle), &fin, 0);
  return fin != p + strlen(cle);
}

static bool stat_champ(const char *img, const char *chemin, const char *cle, unsigned long long *v) {
  static char out[1 << 16], cmd[4200];
  snprintf(cmd, sizeof cmd, "stat \"%s\"\n", chemin);
  return debugfs_f(img, false, cmd, out, sizeof out) == 0 && champ(out, cle, v);
}

/* ---------- lwext4 en direct sur un fichier (tests internes) ---------- */

static int g_lw_fd = -1;

static int lw_open(struct ext4_blockdev *bd) { (void)bd; return EOK; }
static int lw_close(struct ext4_blockdev *bd) { (void)bd; return EOK; }
static int lw_bread(struct ext4_blockdev *bd, void *buf, uint64_t blk, uint32_t cnt) {
  (void)bd;
  size_t n = (size_t)cnt * 512;
  return pread(g_lw_fd, buf, n, (off_t)(blk * 512)) == (ssize_t)n ? EOK : EIO;
}
static int lw_bwrite(struct ext4_blockdev *bd, const void *buf, uint64_t blk, uint32_t cnt) {
  (void)bd;
  size_t n = (size_t)cnt * 512;
  return pwrite(g_lw_fd, buf, n, (off_t)(blk * 512)) == (ssize_t)n ? EOK : EIO;
}
static uint8_t g_lw_ph[512];
static struct ext4_blockdev_iface g_lw_if = {
    .open = lw_open, .bread = lw_bread, .bwrite = lw_bwrite, .close = lw_close,
    .ph_bsize = 512, .ph_bbuf = g_lw_ph};
static struct ext4_blockdev g_lw_bd = {.bdif = &g_lw_if};

/* Monte img en écriture sur "/tt/" (sans journal : écritures directes). */
static int lw_monter(const char *img) {
  g_lw_fd = open(img, O_RDWR);
  if (g_lw_fd < 0) return EIO;
  off_t t = lseek(g_lw_fd, 0, SEEK_END);
  g_lw_if.ph_bcnt = (uint64_t)t / 512;
  g_lw_bd.part_offset = 0;
  g_lw_bd.part_size = (uint64_t)t;
  int r = ext4_device_register(&g_lw_bd, "tt-lw");
  if (r == EOK) {
    r = ext4_mount("tt-lw", "/tt/", false);
    if (r != EOK) ext4_device_unregister("tt-lw");
  }
  if (r != EOK) {
    close(g_lw_fd);
    g_lw_fd = -1;
  }
  return r;
}

static int lw_demonter(void) {
  int r = ext4_umount("/tt/");
  ext4_device_unregister("tt-lw");
  close(g_lw_fd);
  g_lw_fd = -1;
  return r;
}

static struct ext4_fs *lw_fs(void) {
  struct ext4_sblock *sb = NULL;
  if (ext4_get_sblock("/tt/", &sb) != EOK) return NULL;
  return (struct ext4_fs *)((char *)sb - offsetof(struct ext4_fs, sb));
}

/* Références (à rendre) de l'inode de `chemin` et de la racine. */
static int lw_refs(const char *chemin, struct ext4_inode_ref *ref, struct ext4_inode_ref *racine) {
  struct ext4_fs *fs = lw_fs();
  uint32_t ino = 0;
  struct ext4_inode in;
  if (!fs) return ENOENT;
  int r = ext4_raw_inode_fill(chemin, &ino, &in);
  if (r != EOK) return r;
  if ((r = ext4_fs_get_inode_ref(fs, ino, ref)) != EOK) return r;
  if ((r = ext4_fs_get_inode_ref(fs, EXT4_INODE_ROOT_INDEX, racine)) != EOK) ext4_fs_put_inode_ref(ref);
  return r;
}

/* Nombre de références au bloc `lba` dans le cache, hors la nôtre (0 si absent du cache). */
static long refs_cache(uint64_t lba) {
  struct ext4_fs *fs = lw_fs();
  if (!fs) return -1;
  struct ext4_block b;
  memset(&b, 0, sizeof b);
  struct ext4_buf *buf = ext4_bcache_find_get(fs->bdev->bc, &b, lba);
  if (!buf) return 0;
  long n = (long)buf->refctr - 1;
  ext4_bcache_free(fs->bdev->bc, &b);
  return n;
}

/* ---------- raisons de lecture seule ---------- */

static void test_ecriture_raisons(void) {
  static const char *const r[] = {FL_RO_FEATURES, FL_RO_RECOVERY, FL_RO_ERRORS, FL_RO_CSUM, FL_RO_NOJOURNAL,
                                  FL_RO_BLOCKSIZE, FL_RO_WP, FL_RO_USER, FL_RO_IOERR};
  for (size_t i = 0; i < sizeof r / sizeof *r; i++) TT_ASSERT(strlen(r[i]) <= 127);
  TT_ASSERT_STR_EQ(FL_RO_FEATURES, "options ext4 non prises en charge en écriture");
  TT_ASSERT_STR_EQ(FL_RO_RECOVERY, "journal à rejouer : branchez le disque sous Ubuntu");
  TT_ASSERT_STR_EQ(FL_RO_ERRORS, "système de fichiers marqué en erreur : vérifiez-le sous Ubuntu");
  TT_ASSERT_STR_EQ(FL_RO_CSUM, "sommes de contrôle invalides");
  TT_ASSERT_STR_EQ(FL_RO_NOJOURNAL, "pas de journal : écriture désactivée par sécurité");
  TT_ASSERT_STR_EQ(FL_RO_BLOCKSIZE, "taille de bloc non prise en charge en écriture");
  TT_ASSERT_STR_EQ(FL_RO_WP, "disque protégé en écriture");
  TT_ASSERT_STR_EQ(FL_RO_USER, "ouvert en lecture seule à votre demande");
  TT_ASSERT_STR_EQ(FL_RO_IOERR, "erreur d'écriture : volume passé en lecture seule");
}

/* ---------- tests de l'API ---------- */

static void test_ecriture_petit_fichier(void) {
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  TT_ASSERT(en_ecriture(v.fs));
  uint8_t data[1024], back[1024];
  remplir(data, sizeof data, 1);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/petit.txt", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pwrite(f, data, sizeof data, 0), (int64_t)sizeof data);
  TT_ASSERT_EQ(fl_file_pread(f, back, sizeof back, 0), (int64_t)sizeof back);
  TT_ASSERT_MEM_EQ(back, data, sizeof data);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/petit.txt", &st), FL_OK);
  TT_ASSERT_EQ(st.type, FL_FT_FILE);
  TT_ASSERT_EQ(st.size, sizeof data);
  TT_ASSERT_EQ(st.mode, 0100644);
  TT_ASSERT_EQ(st.uid, 0); /* uid et gid de la racine (mkfs -d) */
  TT_ASSERT_EQ(st.gid, 0);
  /* Relu depuis une nouvelle ouverture, et par tout le modèle (moteur). */
  memset(back, 0, sizeof back);
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/petit.txt", FL_O_RDONLY, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pread(f, back, sizeof back, 0), (int64_t)sizeof back);
  TT_ASSERT_EQ(fl_file_pread(f, back, sizeof back, sizeof data), 0);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_MEM_EQ(back, data, sizeof data);
  tm *m = tm_clone(g_base);
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(tm_apply_file(m, "/petit.txt", data, sizeof data, 0, 0, 0644), 0);
  TT_ASSERT_EQ(tm_check_fl(m, v.fs), 0);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  /* Relu par debugfs. */
  TT_ASSERT_EQ(fu_debugfs_cat(img, "/petit.txt", back, sizeof back), (int64_t)sizeof back);
  TT_ASSERT_MEM_EQ(back, data, sizeof data);
  TT_ASSERT_EQ(juger(img, m), 0);
  /* Remonté : le moteur relit ce qu'il a écrit. */
  TT_ASSERT_EQ(vol_monter(&v, img, false, true), FL_OK);
  TT_ASSERT_EQ(tm_check_fl(m, v.fs), 0);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  tm_free(m);
  unlink(img);
}

static void test_ecriture_proprietaire_herite(void) {
  char img[4096], out[8192];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(debugfs_f(img, true,
                         "mkdir /u\nsif /u uid 1000\nsif /u gid 1000\nsif /u mode 040755\n"
                         "mkdir /s\nsif /s uid 1000\nsif /s gid 1234\nsif /s mode 042775\n",
                         out, sizeof out),
               0);
  tm *m = tm_clone(g_base);
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/u", 1000, 1000, 0755), 0);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/s", 1000, 1234, 02775), 0);
  TT_ASSERT_EQ(juger(img, m), 0); /* point de départ */

  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/u/f", "u", 1), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/u/d"), FL_OK);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/u/d/g", "g", 1), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/s/d"), FL_OK);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/s/f", "s", 1), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/s/d/e"), FL_OK); /* setgid hérité de génération en génération */
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/u/f", &st), FL_OK);
  TT_ASSERT(st.mode == 0100644 && st.uid == 1000 && st.gid == 1000);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/u/d", &st), FL_OK);
  TT_ASSERT(st.mode == 040755 && st.uid == 1000 && st.gid == 1000);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/u/d/g", &st), FL_OK);
  TT_ASSERT(st.mode == 0100644 && st.uid == 1000 && st.gid == 1000);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/s/d", &st), FL_OK);
  TT_ASSERT_EQ(st.mode, 042755);
  TT_ASSERT(st.uid == 1000 && st.gid == 1234);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/s/f", &st), FL_OK);
  TT_ASSERT(st.mode == 0100644 && st.uid == 1000 && st.gid == 1234);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/s/d/e", &st), FL_OK);
  TT_ASSERT(st.mode == 042755 && st.gid == 1234);
  /* Un fichier modifié garde son propriétaire et ses droits. */
  fl_stat avant;
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/hello.txt", &avant), FL_OK);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/hello.txt", "Salut FluLuks\n", 14), FL_OK);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/hello.txt", &st), FL_OK);
  TT_ASSERT(st.mode == avant.mode && st.uid == avant.uid && st.gid == avant.gid);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);

  uint64_t x;
  TT_ASSERT_EQ(fu_debugfs_inode_field(img, "/s/d", "mode", &x), 0);
  TT_ASSERT_EQ(x, 02755);
  TT_ASSERT_EQ(fu_debugfs_inode_field(img, "/s/d", "gid", &x), 0);
  TT_ASSERT_EQ(x, 1234);
  TT_ASSERT_EQ(fu_debugfs_inode_field(img, "/u/f", "uid", &x), 0);
  TT_ASSERT_EQ(x, 1000);
  TT_ASSERT_EQ(fu_debugfs_inode_field(img, "/u/f", "mode", &x), 0);
  TT_ASSERT_EQ(x, 0644);
  TT_ASSERT_EQ(tm_apply_file(m, "/u/f", "u", 1, 1000, 1000, 0644), 0);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/u/d", 1000, 1000, 0755), 0);
  TT_ASSERT_EQ(tm_apply_file(m, "/u/d/g", "g", 1, 1000, 1000, 0644), 0);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/s/d", 1000, 1234, 02755), 0);
  TT_ASSERT_EQ(tm_apply_file(m, "/s/f", "s", 1, 1000, 1234, 0644), 0);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/s/d/e", 1000, 1234, 02755), 0);
  TT_ASSERT_EQ(tm_apply_file(m, "/hello.txt", "Salut FluLuks\n", 14, avant.uid, avant.gid, avant.mode & 07777), 0);
  TT_ASSERT_EQ(juger(img, m), 0);
  tm_free(m);
  unlink(img);
}

static bool temps(const char *img, const char *chemin, const char *champ_, uint64_t attendu) {
  uint64_t v = 0;
  if (fu_debugfs_inode_field(img, chemin, champ_, &v) != 0) {
    fprintf(stderr, "%s %s : illisible\n", chemin, champ_);
    return false;
  }
  if (v != attendu) fprintf(stderr, "%s %s = %llu, attendu %llu\n", chemin, champ_, (unsigned long long)v,
                            (unsigned long long)attendu);
  return v == attendu;
}

static void test_ecriture_horodatages(void) {
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  g_t = T0;
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/h.txt", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/hd"), FL_OK);
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/h.txt", &st), FL_OK);
  TT_ASSERT_EQ(st.mtime, T0);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT(temps(img, "/h.txt", "mtime", T0));
  TT_ASSERT(temps(img, "/h.txt", "ctime", T0));
  TT_ASSERT(temps(img, "/h.txt", "atime", T0));
  TT_ASSERT(temps(img, "/h.txt", "crtime", T0));
  TT_ASSERT(temps(img, "/hd", "crtime", T0));
  TT_ASSERT(temps(img, "/hd", "mtime", T0));
  TT_ASSERT(temps(img, "/", "mtime", T0));
  TT_ASSERT(temps(img, "/", "ctime", T0));

  /* Écriture puis fermeture, 100 s plus tard : mtime et ctime suivent, crtime et le parent non. */
  g_t = T0 + 100;
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/h.txt", FL_O_RDWR, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pwrite(f, "abc", 3, 0), 3);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/h.txt", &st), FL_OK);
  TT_ASSERT_EQ(st.mtime, T0 + 100);
  /* Troncature : mtime et ctime. */
  g_t = T0 + 150;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/hd/t.txt", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pwrite(f, "0123456789", 10, 0), 10);
  g_t = T0 + 160;
  TT_ASSERT_EQ(fl_file_truncate(f, 4), FL_OK);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  /* Dossiers : créer, renommer, supprimer met à jour mtime et ctime du parent ; le renommage, le
   * ctime de l'inode déplacé. */
  g_t = T0 + 200;
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/hd/sous"), FL_OK);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/hd/sous/x", "x", 1), FL_OK);
  g_t = T0 + 300;
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/hd/sous/x", "/hd/y"), FL_OK);
  g_t = T0 + 400;
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/hd/z", "z", 1), FL_OK);
  g_t = T0 + 500;
  TT_ASSERT_EQ(fl_fs_unlink(v.fs, "/hd/z"), FL_OK);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);

  TT_ASSERT(temps(img, "/h.txt", "mtime", T0 + 100));
  TT_ASSERT(temps(img, "/h.txt", "ctime", T0 + 100));
  TT_ASSERT(temps(img, "/h.txt", "crtime", T0));
  TT_ASSERT(temps(img, "/", "mtime", T0));
  TT_ASSERT(temps(img, "/hd/t.txt", "mtime", T0 + 160));
  TT_ASSERT(temps(img, "/hd/t.txt", "ctime", T0 + 160));
  TT_ASSERT(temps(img, "/hd/t.txt", "crtime", T0 + 150));
  TT_ASSERT(temps(img, "/hd/sous", "mtime", T0 + 300));
  TT_ASSERT(temps(img, "/hd/y", "ctime", T0 + 300));
  TT_ASSERT(temps(img, "/hd/y", "mtime", T0 + 200));
  TT_ASSERT(temps(img, "/hd", "mtime", T0 + 500));
  TT_ASSERT(temps(img, "/hd", "ctime", T0 + 500));

  /* Après 2038 : les bits d'époque des champs « extra ». */
  const int64_t t2 = (int64_t)1 << 32 | 12345; /* 2106 */
  g_t = t2;
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/2106.txt", "2106", 4), FL_OK);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/2106.txt", &st), FL_OK);
  TT_ASSERT_EQ(st.mtime, t2);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  g_t = T0;
  TT_ASSERT(temps(img, "/2106.txt", "mtime", (uint64_t)t2));
  TT_ASSERT(temps(img, "/2106.txt", "crtime", (uint64_t)t2));

  tm *m = tm_clone(g_base);
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(tm_apply_file(m, "/h.txt", "abc", 3, 0, 0, 0644), 0);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/hd", 0, 0, 0755), 0);
  TT_ASSERT_EQ(tm_apply_file(m, "/hd/t.txt", "0123", 4, 0, 0, 0644), 0);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/hd/sous", 0, 0, 0755), 0);
  TT_ASSERT_EQ(tm_apply_file(m, "/hd/y", "x", 1, 0, 0, 0644), 0);
  TT_ASSERT_EQ(tm_apply_file(m, "/2106.txt", "2106", 4, 0, 0, 0644), 0);
  TT_ASSERT_EQ(juger(img, m), 0);
  tm_free(m);
  unlink(img);
}

/* ext4-rw-reel.img (256 Mio, 228 Mio libres) ne peut pas recevoir 300 Mio : ext4-rw-gros.img (8 Gio
 * creux, mêmes options) sert aux gros fichiers. */
static void test_ecriture_gros_fichier(void) {
  char img[4096];
  vol v;
  const size_t n = 300 * MIB;
  TT_ASSERT(copie_de("ext4-rw-gros.img", img, sizeof img) == 0);
  uint8_t *data = malloc(n);
  TT_ASSERT(data != NULL);
  remplir(data, n, 300);
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  TT_ASSERT(en_ecriture(v.fs));
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/gros300.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  for (size_t o = 0; o < n; o += MIB) TT_ASSERT_EQ(fl_file_pwrite(f, data + o, MIB, o), (int64_t)MIB);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/gros300.bin", FL_O_RDONLY, &f), FL_OK);
  TT_ASSERT(relire(f, 0, data, n));
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  tm *m = tm_clone(g_base);
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(tm_apply_file(m, "/gros300.bin", data, n, 0, 0, 0644), 0); /* sha256 par tranches */
  free(data);
  TT_ASSERT_EQ(juger(img, m), 0);
  tm_free(m);
  unlink(img);
}

static void test_ecriture_trou(void) {
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  const size_t dix = 10 * MIB, nfin = 1000;
  uint8_t fin[1000];
  remplir(fin, nfin, 10);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/trou.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pwrite(f, fin, nfin, dix), (int64_t)nfin);
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/trou.bin", &st), FL_OK);
  TT_ASSERT_EQ(st.size, dix + nfin);
  TT_ASSERT(relire(f, 0, NULL, dix));
  TT_ASSERT(relire(f, dix, fin, nfin));
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);

  /* Écriture non alignée dans un trou, après un bloc partiel. */
  uint8_t a[100], b[50];
  remplir(a, sizeof a, 11);
  remplir(b, sizeof b, 12);
  const size_t ob = 3 * MIB + 123;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/trou2.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pwrite(f, a, sizeof a, 0), (int64_t)sizeof a);
  TT_ASSERT_EQ(fl_file_pwrite(f, b, sizeof b, ob), (int64_t)sizeof b);
  TT_ASSERT(relire(f, 0, a, sizeof a));
  TT_ASSERT(relire(f, sizeof a, NULL, ob - sizeof a));
  TT_ASSERT(relire(f, ob, b, sizeof b));
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);

  /* La fin d'un dernier bloc raccourci se relit en zéros quand le fichier regrandit. */
  uint8_t aa[8192];
  memset(aa, 0xAA, sizeof aa);
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/trou3.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pwrite(f, aa, sizeof aa, 0), (int64_t)sizeof aa);
  TT_ASSERT_EQ(fl_file_truncate(f, 100), FL_OK);
  TT_ASSERT_EQ(fl_file_pwrite(f, "z", 1, 20000), 1);
  TT_ASSERT(relire(f, 0, aa, 100));
  TT_ASSERT(relire(f, 100, NULL, 20000 - 100));
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);

  unsigned long long blocs = 0;
  TT_ASSERT(stat_champ(img, "/trou.bin", "Blockcount: ", &blocs));
  TT_ASSERT(blocs * 512 < MIB); /* creux : moins de 1 Mio alloué */
  uint8_t *tout = calloc(1, dix + nfin);
  TT_ASSERT(tout != NULL);
  memcpy(tout + dix, fin, nfin);
  tm *m = tm_clone(g_base);
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(tm_apply_file(m, "/trou.bin", tout, dix + nfin, 0, 0, 0644), 0);
  memset(tout, 0, ob + sizeof b);
  memcpy(tout, a, sizeof a);
  memcpy(tout + ob, b, sizeof b);
  TT_ASSERT_EQ(tm_apply_file(m, "/trou2.bin", tout, ob + sizeof b, 0, 0, 0644), 0);
  memset(tout, 0, 20001);
  memset(tout, 0xAA, 100);
  tout[20000] = 'z';
  TT_ASSERT_EQ(tm_apply_file(m, "/trou3.bin", tout, 20001, 0, 0, 0644), 0);
  free(tout);
  TT_ASSERT_EQ(juger(img, m), 0);
  tm_free(m);
  unlink(img);
}

static void test_ecriture_troncature(void) {
  char img[4096];
  vol v;
  const size_t n = 300 * MIB;
  TT_ASSERT(copie_de("ext4-rw-gros.img", img, sizeof img) == 0);
  uint8_t *data = malloc(n);
  TT_ASSERT(data != NULL);
  remplir(data, n, 7);
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  fl_file *f = NULL;
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/t.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  for (size_t o = 0; o < n; o += MIB) TT_ASSERT_EQ(fl_file_pwrite(f, data + o, MIB, o), (int64_t)MIB);
  TT_ASSERT_EQ(fl_file_truncate(f, 10 * MIB), FL_OK);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/t.bin", &st), FL_OK);
  TT_ASSERT_EQ(st.size, 10 * MIB);
  TT_ASSERT(relire(f, 0, data, 10 * MIB));
  TT_ASSERT_EQ(fl_file_pread(f, data, 1, 10 * MIB), 0);
  TT_ASSERT_EQ(fl_file_truncate(f, 50 * MIB), FL_OK); /* vers le haut : creux */
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/t.bin", &st), FL_OK);
  TT_ASSERT_EQ(st.size, 50 * MIB);
  remplir(data, 10 * MIB, 7); /* data a été écrasé par le pread d'un octet */
  TT_ASSERT(relire(f, 0, data, 10 * MIB));
  TT_ASSERT(relire(f, 10 * MIB, NULL, 40 * MIB));
  TT_ASSERT_EQ(fl_file_truncate(f, 0), FL_OK);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/t.bin", &st), FL_OK);
  TT_ASSERT_EQ(st.size, 0);
  TT_ASSERT_EQ(fl_file_truncate(f, 4097), FL_OK);
  TT_ASSERT(relire(f, 0, NULL, 4097));
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  /* Non aligné : 8192 → 100 → 8192. */
  uint8_t aa[8192];
  memset(aa, 0xAA, sizeof aa);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/t2.bin", aa, sizeof aa), FL_OK);
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/t2.bin", FL_O_RDWR, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_truncate(f, 100), FL_OK);
  TT_ASSERT_EQ(fl_file_truncate(f, 8192), FL_OK);
  TT_ASSERT(relire(f, 0, aa, 100));
  TT_ASSERT(relire(f, 100, NULL, 8192 - 100));
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  /* FL_O_TRUNC à l'ouverture. */
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/t3.bin", aa, sizeof aa), FL_OK);
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/t3.bin", FL_O_RDWR | FL_O_TRUNC, &f), FL_OK);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/t3.bin", &st), FL_OK);
  TT_ASSERT_EQ(st.size, 0);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);

  unsigned long long blocs = 0;
  TT_ASSERT(stat_champ(img, "/t.bin", "Blockcount: ", &blocs));
  TT_ASSERT(blocs * 512 <= 8192);
  tm *m = tm_clone(g_base);
  TT_ASSERT(m != NULL);
  memset(data, 0, 8192);
  TT_ASSERT_EQ(tm_apply_file(m, "/t.bin", data, 4097, 0, 0, 0644), 0);
  memset(data, 0xAA, 100);
  TT_ASSERT_EQ(tm_apply_file(m, "/t2.bin", data, 8192, 0, 0, 0644), 0);
  TT_ASSERT_EQ(tm_apply_file(m, "/t3.bin", data, 0, 0, 0, 0644), 0);
  free(data);
  TT_ASSERT_EQ(juger(img, m), 0);
  tm_free(m);
  unlink(img);
}

static void test_ecriture_dossier_vide(void) {
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/vide-d"), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/a"), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/a/b"), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/a/b/c"), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/a"), FL_EEXIST);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/hello.txt"), FL_EEXIST);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/"), FL_EEXIST);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/x/y"), FL_ENOENT); /* jamais de création intermédiaire */
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/hello.txt/y"), FL_ENOTDIR);
  fl_file *f = (fl_file *)0x1;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/x/y.txt", FL_O_RDWR | FL_O_CREAT, &f), FL_ENOENT);
  TT_ASSERT(f == NULL);
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/hello.txt/y.txt", FL_O_RDWR | FL_O_CREAT, &f), FL_ENOTDIR);
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/a", FL_O_RDWR | FL_O_CREAT, &f), FL_EISDIR);
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/x", &st), FL_ENOENT);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  tm *m = tm_clone(g_base);
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/vide-d", 0, 0, 0755), 0);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/a", 0, 0, 0755), 0);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/a/b", 0, 0, 0755), 0);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/a/b/c", 0, 0, 0755), 0);
  TT_ASSERT_EQ(juger(img, m), 0); /* défaut de somme de l'essai : un dossier resté vide */
  tm_free(m);
  unlink(img);
}

static void nom_3000(char *buf, size_t cap, int i) {
  snprintf(buf, cap, "/d3000/entree_avec_un_nom_assez_long_%05d.txt", i);
}

static void test_ecriture_3000(void) {
  char img[4096], p[256], out[1 << 14];
  vol v;
  uint8_t data[7 * 37];
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/d3000"), FL_OK);
  tm *m = tm_clone(g_base);
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/d3000", 0, 0, 0755), 0);
  for (int i = 0; i < 3000; i++) {
    nom_3000(p, sizeof p, i);
    size_t n = (size_t)(i % 7) * 37;
    remplir(data, n, 5000 + (uint64_t)i);
    TT_ASSERT_EQ(ecrire_fichier(v.fs, p, data, n), FL_OK);
    if (i % 2) TT_ASSERT_EQ(tm_apply_file(m, p, data, n, 0, 0, 0644), 0);
  }
  for (int i = 0; i < 3000; i += 2) {
    nom_3000(p, sizeof p, i);
    TT_ASSERT_EQ(fl_fs_unlink(v.fs, p), FL_OK);
  }
  fl_stat st;
  nom_3000(p, sizeof p, 2);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, p, &st), FL_ENOENT);
  nom_3000(p, sizeof p, 2999);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, p, &st), FL_OK);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(debugfs_f(img, false, "htree /d3000\n", out, sizeof out), 0);
  TT_ASSERT(strstr(out, "Indirect levels") != NULL); /* dossier indexé */
  TT_ASSERT_EQ(juger(img, m), 0);
  tm_free(m);
  unlink(img);
}

/* Numéro d'inode de l'entrée « .. » de `dossier`, d'après « debugfs ls -p ». */
static bool point_point(const char *img, const char *dossier, unsigned long *ino) {
  static char out[1 << 16], cmd[4200];
  snprintf(cmd, sizeof cmd, "ls -p \"%s\"\n", dossier);
  if (debugfs_f(img, false, cmd, out, sizeof out) != 0) return false;
  for (char *l = strtok(out, "\n"); l; l = strtok(NULL, "\n")) {
    unsigned long i, mode, uid, gid;
    int off = 0;
    if (sscanf(l, "/%lu/%lo/%lu/%lu/%n", &i, &mode, &uid, &gid, &off) == 4 && off && !strncmp(l + off, "../", 3)) {
      *ino = i;
      return true;
    }
  }
  return false;
}

static void test_ecriture_renommage(void) {
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/r"), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/r2"), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/r/sub"), FL_OK);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/r/a", "aaaa", 4), FL_OK);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/r/sub/inner", "in", 2), FL_OK);
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/r/a", "/r/b"), FL_OK);          /* dans un dossier */
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/r/b", "/r2/b"), FL_OK);         /* entre dossiers */
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/r/sub", "/r2/sub"), FL_OK);     /* dossier vers un autre parent */
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/r2/b", "/r2/sub/inner"), FL_EEXIST); /* pas de remplacement */
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/r2/b", "/r2/sub"), FL_EEXIST);
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/r2", "/r2/sub/x"), FL_EINVAL);   /* sous lui-même */
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/r2", "/r2"), FL_EEXIST);
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/", "/x"), FL_EINVAL);
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/absent", "/x"), FL_ENOENT);
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/r2/b", "/absent/b"), FL_ENOENT);
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/r2/b", "/hello.txt/b"), FL_ENOTDIR);
  /* Entrées créées par mkfs : un fichier, et un dossier de 300 fichiers. */
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/hello.txt", "/sous-dossier/hello.txt"), FL_OK);
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/beaucoup", "/r/beaucoup"), FL_OK);
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/r2/sub/inner", &st), FL_OK);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/r/beaucoup/f123.txt", &st), FL_OK);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/r/sub", &st), FL_ENOENT);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);

  unsigned long dd = 0;
  unsigned long long ino_r2 = 0, ino_r = 0, liens = 0;
  TT_ASSERT(stat_champ(img, "/r2", "Inode: ", &ino_r2));
  TT_ASSERT(point_point(img, "/r2/sub", &dd));
  TT_ASSERT_EQ(dd, ino_r2); /* « .. » suit le dossier */
  TT_ASSERT(stat_champ(img, "/r", "Inode: ", &ino_r));
  TT_ASSERT(point_point(img, "/r/beaucoup", &dd));
  TT_ASSERT_EQ(dd, ino_r);
  TT_ASSERT(stat_champ(img, "/r2", "Links: ", &liens));
  TT_ASSERT_EQ(liens, 3); /* ., .. de sub, entrée dans / */
  TT_ASSERT(stat_champ(img, "/r", "Links: ", &liens));
  TT_ASSERT_EQ(liens, 3); /* beaucoup est arrivé, sub est parti */

  tm *m = tm_clone(g_base);
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/r", 0, 0, 0755), 0);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/r2", 0, 0, 0755), 0);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/r2/sub", 0, 0, 0755), 0);
  TT_ASSERT_EQ(tm_apply_file(m, "/r2/b", "aaaa", 4, 0, 0, 0644), 0);
  TT_ASSERT_EQ(tm_apply_file(m, "/r2/sub/inner", "in", 2, 0, 0, 0644), 0);
  TT_ASSERT_EQ(tm_apply_rename(m, "/hello.txt", "/sous-dossier/hello.txt"), 0);
  TT_ASSERT_EQ(tm_apply_rename(m, "/beaucoup", "/r/beaucoup"), 0);
  TT_ASSERT_EQ(juger(img, m), 0);
  tm_free(m);
  unlink(img);
}

static int cb_noms(void *ctx, const fl_dirent *e) {
  char ***noms = ctx;
  size_t k = 0;
  while ((*noms)[k]) k++;
  char **nv = realloc(*noms, (k + 2) * sizeof *nv);
  if (!nv) return 1;
  nv[k] = strdup(e->name);
  nv[k + 1] = NULL;
  *noms = nv;
  return 0;
}

static void test_ecriture_suppression(void) {
  char img[4096], out[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(debugfs_f(img, true, "symlink /lien /hello.txt\n", out, sizeof out), 0);
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/lien", &st), FL_OK);
  TT_ASSERT_EQ(st.type, FL_FT_SYMLINK);
  TT_ASSERT_EQ(fl_fs_unlink(v.fs, "/sous-dossier"), FL_EISDIR);
  TT_ASSERT_EQ(fl_fs_unlink(v.fs, "/"), FL_EISDIR);
  TT_ASSERT_EQ(fl_fs_rmdir(v.fs, "/sous-dossier"), FL_ENOTEMPTY);
  TT_ASSERT_EQ(fl_fs_rmdir(v.fs, "/"), FL_EINVAL);
  TT_ASSERT_EQ(fl_fs_rmdir(v.fs, "/hello.txt"), FL_ENOTDIR);
  TT_ASSERT_EQ(fl_fs_unlink(v.fs, "/absent"), FL_ENOENT);
  TT_ASSERT_EQ(fl_fs_rmdir(v.fs, "/absent"), FL_ENOENT);
  TT_ASSERT_EQ(fl_fs_unlink(v.fs, "/lien"), FL_OK); /* un lien */
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/hello.txt", &st), FL_OK); /* sa cible reste */
  /* Un fichier ouvert n'est pas supprimé (lwext4 libérerait son inode sous le descripteur). */
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/hello.txt", FL_O_RDONLY, &f), FL_OK);
  TT_ASSERT_EQ(fl_fs_unlink(v.fs, "/hello.txt"), FL_EBUSY);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_EQ(fl_fs_unlink(v.fs, "/hello.txt"), FL_OK);
  /* Vidage puis rmdir. */
  TT_ASSERT_EQ(fl_fs_unlink(v.fs, "/sous-dossier/données.bin"), FL_OK);
  TT_ASSERT_EQ(fl_fs_rmdir(v.fs, "/sous-dossier"), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/e"), FL_OK);
  TT_ASSERT_EQ(fl_fs_rmdir(v.fs, "/e"), FL_OK);
  char **noms = calloc(1, sizeof *noms);
  TT_ASSERT(noms != NULL);
  TT_ASSERT_EQ(fl_fs_readdir(v.fs, "/beaucoup", cb_noms, &noms), FL_OK);
  size_t k = 0;
  for (; noms[k]; k++) {
    char p[512];
    snprintf(p, sizeof p, "/beaucoup/%s", noms[k]);
    TT_ASSERT_EQ(fl_fs_unlink(v.fs, p), FL_OK);
    free(noms[k]);
  }
  free(noms);
  TT_ASSERT_EQ(k, 300);
  TT_ASSERT_EQ(fl_fs_rmdir(v.fs, "/beaucoup"), FL_OK);
  TT_ASSERT_EQ(fl_fs_unlink(v.fs, "/clairsemé.bin"), FL_OK); /* 5 Gio creux */
  TT_ASSERT_EQ(fl_fs_unlink(v.fs, "/gros.bin"), FL_OK);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/beaucoup", &st), FL_ENOENT);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);

  tm *m = tm_clone(g_base);
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(tm_apply_unlink(m, "/hello.txt"), 0);
  TT_ASSERT_EQ(tm_apply_unlink(m, "/sous-dossier/données.bin"), 0);
  TT_ASSERT_EQ(tm_apply_rmdir(m, "/sous-dossier"), 0);
  for (int i = 0; i < 300; i++) {
    char p[64];
    snprintf(p, sizeof p, "/beaucoup/f%03d.txt", i);
    TT_ASSERT_EQ(tm_apply_unlink(m, p), 0);
  }
  TT_ASSERT_EQ(tm_apply_rmdir(m, "/beaucoup"), 0);
  TT_ASSERT_EQ(tm_apply_unlink(m, "/clairsemé.bin"), 0);
  TT_ASSERT_EQ(tm_apply_unlink(m, "/gros.bin"), 0);
  TT_ASSERT_EQ(juger(img, m), 0);
  tm_free(m);
  unlink(img);
}

static void test_ecriture_noms_unicode(void) {
  char img[4096], n255[300], u255[300], p[700];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  memset(n255, 'x', 255);
  n255[255] = 0;
  u255[0] = 0;
  for (int i = 0; i < 127; i++) strcat(u255, "\xc3\xa9");
  strcat(u255, "a"); /* 255 octets UTF-8 */
  TT_ASSERT_EQ(strlen(u255), 255);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/u"), FL_OK);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/u/caf\xc3\xa9_\xc3\xa0\xc3\xa7\xc3\xa8\xc3\xaa.txt", "1", 1), FL_OK);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/u/emoji_\xf0\x9f\x98\x80\xf0\x9f\x8e\x89.txt", "22", 2), FL_OK);
  snprintf(p, sizeof p, "/u/%s", n255);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, p, "333", 3), FL_OK);
  snprintf(p, sizeof p, "/u/%s", u255);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, p), FL_OK);
  snprintf(p, sizeof p, "/u/%s/%s", u255, n255);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, p, "4444", 4), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/u/dossier \xe2\x98\x80 \xe6\x97\xa5\xe6\x9c\xac"), FL_OK);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/u/dossier \xe2\x98\x80 \xe6\x97\xa5\xe6\x9c\xac/f.txt", "55555", 5), FL_OK);
  /* 256 octets : refusé, rien n'est créé. */
  char n256[300];
  memset(n256, 'y', 256);
  n256[256] = 0;
  snprintf(p, sizeof p, "/u/%s", n256);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, p, FL_O_RDWR | FL_O_CREAT, &f), FL_EINVAL);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, p), FL_EINVAL);
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/u/caf\xc3\xa9_\xc3\xa0\xc3\xa7\xc3\xa8\xc3\xaa.txt", p), FL_EINVAL);
  snprintf(p, sizeof p, "/u/%s", n255);
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_stat(v.fs, p, &st), FL_OK);
  TT_ASSERT_EQ(st.size, 3);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  tm *m = tm_clone(g_base);
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/u", 0, 0, 0755), 0);
  TT_ASSERT_EQ(tm_apply_file(m, "/u/caf\xc3\xa9_\xc3\xa0\xc3\xa7\xc3\xa8\xc3\xaa.txt", "1", 1, 0, 0, 0644), 0);
  TT_ASSERT_EQ(tm_apply_file(m, "/u/emoji_\xf0\x9f\x98\x80\xf0\x9f\x8e\x89.txt", "22", 2, 0, 0, 0644), 0);
  snprintf(p, sizeof p, "/u/%s", n255);
  TT_ASSERT_EQ(tm_apply_file(m, p, "333", 3, 0, 0, 0644), 0);
  snprintf(p, sizeof p, "/u/%s", u255);
  TT_ASSERT_EQ(tm_apply_mkdir(m, p, 0, 0, 0755), 0);
  snprintf(p, sizeof p, "/u/%s/%s", u255, n255);
  TT_ASSERT_EQ(tm_apply_file(m, p, "4444", 4, 0, 0, 0644), 0);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/u/dossier \xe2\x98\x80 \xe6\x97\xa5\xe6\x9c\xac", 0, 0, 0755), 0);
  TT_ASSERT_EQ(tm_apply_file(m, "/u/dossier \xe2\x98\x80 \xe6\x97\xa5\xe6\x9c\xac/f.txt", "55555", 5, 0, 0, 0644), 0);
  TT_ASSERT_EQ(juger(img, m), 0);
  tm_free(m);
  unlink(img);
}

/* ---------- concurrence ---------- */

#define NFICH_MAX 4096

typedef struct {
  fl_fs *fs;
  int id;
  time_t fin;
  int erreurs;
  int n; /* fichiers écrits (écrivains) ou lectures (lecteurs) */
} fil_ctx;

static uint8_t motif_gros(uint64_t i) { return (uint8_t)((i * 31 + 7) & 0xFF); }

static size_t taille_conc(int id, int n) { return (size_t)((n * 7919 + id * 104729) % 200000) + 1; }

static void *lecteur(void *p) {
  fil_ctx *c = p;
  uint8_t *b = malloc(65536);
  uint64_t x = 0x1234567ull + (uint64_t)c->id;
  if (!b) {
    c->erreurs++;
    return NULL;
  }
  while (time(NULL) < c->fin) {
    fl_file *f = NULL;
    if (fl_fs_open(c->fs, "/gros.bin", FL_O_RDONLY, &f) != FL_OK) {
      c->erreurs++;
      break;
    }
    for (int k = 0; k < 16; k++) {
      x ^= x << 13;
      x ^= x >> 7;
      x ^= x << 17;
      uint64_t off = x % (5u << 20);
      size_t len = 1 + (size_t)(x >> 40) % 65536;
      if (off + len > (5u << 20)) len = (size_t)((5u << 20) - off);
      if (fl_file_pread(f, b, len, off) != (int64_t)len) c->erreurs++;
      for (size_t i = 0; i < len; i++)
        if (b[i] != motif_gros(off + i)) {
          c->erreurs++;
          break;
        }
      c->n++;
    }
    fl_file_close(f);
  }
  free(b);
  return NULL;
}

static void *ecrivain(void *p) {
  fil_ctx *c = p;
  uint8_t *b = malloc(200001), *r = malloc(200001);
  if (!b || !r) {
    c->erreurs++;
    free(b);
    free(r);
    return NULL;
  }
  while (time(NULL) < c->fin && c->n < NFICH_MAX) {
    char chemin[64];
    snprintf(chemin, sizeof chemin, "/conc-%d-%04d", c->id, c->n);
    size_t n = taille_conc(c->id, c->n);
    remplir(b, n, (uint64_t)c->id * 100000 + (uint64_t)c->n);
    fl_file *f = NULL;
    if (fl_fs_open(c->fs, chemin, FL_O_RDWR | FL_O_CREAT, &f) != FL_OK) {
      c->erreurs++;
      break;
    }
    /* en trois morceaux, pour entrelacer avec les autres fils */
    size_t t1 = n / 3, t2 = 2 * n / 3;
    if (fl_file_pwrite(f, b, t1, 0) != (int64_t)t1) c->erreurs++;
    if (fl_file_pwrite(f, b + t2, n - t2, t2) != (int64_t)(n - t2)) c->erreurs++;
    if (fl_file_pwrite(f, b + t1, t2 - t1, t1) != (int64_t)(t2 - t1)) c->erreurs++;
    if (fl_file_pread(f, r, n, 0) != (int64_t)n || memcmp(r, b, n) != 0) c->erreurs++;
    if (fl_file_close(f) != FL_OK) c->erreurs++;
    c->n++;
  }
  free(b);
  free(r);
  return NULL;
}

static void test_ext4_ecriture_concurrente(void) {
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  fil_ctx c[4];
  pthread_t t[4];
  time_t fin = time(NULL) + 3;
  alarm(60); /* un interblocage tue le test */
  for (int i = 0; i < 4; i++) {
    c[i] = (fil_ctx){.fs = v.fs, .id = i, .fin = fin};
    TT_ASSERT_EQ(pthread_create(&t[i], NULL, i < 2 ? lecteur : ecrivain, &c[i]), 0);
  }
  for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
  alarm(0);
  for (int i = 0; i < 4; i++) {
    if (c[i].erreurs) fprintf(stderr, "fil %d : %d erreurs\n", i, c[i].erreurs);
    TT_ASSERT_EQ(c[i].erreurs, 0);
    TT_ASSERT(c[i].n > 0);
  }
  tm *m = tm_clone(g_base);
  TT_ASSERT(m != NULL);
  uint8_t *b = malloc(200001);
  TT_ASSERT(b != NULL);
  for (int w = 2; w < 4; w++)
    for (int k = 0; k < c[w].n; k++) {
      char chemin[64];
      snprintf(chemin, sizeof chemin, "/conc-%d-%04d", w, k);
      size_t n = taille_conc(w, k);
      remplir(b, n, (uint64_t)w * 100000 + (uint64_t)k);
      TT_ASSERT_EQ(tm_apply_file(m, chemin, b, n, 0, 0, 0644), 0);
    }
  free(b);
  fprintf(stderr, "concurrence : %d + %d lectures, %d + %d fichiers\n", c[0].n, c[1].n, c[2].n, c[3].n);
  TT_ASSERT_EQ(tm_check_fl(m, v.fs), 0);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(juger(img, m), 0);
  tm_free(m);
  unlink(img);
}

/* ---------- lecture seule ---------- */

static bool identique(const char *fixture, const char *copie) {
  char out[4096], src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture(fixture));
  char *const argv[] = {(char *)"cmp", src, (char *)copie, NULL};
  int rc = fu_run(argv, out, sizeof out);
  if (rc != 0) fprintf(stderr, "%s modifiée : %s\n", fixture, out);
  return rc == 0;
}

static void toutes_les_ecritures_erofs(fl_fs *fs) {
  fl_file *f = (fl_file *)0x1;
  TT_ASSERT_EQ(fl_fs_open(fs, "/hello.txt", FL_O_RDWR, &f), FL_EROFS);
  TT_ASSERT(f == NULL);
  TT_ASSERT_EQ(fl_fs_open(fs, "/hello.txt", FL_O_TRUNC, &f), FL_EROFS);
  TT_ASSERT_EQ(fl_fs_open(fs, "/nouveau.txt", FL_O_CREAT, &f), FL_EROFS);
  TT_ASSERT_EQ(fl_fs_open(fs, "/nouveau.txt", FL_O_RDWR | FL_O_CREAT, &f), FL_EROFS);
  TT_ASSERT_EQ(fl_fs_mkdir(fs, "/nouveau"), FL_EROFS);
  TT_ASSERT_EQ(fl_fs_unlink(fs, "/hello.txt"), FL_EROFS);
  TT_ASSERT_EQ(fl_fs_rmdir(fs, "/sous-dossier"), FL_EROFS);
  TT_ASSERT_EQ(fl_fs_rename(fs, "/hello.txt", "/h.txt"), FL_EROFS);
  TT_ASSERT_EQ(fl_fs_sync(fs), FL_OK);
  TT_ASSERT_EQ(fl_fs_open(fs, "/hello.txt", FL_O_RDONLY, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pwrite(f, "x", 1, 0), FL_EROFS);
  TT_ASSERT_EQ(fl_file_truncate(f, 0), FL_EROFS);
  char b[16];
  TT_ASSERT_EQ(fl_file_pread(f, b, 16, 0), 16);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
}

static void test_ecriture_lecture_seule_api(void) {
  char img[4096];
  vol v;
  /* want_rw = false, sur un périphérique ouvert en écriture : rien n'est écrit. */
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, false, false), FL_OK);
  TT_ASSERT(lecture_seule(v.fs, FL_RO_USER));
  toutes_les_ecritures_erofs(v.fs);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT(identique("ext4-rw-reel.img", img));
  /* want_rw sur un périphérique en lecture seule. */
  TT_ASSERT_EQ(vol_monter(&v, img, true, true), FL_OK);
  TT_ASSERT(lecture_seule(v.fs, FL_RO_WP));
  toutes_les_ecritures_erofs(v.fs);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  unlink(img);
  /* Sans journal. */
  TT_ASSERT(copie_de("ext4-rw-sans-journal.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  TT_ASSERT(lecture_seule(v.fs, FL_RO_NOJOURNAL));
  toutes_les_ecritures_erofs(v.fs);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT(identique("ext4-rw-sans-journal.img", img));
  unlink(img);
  /* Sans extents (ext3 et anciens ext4) : écrire un fichier troué viserait le bloc 0. */
  TT_ASSERT(copie_de("ext4-indirect.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  TT_ASSERT(lecture_seule(v.fs, FL_RO_FEATURES));
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/nouveau"), FL_EROFS);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT(identique("ext4-indirect.img", img));
  unlink(img);
}

static void test_ecriture_sync_flush(void) {
  char img[4096];
  vol v;
  memset(&v, 0, sizeof v);
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(fl_file_bdev_open(img, 512, false, &v.file), FL_OK);
  TT_ASSERT_EQ(fl_fault_bdev_new(v.file, UINT64_MAX, FL_EIO, &v.dev), FL_OK);
  TT_ASSERT_EQ(vol_monter_sur(&v, true), FL_OK);
  TT_ASSERT(en_ecriture(v.fs));
  fl_bdev *fd = v.dev;
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/sync.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  uint8_t b[4096];
  remplir(b, sizeof b, 4);
  TT_ASSERT_EQ(fl_file_pwrite(f, b, sizeof b, 0), (int64_t)sizeof b);
  uint64_t avant = fl_fault_bdev_flushes(fd);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT(fl_fault_bdev_flushes(fd) >= avant + 1);
  avant = fl_fault_bdev_flushes(fd);
  TT_ASSERT_EQ(fl_fs_sync(v.fs), FL_OK);
  TT_ASSERT(fl_fault_bdev_flushes(fd) >= avant + 1);
  /* Un fichier seulement lu ne déclenche pas de sync à la fermeture. */
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/sync.bin", FL_O_RDONLY, &f), FL_OK);
  avant = fl_fault_bdev_flushes(fd);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_EQ(fl_fault_bdev_flushes(fd), avant);
  TT_ASSERT_EQ(fl_fs_unmount(v.fs), FL_OK);
  TT_ASSERT(fl_fault_bdev_flushes(fd) >= avant + 1); /* le démontage vide aussi */
  fl_bdev_close(v.dev);
  fl_bdev_close(v.file);
  tm *m = tm_clone(g_base);
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(tm_apply_file(m, "/sync.bin", b, sizeof b, 0, 0, 0644), 0);
  TT_ASSERT_EQ(juger(img, m), 0);
  tm_free(m);
  unlink(img);
}

/* ---------- défauts lwext4 hérités de T1 ---------- */

/* Périphérique qui compte les accès au bloc 0 du disque hors superbloc ([0, 1024) et [2048, 4096)). */
typedef struct {
  fl_bdev *parent;
  int bas;
} garde;

static void garde_compter(fl_bdev *d, uint64_t lba, uint32_t count) {
  garde *g = d->priv;
  uint64_t a = lba * 512, b = a + (uint64_t)count * 512;
  if (count && ((a < 1024) || (a < 4096 && b > 2048))) g->bas++;
}
static int garde_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  garde_compter(d, lba, count);
  return fl_bdev_read(((garde *)d->priv)->parent, lba, count, buf);
}
static int garde_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf) {
  garde_compter(d, lba, count);
  return fl_bdev_write(((garde *)d->priv)->parent, lba, count, buf);
}
static int garde_flush(fl_bdev *d) { return fl_bdev_flush(((garde *)d->priv)->parent); }
static void garde_destroy(fl_bdev *d) { (void)d; }
static const fl_bdev_ops garde_ops = {garde_read, garde_write, garde_flush, garde_destroy};

/* (e) Ajout d'entrée dans un dossier troué, par l'API publique (T1 le faisait par lwext4). */
static void test_ecriture_trou_de_dossier_api(void) {
  char img[4096], longs[300];
  vol v;
  memset(&v, 0, sizeof v);
  TT_ASSERT(copie_de("ext4-trou-dossier.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(fl_file_bdev_open(img, 512, false, &v.file), FL_OK);
  garde g = {v.file, 0};
  fl_bdev gd = {&garde_ops, v.file->sector_size, v.file->sector_count, false, &g};
  v.dev = &gd;
  fl_fs *fs = NULL;
  TT_ASSERT_EQ(fl_fs_mount(v.dev, FL_CONTENT_EXT4, true, &fs), FL_OK);
  g.bas = 0; /* le montage lit les 1024 premiers octets à la recherche du superbloc */
  TT_ASSERT(en_ecriture(fs));
  /* /d : linéaire, trou au bloc 1 ; un nom de 200 octets ne tient pas dans le bloc 0. */
  memset(longs, 'L', 200);
  strcpy(longs + 200, ".txt");
  char p[400];
  snprintf(p, sizeof p, "/d/%s", longs);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(fs, p, FL_O_RDWR | FL_O_CREAT, &f), FL_ECORRUPT);
  TT_ASSERT_EQ(fl_fs_mkdir(fs, "/d/nouveau"), FL_ECORRUPT);
  /* /h : htree, trou au bloc 3 : le hachage choisit la feuille, on essaie jusqu'à y tomber. */
  int corrompu = 0, crees = 0;
  for (int i = 0; i < 200 && !corrompu; i++) {
    snprintf(p, sizeof p, "/h/nouveau-%d", i);
    int rc = fl_fs_mkdir(fs, p);
    if (rc == FL_ECORRUPT) corrompu++;
    else if (rc == FL_OK) crees++;
    else TT_ASSERT_EQ(rc, FL_OK);
  }
  TT_ASSERT(corrompu > 0);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(v.file);
  TT_ASSERT_EQ(g.bas, 0); /* jamais le bloc 0 du disque */
  fprintf(stderr, "trou de dossier : %d dossiers créés avant la feuille trouée\n", crees);
  unlink(img);
}

/* (a) Un index htree jugé mauvais : une recherche ne modifie rien (FL_ECORRUPT), une création non plus. */
static int indexer_et_abimer(const char *img, char *stat_avant, size_t cap) {
  char out[1 << 14];
  char *const fyd[] = {(char *)"e2fsck", (char *)"-fyD", (char *)img, NULL};
  int rc = fu_run(fyd, out, sizeof out);
  if (rc != 0 && rc != 1) return -1;
  unsigned long long fl = 0;
  if (!stat_champ(img, "/beaucoup", "Flags: ", &fl) || !(fl & 0x1000)) return -1; /* INDEX_FL */
  /* hash_version (octet 28 de la racine d'index, bloc logique 0) : 7, inconnu */
  if (debugfs_f(img, true, "zap_block -f /beaucoup -o 28 -l 1 -p 7 0\n", out, sizeof out) != 0) return -1;
  return debugfs_f(img, false, "stat /beaucoup\n", stat_avant, cap);
}

static void test_ecriture_htree_invalide_sans_modif(void) {
  static char avant[1 << 14], apres[1 << 14];
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(indexer_et_abimer(img, avant, sizeof avant), 0);
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  TT_ASSERT(en_ecriture(v.fs));
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/beaucoup/f001.txt", &st), FL_ECORRUPT);
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/beaucoup/absent", &st), FL_ECORRUPT);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/beaucoup/nouveau", FL_O_RDWR | FL_O_CREAT, &f), FL_ECORRUPT);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/beaucoup/nouveau"), FL_ECORRUPT);
  TT_ASSERT_EQ(fl_fs_unlink(v.fs, "/beaucoup/f001.txt"), FL_ECORRUPT);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(debugfs_f(img, false, "stat /beaucoup\n", apres, sizeof apres), 0);
  TT_ASSERT_STR_EQ(apres, avant); /* inode intact : drapeau INDEX, horodatages, tout */
  unlink(img);
}

/* (a), au niveau de lwext4 : ext4_dir_find_entry ne doit pas effacer le drapeau INDEX. */
static void test_interne_htree_invalide_recherche(void) {
  static char avant[1 << 14], apres[1 << 14];
  char img[4096];
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(indexer_et_abimer(img, avant, sizeof avant), 0);
  TT_ASSERT_EQ(lw_monter(img), EOK);
  uint32_t ino;
  struct ext4_inode in;
  int r = ext4_raw_inode_fill("/tt/beaucoup/f001.txt", &ino, &in);
  int u = lw_demonter();
  TT_ASSERT_EQ(r, EFSCORRUPTED);
  TT_ASSERT_EQ(u, EOK);
  TT_ASSERT_EQ(debugfs_f(img, false, "stat /beaucoup\n", apres, sizeof apres), 0);
  unsigned long long fl = 0;
  TT_ASSERT(champ(apres, "Flags: ", &fl));
  TT_ASSERT(fl & 0x1000);
  unlink(img);
}

/* (b) ext4_dir_try_insert_entry sur un bloc dont une entrée a une longueur nulle : boucle sans fin
 * avant le correctif. L'API publique est protégée par la recherche qui précède tout ajout. */
static void test_interne_ajout_longueur_nulle(void) {
  char img[4096], out[4096];
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  /* /sous-dossier : « . » (0), « .. » (12), « données.bin » (24) ; rec_len de la 3e entrée à 0 */
  TT_ASSERT_EQ(debugfs_f(img, true, "zap_block -f /sous-dossier -o 28 -l 2 -p 0 0\n", out, sizeof out), 0);
  vol v;
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/sous-dossier/nouveau", FL_O_RDWR | FL_O_CREAT, &f), FL_ECORRUPT);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(lw_monter(img), EOK);
  struct ext4_inode_ref dir, racine;
  alarm(20); /* boucle sans fin = échec */
  int r = lw_refs("/tt/sous-dossier", &dir, &racine);
  int ra = -1;
  if (r == EOK) {
    ra = ext4_dir_add_entry(&dir, "x", 1, &racine);
    ext4_fs_put_inode_ref(&racine);
    ext4_fs_put_inode_ref(&dir);
  }
  alarm(0);
  lw_demonter();
  TT_ASSERT_EQ(r, EOK);
  TT_ASSERT_EQ(ra, EFSCORRUPTED);
  unlink(img);
}

static void nom_2niv(char *buf, size_t cap, int i) {
  snprintf(buf, cap, "/d2/un_nom_de_soixante_octets_pour_remplir_les_feuilles_%05d", i);
}

static uint32_t le32_(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Bloc physique du bloc logique `lblk` de `chemin` (debugfs bmap). */
static uint64_t bmap(const char *img, const char *chemin, uint32_t lblk) {
  char cmd[4200], out[1024];
  snprintf(cmd, sizeof cmd, "bmap \"%s\" %u\n", chemin, lblk);
  if (debugfs_f(img, false, cmd, out, sizeof out) != 0) return 0;
  /* bannière de version, « debugfs: bmap ... », puis le numéro */
  const char *cmd_ = strstr(out, "debugfs: bmap");
  const char *nl = cmd_ ? strchr(cmd_, '\n') : NULL;
  if (!nl) return 0;
  return strtoull(nl + 1, NULL, 10);
}

/* (c) Index htree à deux niveaux, erreur au niveau du nœud fils : la racine doit être rendue une fois,
 * le fils une fois (compteurs de références du cache de blocs). */
static void test_interne_htree_2niveaux_erreur(void) {
  char img[4096], p[256], out[1 << 15];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-1k.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, true, false), FL_OK);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/d2"), FL_OK);
  for (int i = 0; i < 3000; i++) {
    nom_2niv(p, sizeof p, i);
    TT_ASSERT_EQ(ecrire_fichier(v.fs, p, NULL, 0), FL_OK);
  }
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(juger(img, NULL), 0); /* un htree à deux niveaux écrit par lwext4 : propre */
  TT_ASSERT_EQ(debugfs_f(img, false, "htree /d2\n", out, sizeof out), 0);
  TT_ASSERT(strstr(out, "Indirect levels: 1") != NULL);

  /* Racine (bloc logique 0) : climit en 32 (limit, count), entrées de 8 octets ; bloc de l'entrée i
   * en 36 + 8i. Chaque nœud fils : climit en 8 ; sa limite devient fausse. */
  uint64_t racine = bmap(img, "/d2", 0);
  TT_ASSERT(racine != 0);
  int fd = open(img, O_RDWR);
  TT_ASSERT(fd >= 0);
  uint8_t blk[1024];
  TT_ASSERT_EQ(pread(fd, blk, sizeof blk, (off_t)(racine * 1024)), (ssize_t)sizeof blk);
  TT_ASSERT_EQ(blk[30], 1); /* indirect_levels */
  unsigned cnt = blk[34] | blk[35] << 8;
  TT_ASSERT(cnt >= 2 && cnt <= 123);
  uint64_t fils[128];
  for (unsigned i = 0; i < cnt; i++) {
    fils[i] = bmap(img, "/d2", le32_(blk + 36 + 8 * i));
    TT_ASSERT(fils[i] != 0);
    uint8_t lim[2];
    TT_ASSERT_EQ(pread(fd, lim, 2, (off_t)(fils[i] * 1024 + 8)), 2);
    lim[0] ^= 0x01;
    TT_ASSERT_EQ(pwrite(fd, lim, 2, (off_t)(fils[i] * 1024 + 8)), 2);
  }
  close(fd);

  TT_ASSERT_EQ(lw_monter(img), EOK);
  uint32_t ino;
  struct ext4_inode in;
  int r = ext4_raw_inode_fill("/tt/d2/absent", &ino, &in);
  long rr = refs_cache(racine);
  long rf = 0;
  for (unsigned i = 0; i < cnt; i++) {
    long n = refs_cache(fils[i]);
    if (n != 0) rf = n;
  }
  int u = lw_demonter();
  TT_ASSERT_EQ(r, EFSCORRUPTED);
  TT_ASSERT_EQ(rr, 0);  /* avant : 1 (racine jamais rendue) */
  TT_ASSERT_EQ(rf, 0);  /* avant : -1 (fils rendu deux fois) */
  TT_ASSERT_EQ(u, EOK);
  unlink(img);
}

/* (d) rmdir d'un dossier vide d'un seul bloc sans extents : ext4_dir_rm déliait l'entrée, puis
 * ext4_trunc_dir échouait (ENOTSUP) et laissait le dossier délié mais alloué. */
static void test_interne_rmdir_sans_extents(void) {
  char td[4096], dir[4300], img[4400], vide[4400], out[1 << 14];
  TT_ASSERT_EQ(fu_tmpdir(td, sizeof td), 0);
  snprintf(dir, sizeof dir, "%s/rmdir-XXXXXX", td);
  TT_ASSERT(mkdtemp(dir) != NULL);
  snprintf(vide, sizeof vide, "%s/vide", dir);
  TT_ASSERT_EQ(mkdir(vide, 0755), 0);
  snprintf(img, sizeof img, "%s.img", dir);
  noter(img);
  int fd = open(img, O_RDWR | O_CREAT | O_TRUNC, 0644);
  TT_ASSERT(fd >= 0);
  TT_ASSERT_EQ(ftruncate(fd, 8 << 20), 0);
  close(fd);
  char *const mkfs[] = {(char *)"mkfs.ext4", (char *)"-q", (char *)"-F", (char *)"-T", (char *)"default",
                        (char *)"-b", (char *)"4096", (char *)"-I", (char *)"256", (char *)"-O",
                        (char *)"^extent,^64bit,^orphan_file,^metadata_csum_seed", (char *)"-d", dir, img, NULL};
  int rc = fu_run(mkfs, out, sizeof out);
  rmdir(vide);
  rmdir(dir);
  TT_ASSERT_EQ(rc, 0);
  TT_ASSERT_EQ(juger(img, NULL), 0);
  unsigned long long taille = 0;
  TT_ASSERT(stat_champ(img, "/vide", "Size: ", &taille));
  TT_ASSERT_EQ(taille, 4096);
  TT_ASSERT_EQ(lw_monter(img), EOK);
  int r = ext4_dir_rm("/tt/vide");
  int u = lw_demonter();
  TT_ASSERT_EQ(r, EOK);
  TT_ASSERT_EQ(u, EOK);
  TT_ASSERT_EQ(juger(img, NULL), 0);
  uint64_t x;
  TT_ASSERT(fu_debugfs_inode_field(img, "/vide", "mode", &x) != 0); /* parti */
  unlink(img);
}

int main(void) {
  if (!fu_tools_available()) {
    printf("SAUTÉ : e2fsck/debugfs/dumpe2fs absents ou plateforme Android (banc réservé au PC)\n");
    return 77; /* SKIP_RETURN_CODE de CTest */
  }
  fl_fs_set_clock(horloge);
  char tree[4096];
  snprintf(tree, sizeof tree, "%s", tt_fixture("tree"));
  g_base = tm_new();
  if (!g_base || tm_from_dir(g_base, tree) != 0) {
    fprintf(stderr, "modèle de tree/ impossible\n");
    return 1;
  }
  TT_RUN(test_ecriture_raisons);
  TT_RUN(test_ecriture_petit_fichier);
  TT_RUN(test_ecriture_proprietaire_herite);
  TT_RUN(test_ecriture_horodatages);
  TT_RUN(test_ecriture_gros_fichier);
  TT_RUN(test_ecriture_trou);
  TT_RUN(test_ecriture_troncature);
  TT_RUN(test_ecriture_dossier_vide);
  TT_RUN(test_ecriture_3000);
  TT_RUN(test_ecriture_renommage);
  TT_RUN(test_ecriture_suppression);
  TT_RUN(test_ecriture_noms_unicode);
  TT_RUN(test_ext4_ecriture_concurrente);
  TT_RUN(test_ecriture_lecture_seule_api);
  TT_RUN(test_ecriture_sync_flush);
  TT_RUN(test_ecriture_trou_de_dossier_api);
  TT_RUN(test_ecriture_htree_invalide_sans_modif);
  TT_RUN(test_interne_htree_invalide_recherche);
  TT_RUN(test_interne_ajout_longueur_nulle);
  TT_RUN(test_interne_htree_2niveaux_erreur);
  TT_RUN(test_interne_rmdir_sans_extents);
  tm_free(g_base);
  fl_fs_set_clock(NULL);
  nettoyer();
  TT_MAIN_END();
}
