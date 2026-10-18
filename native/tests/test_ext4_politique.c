#define _GNU_SOURCE
/* Règles de lecture seule du § 4.2 et montage sûr (phase 3, T6) : ext4_policy.c, correctif lwext4
 * 0005 (état du superbloc, tables d'inodes jamais remises à zéro, metadata_csum_seed, sommes fausses
 * bloquantes, NULL déréférencé), lecture seule collante en cours de session, disque mort.
 *
 * Chaque test part d'une copie d'une image du banc (T3), éventuellement retouchée par debugfs ; une
 * image refusée en écriture doit rester identique octet pour octet.
 *
 * Réservé au PC : sauté (code 77) si e2fsck/debugfs/dumpe2fs sont absents, et sur Android. */
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fault_bdev.h"
#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fs/ro_reasons.h"
#include "fsck_util.h"
#include "tt.h"

#define MIB ((size_t)1 << 20)

/* ---------- utilitaires ---------- */

static char *temporaires[64];
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

static int copie_de(const char *fixture, char *out, size_t cap) {
  char src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture(fixture));
  if (fu_copy_image(src, out, cap) != 0) return -1;
  noter(out);
  return 0;
}

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

static int64_t horloge(void) { return 1760000000LL; }

typedef struct {
  fl_bdev *file, *dev; /* dev : file, ou un fault_bdev au-dessus */
  fl_fs *fs;
} vol;

/* writes_ok = 0 : sans fault_bdev. */
static int vol_monter(vol *v, const char *img, uint64_t writes_ok, int err) {
  memset(v, 0, sizeof *v);
  int rc = fl_file_bdev_open(img, 512, false, &v->file);
  if (rc != FL_OK) return rc;
  v->dev = v->file;
  if (writes_ok && (rc = fl_fault_bdev_new(v->file, writes_ok, err, &v->dev)) != FL_OK) v->dev = v->file;
  if (rc == FL_OK) rc = fl_fs_mount(v->dev, FL_CONTENT_EXT4, true, &v->fs);
  if (rc != FL_OK) {
    if (v->dev != v->file) fl_bdev_close(v->dev);
    fl_bdev_close(v->file);
    memset(v, 0, sizeof *v);
  }
  return rc;
}

static int vol_demonter(vol *v) {
  int rc = fl_fs_unmount(v->fs);
  if (v->dev != v->file) fl_bdev_close(v->dev);
  fl_bdev_close(v->file);
  memset(v, 0, sizeof *v);
  return rc;
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

static int ecrire_fichier(fl_fs *fs, const char *chemin, const void *data, size_t n) {
  fl_file *f = NULL;
  int rc = fl_fs_open(fs, chemin, FL_O_RDWR | FL_O_CREAT | FL_O_TRUNC, &f);
  if (rc != FL_OK) return rc;
  int64_t w = n ? fl_file_pwrite(f, data, n, 0) : 0;
  rc = fl_file_close(f);
  if (w != (int64_t)n) return w < 0 ? (int)w : FL_EIO;
  return rc;
}

static int e2fsck_propre(const char *img) {
  static char out[1 << 16];
  int rc = fu_e2fsck_n(img, out, sizeof out);
  if (rc != 0) fprintf(stderr, "e2fsck -fn = %d\n%s\n", rc, out);
  return rc;
}

static bool identique(const char *fixture, const char *copie) {
  char out[4096], src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture(fixture));
  char *const argv[] = {(char *)"cmp", src, (char *)copie, NULL};
  int rc = fu_run(argv, out, sizeof out);
  if (rc != 0) fprintf(stderr, "%s modifiée : %s\n", fixture, out);
  return rc == 0;
}

/* debugfs -w -R <cmd> img. */
static int debugfs_w(const char *img, const char *cmd) {
  char out[4096];
  char *const argv[] = {(char *)"debugfs", (char *)"-w", (char *)"-R", (char *)cmd, (char *)img, NULL};
  return fu_run(argv, out, sizeof out);
}

static bool lire_hello(fl_fs *fs) {
  fl_file *f = NULL;
  char b[32] = {0};
  if (fl_fs_open(fs, "/hello.txt", FL_O_RDONLY, &f) != FL_OK) return false;
  int64_t n = fl_file_pread(f, b, sizeof b, 0);
  fl_file_close(f);
  return n == 16 && !memcmp(b, "Bonjour FluLuks\n", 16);
}

/* Image retouchée, montée en écriture demandée : lecture seule avec `raison`, lecture possible,
 * écriture refusée, image intacte après démontage (comparée à la copie retouchée). */
static void refusee(const char *fixture, const char *retouche, const char *raison) {
  char img[4096], ref[4096];
  vol v;
  TT_ASSERT(copie_de(fixture, img, sizeof img) == 0);
  if (retouche) TT_ASSERT_EQ(debugfs_w(img, retouche), 0);
  TT_ASSERT_EQ(fu_copy_image(img, ref, sizeof ref), 0); /* debugfs date ses écritures (s_wtime) */
  noter(ref);
  TT_ASSERT_EQ(vol_monter(&v, img, 0, 0), FL_OK);
  TT_ASSERT(lecture_seule(v.fs, raison));
  TT_ASSERT(lire_hello(v.fs));
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/nouveau"), FL_EROFS);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  char out[4096];
  char *const argv[] = {(char *)"cmp", ref, img, NULL};
  int rc = fu_run(argv, out, sizeof out);
  if (rc != 0) fprintf(stderr, "%s modifiée : %s\n", fixture, out);
  TT_ASSERT_EQ(rc, 0);
  unlink(img);
  unlink(ref);
}

/* ---------- tests ---------- */

static void test_politique_vrai_disque_rw(void) {
  static const char *const images[] = {"ext4-rw-reel.img", "ext4-rw-1k.img", "ext4-rw-uninit.img",
                                       "ext4-rw-seed.img"};
  uint8_t data[5000];
  remplir(data, sizeof data, 1);
  for (size_t i = 0; i < sizeof images / sizeof *images; i++) {
    char img[4096];
    vol v;
    TT_ASSERT(copie_de(images[i], img, sizeof img) == 0);
    TT_ASSERT_EQ(vol_monter(&v, img, 0, 0), FL_OK);
    if (!en_ecriture(v.fs)) fprintf(stderr, "%s\n", images[i]);
    TT_ASSERT(en_ecriture(v.fs));
    TT_ASSERT(lire_hello(v.fs));
    TT_ASSERT_EQ(ecrire_fichier(v.fs, "/p.txt", data, sizeof data), FL_OK);
    TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/pd"), FL_OK);
    TT_ASSERT_EQ(ecrire_fichier(v.fs, "/pd/q.txt", data, 100), FL_OK);
    TT_ASSERT_EQ(fl_fs_rename(v.fs, "/pd/q.txt", "/q.txt"), FL_OK);
    TT_ASSERT_EQ(fl_fs_unlink(v.fs, "/hello.txt"), FL_OK);
    TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
    if (e2fsck_propre(img) != 0) fprintf(stderr, "%s\n", images[i]);
    TT_ASSERT_EQ(e2fsck_propre(img), 0);
    unlink(img);
  }
}

static void test_politique_sans_journal(void) {
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-sans-journal.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, 0, 0), FL_OK);
  TT_ASSERT(lecture_seule(v.fs, FL_RO_NOJOURNAL));
  TT_ASSERT(lire_hello(v.fs));
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/x", "x", 1), FL_EROFS);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT(identique("ext4-rw-sans-journal.img", img));
  unlink(img);
}

static void test_politique_meta_bg(void) { refusee("ext4-rw-meta-bg.img", NULL, FL_RO_FEATURES); }

static void test_politique_orphan_present(void) {
  refusee("ext4-rw-reel.img", "feature orphan_present", FL_RO_FEATURES);
}

static void test_politique_recover(void) {
  refusee("ext4-rw-reel.img", "feature needs_recovery", FL_RO_RECOVERY);
}

static void test_politique_erreurs(void) {
  refusee("ext4-rw-reel.img", "ssv state 2", FL_RO_ERRORS);
  refusee("ext4-rw-reel.img", "ssv error_count 3", FL_RO_ERRORS);
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Inverse un octet du descripteur de groupe 3 (son champ bg_checksum). */
static int abimer_descripteur(const char *img) {
  int fd = open(img, O_RDWR);
  if (fd < 0) return -1;
  uint8_t sb[1024], o;
  int rc = -1;
  if (pread(fd, sb, sizeof sb, 1024) == (ssize_t)sizeof sb) {
    uint32_t bs = 1024u << le32(sb + 0x18);
    uint32_t ds = (le32(sb + 0x60) & 0x80) ? le16(sb + 0xFE) : 32;
    off_t off = (off_t)(le32(sb + 0x14) + 1) * bs + 3 * ds + 0x1E;
    if (pread(fd, &o, 1, off) == 1) {
      o ^= 0xFF;
      if (pwrite(fd, &o, 1, off) == 1) rc = 0;
    }
  }
  close(fd);
  return rc;
}

/* ext4-rw-1k.img : 8 groupes (ext4-rw-reel.img n'en a que 2). */
static void test_politique_somme_descripteur(void) {
  char img[4096], ref[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-1k.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(abimer_descripteur(img), 0);
  TT_ASSERT_EQ(fu_copy_image(img, ref, sizeof ref), 0);
  noter(ref);
  TT_ASSERT_EQ(vol_monter(&v, img, 0, 0), FL_OK);
  TT_ASSERT(lecture_seule(v.fs, FL_RO_CSUM));
  TT_ASSERT(lire_hello(v.fs));
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  char out[4096];
  char *const argv[] = {(char *)"cmp", ref, img, NULL};
  TT_ASSERT_EQ(fu_run(argv, out, sizeof out), 0);
  unlink(img);
  unlink(ref);
}

/* Somme fausse découverte en cours de session : l'opération rend FL_ECORRUPT et le volume passe en
 * lecture seule (RO_CSUM), la lecture continue. */
static void test_politique_somme_en_cours(void) {
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  /* un octet du nom « données.bin » (entrée à l'octet 24, nom à 32) du bloc de /sous-dossier */
  TT_ASSERT_EQ(debugfs_w(img, "zap_block -f /sous-dossier -o 33 -l 1 -p 0x55 0"), 0);
  TT_ASSERT_EQ(vol_monter(&v, img, 0, 0), FL_OK);
  TT_ASSERT(en_ecriture(v.fs));
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/avant.txt", "a", 1), FL_OK);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/sous-dossier/nouveau", FL_O_RDWR | FL_O_CREAT, &f), FL_ECORRUPT);
  TT_ASSERT(lecture_seule(v.fs, FL_RO_CSUM));
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/apres"), FL_EROFS);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/apres.txt", "b", 1), FL_EROFS);
  TT_ASSERT(lire_hello(v.fs));
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  uint8_t b[4];
  TT_ASSERT_EQ(fu_debugfs_cat(img, "/avant.txt", b, sizeof b), 1);
  unlink(img);
}

/* Pendant un montage en écriture, le superbloc dit « clean » et needs_recovery (comme le noyau) ;
 * avant le correctif 0005 : « not clean with errors ». */
static void test_politique_etat_pendant_montage(void) {
  char img[4096], chaud[4096];
  static char out[1 << 16];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, 0, 0), FL_OK);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/c.txt", "c", 1), FL_OK);
  TT_ASSERT_EQ(fu_copy_image(img, chaud, sizeof chaud), 0);
  noter(chaud);
  char *const argv[] = {(char *)"dumpe2fs", (char *)"-h", chaud, NULL};
  int rc = fu_run(argv, out, sizeof out);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(rc, 0);
  const char *etat = strstr(out, "Filesystem state:");
  const char *options = strstr(out, "Filesystem features:");
  TT_ASSERT(etat && options);
  char ligne[256];
  sscanf(etat, "Filesystem state: %255[^\n]", ligne);
  fprintf(stderr, "état pendant le montage : « %s »\n", ligne);
  TT_ASSERT_STR_EQ(ligne, "clean");
  const char *fin = strchr(options, '\n');
  TT_ASSERT(fin && memmem(options, (size_t)(fin - options), "needs_recovery", 14));
  /* et après : propre, horodatages du superbloc posés */
  TT_ASSERT_EQ(e2fsck_propre(img), 0);
  char *const argv2[] = {(char *)"dumpe2fs", (char *)"-h", img, NULL};
  TT_ASSERT_EQ(fu_run(argv2, out, sizeof out), 0);
  TT_ASSERT(strstr(out, "Last write time:") && !strstr(out, "Last write time:          n/a"));
  unlink(img);
  unlink(chaud);
}

/* Premier montage en écriture d'un disque de 8 Gio aux tables d'inodes non initialisées : pas de
 * remise à zéro des tables (avant : toutes celles atteintes, environ 15 Go pour 1 To). */
static void test_politique_premier_montage_sobre(void) {
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-gros.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, UINT64_MAX, FL_EIO), FL_OK);
  TT_ASSERT(en_ecriture(v.fs));
  uint8_t b[4096];
  remplir(b, sizeof b, 2);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/sobre.bin", b, sizeof b), FL_OK);
  fl_bdev *fd = v.dev;
  TT_ASSERT_EQ(fl_fs_unmount(v.fs), FL_OK);
  uint64_t w = fl_fault_bdev_writes(fd);
  fl_bdev_close(v.dev);
  fl_bdev_close(v.file);
  fprintf(stderr, "premier montage : %llu écritures\n", (unsigned long long)w);
  TT_ASSERT(w < 200);
  TT_ASSERT_EQ(e2fsck_propre(img), 0);
  unlink(img);
}

/* Disque débranché en pleine écriture : erreur, démontage qui libère tout (ASan), et les 8
 * emplacements de lwext4 ne s'épuisent pas. */
static void test_politique_disque_mort(void) {
  uint8_t b[20000];
  remplir(b, sizeof b, 9);
  for (int k = 0; k < 10; k++) {
    char img[4096];
    vol v;
    TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
    TT_ASSERT_EQ(vol_monter(&v, img, 100, FL_ENODEV), FL_OK);
    int rc = FL_OK;
    for (int i = 0; i < 200 && rc == FL_OK; i++) {
      char p[32];
      snprintf(p, sizeof p, "/mort-%d", i);
      rc = ecrire_fichier(v.fs, p, b, sizeof b);
    }
    TT_ASSERT_EQ(rc, FL_EIO);
    TT_ASSERT(lecture_seule(v.fs, FL_RO_IOERR));
    TT_ASSERT(lire_hello(v.fs));
    TT_ASSERT(vol_demonter(&v) != FL_OK);
    unlink(img);
  }
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, 0, 0), FL_OK);
  TT_ASSERT(en_ecriture(v.fs));
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  unlink(img);
}

/* Le scénario de l'essai : erreur d'E/S pendant l'écriture d'un gros fichier (cache en écriture
 * différée pendant ext4_fwrite) ; avant le correctif, NULL déréférencé dans read_extent_tree_block. */
static void test_politique_eio_ecriture_differee(void) {
  static const uint64_t ns[] = {10, 37, 120, 333, 700};
  uint8_t *b = malloc(MIB);
  TT_ASSERT(b != NULL);
  remplir(b, MIB, 4);
  for (size_t k = 0; k < sizeof ns / sizeof *ns; k++) {
    char img[4096];
    vol v;
    TT_ASSERT(copie_de("ext4-rw-gros.img", img, sizeof img) == 0);
    TT_ASSERT_EQ(vol_monter(&v, img, ns[k], FL_ENODEV), FL_OK);
    fl_file *f = NULL;
    int rc = fl_fs_open(v.fs, "/eio.bin", FL_O_RDWR | FL_O_CREAT, &f);
    for (size_t o = 0; rc == FL_OK && o < 64 * MIB; o += MIB) {
      int64_t w = fl_file_pwrite(f, b, MIB, o);
      if (w != (int64_t)MIB) rc = (int)w;
    }
    if (f) fl_file_close(f);
    TT_ASSERT(rc == FL_EIO || rc == FL_EROFS);
    TT_ASSERT(vol_demonter(&v) != FL_OK);
    unlink(img);
  }
  free(b);
}

int main(void) {
  if (!fu_tools_available()) {
    printf("SAUTÉ : e2fsck/debugfs/dumpe2fs absents ou plateforme Android (banc réservé au PC)\n");
    return 77; /* SKIP_RETURN_CODE de CTest */
  }
  fl_fs_set_clock(horloge);
  TT_RUN(test_politique_vrai_disque_rw);
  TT_RUN(test_politique_sans_journal);
  TT_RUN(test_politique_meta_bg);
  TT_RUN(test_politique_orphan_present);
  TT_RUN(test_politique_recover);
  TT_RUN(test_politique_erreurs);
  TT_RUN(test_politique_somme_descripteur);
  TT_RUN(test_politique_somme_en_cours);
  TT_RUN(test_politique_etat_pendant_montage);
  TT_RUN(test_politique_premier_montage_sobre);
  TT_RUN(test_politique_disque_mort);
  TT_RUN(test_politique_eio_ecriture_differee);
  fl_fs_set_clock(NULL);
  nettoyer();
  TT_MAIN_END();
}
