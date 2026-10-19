#define _GNU_SOURCE
/* Coupures et pannes sur FAT32 et exFAT (phase 4, T7). Une coupure (toute écriture refusée à partir de
 * la n-ième) ne doit jamais laisser un volume incohérent marqué propre : soit le volume est propre selon
 * fsck, soit il porte le drapeau « sale » (lecture seule FL_RO_DIRTY à la réouverture). Après réparation
 * par fsck, les fichiers fermés avant la coupure et non touchés ensuite sont intacts. Réservé au PC. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fault_bdev.h"
#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fluluks/probe.h"
#include "fs/ro_reasons.h"
#include "fsck_util.h"
#include "tree_model.h"
#include "tt.h"

#define KIB ((size_t)1024)
#define NSUR 5

static uint8_t *motif(size_t n, uint32_t g) {
  uint8_t *b = malloc(n ? n : 1);
  for (size_t i = 0; b && i < n; i++) {
    g = g * 1103515245u + 12345u;
    b[i] = (uint8_t)(g >> 16);
  }
  return b;
}

static int ecrire_fichier(fl_fs *fs, const char *p, const void *data, size_t n) {
  fl_file *f = NULL;
  int rc = fl_fs_open(fs, p, FL_O_RDWR | FL_O_CREAT | FL_O_TRUNC, &f);
  if (rc != FL_OK) return rc;
  int64_t w = n ? fl_file_pwrite(f, data, n, 0) : 0;
  int c = fl_file_close(f);
  return w < 0 ? (int)w : c;
}

/* Monte `img` (périphérique dev) selon son contenu sondé. */
static int monter(fl_bdev *dev, bool rw, fl_fs **fs) {
  fl_probe_result pr;
  int rc = fl_probe(dev, &pr);
  return rc == FL_OK ? fl_fs_mount(dev, pr.content, rw, fs) : rc;
}

/* Image de base : la fixture vide plus NSUR fichiers « sûrs » de 300 Kio, écrits et démontés proprement. */
static int preparer_base(const char *fixture, char *base, size_t cap, uint8_t *sur[NSUR]) {
  if (fu_copy_image(tt_fixture(fixture), base, cap) != 0) return FL_EIO;
  fl_bdev *d = NULL;
  fl_fs *fs = NULL;
  int rc = fl_file_bdev_open(base, 512, false, &d);
  if (rc == FL_OK) rc = monter(d, true, &fs);
  for (int i = 0; rc == FL_OK && i < NSUR; i++) {
    char p[32];
    snprintf(p, sizeof p, "/sur%d", i + 1);
    rc = ecrire_fichier(fs, p, sur[i], 300 * KIB);
  }
  if (fs && fl_fs_unmount(fs) != FL_OK && rc == FL_OK) rc = FL_EIO;
  fl_bdev_close(d);
  return rc;
}

/* Le scénario coupé : écriture d'un fichier en cours, mkdir, renommage et troncature de fichiers sûrs.
 * Les erreurs sont attendues (coupure) : on continue jusqu'au bout sans rien vérifier. */
static void scenario(fl_fs *fs, const uint8_t *data) {
  fl_file *f = NULL;
  if (fl_fs_open(fs, "/encours.bin", FL_O_RDWR | FL_O_CREAT, &f) == FL_OK) {
    for (size_t o = 0; o < 2048 * KIB; o += 128 * KIB) fl_file_pwrite(f, data + o, 128 * KIB, o);
  }
  fl_fs_mkdir(fs, "/d");
  fl_fs_rename(fs, "/sur1", "/sur1b");
  fl_file *t = NULL;
  if (fl_fs_open(fs, "/sur2", FL_O_RDWR, &t) == FL_OK) {
    fl_file_truncate(t, 1000);
    fl_file_close(t);
  }
  if (f) fl_file_close(f);
}

/* Écritures d'un scénario complet (sans coupure) sur une copie de base. */
static uint64_t mesurer(const char *base, const uint8_t *data) {
  char c[4096];
  if (fu_copy_image(base, c, sizeof c) != 0) return 0;
  fl_bdev *img = NULL, *fd = NULL;
  fl_fs *fs = NULL;
  uint64_t n = 0;
  if (fl_file_bdev_open(c, 512, false, &img) == FL_OK && fl_fault_bdev_new(img, UINT64_MAX, FL_EIO, &fd) == FL_OK &&
      monter(fd, true, &fs) == FL_OK) {
    scenario(fs, data);
    fl_fs_unmount(fs);
    n = fl_fault_bdev_writes(fd);
  }
  fl_bdev_close(fd);
  fl_bdev_close(img);
  unlink(c);
  return n;
}

static int memes_octets(const char *a, const char *b) {
  FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
  int same = fa && fb;
  char x[65536], y[65536];
  while (same) {
    size_t na = fread(x, 1, sizeof x, fa), nb = fread(y, 1, sizeof y, fb);
    if (na != nb || memcmp(x, y, na)) same = 0;
    if (na == 0) break;
  }
  if (fa) fclose(fa);
  if (fb) fclose(fb);
  return same;
}

/* Lignes du manifeste `fichier` dont le chemin est l'un de `chemins` (concaténées dans out). */
static void lignes_de(const char *fichier, const char *const *chemins, size_t n, char *out, size_t cap) {
  out[0] = 0;
  FILE *f = fopen(fichier, "r");
  char l[1024];
  while (f && fgets(l, sizeof l, f)) {
    const char *c = strrchr(l, '\t');
    for (size_t i = 0; c && i < n; i++) {
      size_t k = strlen(chemins[i]);
      if (!strncmp(c + 1, chemins[i], k) && c[1 + k] == '\n') strncat(out, l, cap - strlen(out) - 1);
    }
  }
  if (f) fclose(f);
}

static void coupures(const char *fixture) {
  uint8_t *sur[NSUR];
  for (int i = 0; i < NSUR; i++) sur[i] = motif(300 * KIB, 100 + (uint32_t)i);
  uint8_t *data = motif(2048 * KIB, 5);
  char base[4096];
  TT_ASSERT_EQ(preparer_base(fixture, base, sizeof base, sur), FL_OK);
  /* manifeste attendu des fichiers sûrs non touchés */
  tm *m = tm_new();
  tm_set_fat_attrs(m);
  for (int i = 2; i < NSUR; i++) {
    char p[32];
    snprintf(p, sizeof p, "/sur%d", i + 1);
    tm_apply_file(m, p, sur[i], 300 * KIB, 0, 0, 0644);
  }
  char man_attendu[4200], man_juge[4200];
  snprintf(man_attendu, sizeof man_attendu, "%s.attendu", base);
  snprintf(man_juge, sizeof man_juge, "%s.juge", base);
  tm_write_manifest(m, man_attendu);
  tm_free(m);
  static const char *const intacts[] = {"/sur3", "/sur4", "/sur5"};
  char attendu[2048], vu[2048];
  lignes_de(man_attendu, intacts, 3, attendu, sizeof attendu);

  uint64_t n = mesurer(base, data);
  uint64_t pas = n / 40 ? n / 40 : 1;
  int echecs = 0, sales = 0, essais = 0;
  for (uint64_t k = 0; k <= n && echecs < 3; k += pas) {
    essais++;
    char c[4096], out[8192];
    if (fu_copy_image(base, c, sizeof c) != 0) {
      echecs++;
      break;
    }
    fl_bdev *img = NULL, *fd = NULL;
    fl_fs *fs = NULL;
    if (fl_file_bdev_open(c, 512, false, &img) == FL_OK && fl_fault_bdev_new(img, k, FL_EIO, &fd) == FL_OK &&
        monter(fd, true, &fs) == FL_OK) {
      scenario(fs, data);
      fl_fs_unmount(fs); /* une erreur est permise */
    }
    fl_bdev_close(fd);
    fl_bdev_close(img);
    /* réouverture en écriture : propre, ou lecture seule FL_RO_DIRTY */
    fl_statfs st = {0};
    fl_bdev *d2 = NULL;
    fl_fs *fs2 = NULL;
    int mrc = fl_file_bdev_open(c, 512, false, &d2);
    if (mrc == FL_OK) mrc = monter(d2, true, &fs2);
    if (mrc == FL_OK) mrc = fl_fs_statfs(fs2, &st);
    fl_fs_unmount(fs2);
    fl_bdev_close(d2);
    const char *pourquoi = NULL;
    if (mrc != FL_OK) pourquoi = "réouverture impossible";
    else if (k == 0 && !memes_octets(c, base)) pourquoi = "image modifiée sans aucune écriture permise";
    else if (st.read_only && strcmp(st.ro_reason, FL_RO_DIRTY)) pourquoi = "lecture seule pour une autre raison";
    else if (!st.read_only && fu_fsck_fat_n(c, out, sizeof out) != 0) pourquoi = "incohérent mais marqué propre";
    else if (fu_fsck_fat_repair(c, out, sizeof out) != 0) pourquoi = "irréparable par fsck";
    else if (fu_fat_manifest(c, man_juge) != 0) pourquoi = "illisible par le juge après réparation";
    if (!pourquoi) {
      lignes_de(man_juge, intacts, 3, vu, sizeof vu);
      if (strcmp(vu, attendu)) pourquoi = "fichier sûr abîmé";
    }
    if (st.read_only) sales++;
    if (pourquoi) {
      fprintf(stderr, "%s, coupure après %llu écritures sur %llu : %s\n%s\n", fixture, (unsigned long long)k,
              (unsigned long long)n, pourquoi, out);
      echecs++;
    }
    unlink(c);
  }
  fprintf(stderr, "%s : %d coupures, %d volumes sales à la réouverture\n", fixture, essais, sales);
  unlink(man_attendu);
  unlink(man_juge);
  unlink(base);
  for (int i = 0; i < NSUR; i++) free(sur[i]);
  free(data);
  TT_ASSERT(n > 10);
  TT_ASSERT(sales > 0);
  TT_ASSERT_EQ(echecs, 0);
}

static void test_fat_coupure_points_fat32(void) { coupures("fat32-vide.img"); }
static void test_fat_coupure_points_exfat(void) { coupures("exfat-vide.img"); }

/* Lecture en échec au milieu d'un fichier : erreur propre, puis relecture juste après rétablissement. */
static void panne_lecture(const char *fixture) {
  char c[4096];
  TT_ASSERT(fu_copy_image(tt_fixture(fixture), c, sizeof c) == 0);
  fl_bdev *img = NULL, *fd = NULL;
  fl_fs *fs = NULL;
  fl_file *f = NULL;
  uint8_t a[4096], b[4096];
  int rc = fl_file_bdev_open(c, 512, false, &img);
  if (rc == FL_OK) rc = fl_fault_bdev_new(img, UINT64_MAX, FL_EIO, &fd);
  if (rc == FL_OK) rc = monter(fd, false, &fs);
  if (rc == FL_OK) rc = fl_fs_open(fs, "/gros.bin", FL_O_RDONLY, &f);
  int64_t r1 = rc == FL_OK ? fl_file_pread(f, a, sizeof a, 0) : rc;
  if (fd) fl_fault_bdev_fail_reads(fd, FL_EIO);
  int64_t r2 = rc == FL_OK ? fl_file_pread(f, b, sizeof b, 2 << 20) : rc;
  if (fd) fl_fault_bdev_fail_reads(fd, 0);
  fl_file_close(f);
  f = NULL;
  if (rc == FL_OK) rc = fl_fs_open(fs, "/gros.bin", FL_O_RDONLY, &f);
  int64_t r3 = rc == FL_OK ? fl_file_pread(f, b, sizeof b, 0) : rc;
  fl_file_close(f);
  fl_fs_unmount(fs);
  fl_bdev_close(fd);
  fl_bdev_close(img);
  unlink(c);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(r1, (int64_t)sizeof a);
  TT_ASSERT_EQ(r2, FL_EIO);
  TT_ASSERT_EQ(r3, (int64_t)sizeof b);
  TT_ASSERT(!memcmp(a, b, sizeof a));
}

static void test_fat_panne_lecture(void) {
  panne_lecture("fat32-arbre.img");
  panne_lecture("exfat-arbre.img");
}

/* Disque débranché (FL_ENODEV) pendant une écriture : erreurs propres, tout est libéré, l'emplacement FatFs
 * est réutilisable (ASan vérifie l'absence de fuite à la fin). */
static void debranche(const char *fixture) {
  char c[4096];
  TT_ASSERT(fu_copy_image(tt_fixture(fixture), c, sizeof c) == 0);
  uint8_t *data = motif(2048 * KIB, 8);
  for (int tour = 0; tour < 10; tour++) {
    fl_bdev *img = NULL, *fd = NULL;
    fl_fs *fs = NULL;
    fl_file *f = NULL;
    int rc = fl_file_bdev_open(c, 512, false, &img);
    if (rc == FL_OK) rc = fl_fault_bdev_new(img, 4, FL_ENODEV, &fd);
    if (rc == FL_OK) rc = monter(fd, true, &fs);
    fl_statfs st = {0};
    if (fs) fl_fs_statfs(fs, &st);
    bool rw = fs && !st.read_only; /* après le premier tour, l'image est sale : lecture seule */
    int64_t w = FL_OK;
    if (rc == FL_OK) rc = fl_fs_open(fs, "/x.bin", FL_O_RDWR | FL_O_CREAT, &f);
    for (size_t o = 0; f && w >= 0 && o < 2048 * KIB; o += 128 * KIB) w = fl_file_pwrite(f, data + o, 128 * KIB, o);
    int cl = f ? fl_file_close(f) : FL_OK;
    int mk = fs ? fl_fs_mkdir(fs, "/apres") : FL_OK;
    int um = fs ? fl_fs_unmount(fs) : FL_OK;
    fl_bdev_close(fd);
    fl_bdev_close(img);
    TT_ASSERT(rc != FL_OK || w < 0 || cl != FL_OK);
    TT_ASSERT(mk != FL_OK);
    TT_ASSERT_EQ(um, rw ? FL_EIO : FL_OK); /* tout est libéré, mais l'éjection ne doit pas se dire réussie */
  }
  free(data);
  unlink(c);
}

/* La dernière écriture de la session (drapeau effacé au démontage) échoue : le démontage le dit. */
static void demontage_en_echec(const char *fixture) {
  uint64_t total = 0;
  for (int passe = 0; passe < 2; passe++) {
    char c[4096];
    TT_ASSERT(fu_copy_image(tt_fixture(fixture), c, sizeof c) == 0);
    fl_bdev *img = NULL, *fd = NULL;
    fl_fs *fs = NULL;
    int rc = fl_file_bdev_open(c, 512, false, &img);
    if (rc == FL_OK) rc = fl_fault_bdev_new(img, passe ? total - 1 : UINT64_MAX, FL_EIO, &fd);
    if (rc == FL_OK) rc = monter(fd, true, &fs);
    if (rc == FL_OK) rc = fl_fs_mkdir(fs, "/d");
    int um = fs ? fl_fs_unmount(fs) : FL_EIO;
    if (fd && !passe) total = fl_fault_bdev_writes(fd);
    fl_bdev_close(fd);
    fl_bdev_close(img);
    unlink(c);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT_EQ(um, passe ? FL_EIO : FL_OK);
    TT_ASSERT(total > 1);
  }
}

static void test_fat_demontage_en_echec(void) {
  demontage_en_echec("fat32-vide.img");
  demontage_en_echec("exfat-vide.img");
}

static void test_fat_debranche(void) {
  debranche("fat32-vide.img");
  debranche("exfat-vide.img");
}

int main(void) {
  if (!fu_fat_tools_available()) {
    printf("SAUTÉ : outils FAT/exFAT absents ou plateforme Android (banc réservé au PC)\n");
    return 77;
  }
  TT_RUN(test_fat_coupure_points_fat32);
  TT_RUN(test_fat_coupure_points_exfat);
  TT_RUN(test_fat_panne_lecture);
  TT_RUN(test_fat_debranche);
  TT_RUN(test_fat_demontage_en_echec);
  TT_MAIN_END();
}
