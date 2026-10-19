#define _GNU_SOURCE
/* Coupures et pannes sur NTFS (phase 5, T7). libntfs-3g n'a pas de journal : une coupure (toute écriture
 * refusée à partir de la n-ième) ne doit jamais laisser un volume incohérent marqué propre. Soit le volume est
 * cohérent selon ntfsjuge.py check, soit il porte VOLUME_IS_DIRTY (lecture seule FL_RO_DIRTY à la
 * réouverture, chkdsk au branchement suivant sous Windows). Un fichier fermé avant la session et non touché
 * ensuite reste intact selon ses enregistrements MFT (ce que chkdsk retrouve). Réservé au PC. */
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
#include "tree_model.h"
#include "tt.h"

#define KIB ((size_t)1024)

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

static int monter(fl_bdev *dev, bool rw, fl_fs **fs) { return fl_fs_mount(dev, FL_CONTENT_NTFS, rw, fs); }

/* Image de base : ntfs-vide plus /surs/sur1..3 (300 Kio chacun) et /racine.txt, démontée proprement. */
static int preparer_base(char *base, size_t cap, uint8_t *sur[3]) {
  if (fu_copy_image(tt_fixture("ntfs-vide.img"), base, cap) != 0) return FL_EIO;
  fl_bdev *d = NULL;
  fl_fs *fs = NULL;
  int rc = fl_file_bdev_open(base, 512, false, &d);
  if (rc == FL_OK) rc = monter(d, true, &fs);
  if (rc == FL_OK) rc = fl_fs_mkdir(fs, "/surs");
  for (int i = 0; rc == FL_OK && i < 3; i++) {
    char p[32];
    snprintf(p, sizeof p, "/surs/sur%d", i + 1);
    rc = ecrire_fichier(fs, p, sur[i], 300 * KIB);
  }
  if (rc == FL_OK) rc = ecrire_fichier(fs, "/racine.txt", "racine\n", 7);
  if (fs && fl_fs_unmount(fs) != FL_OK && rc == FL_OK) rc = FL_EIO;
  fl_bdev_close(d);
  return rc;
}

/* Le scénario coupé ; les erreurs sont attendues (coupure) : on va jusqu'au bout sans rien vérifier. */
static void scenario(fl_fs *fs, const uint8_t *data) {
  fl_file *f = NULL;
  if (fl_fs_open(fs, "/encours.bin", FL_O_RDWR | FL_O_CREAT, &f) == FL_OK)
    for (size_t o = 0; o < 2048 * KIB; o += 128 * KIB) fl_file_pwrite(f, data + o, 128 * KIB, o);
  fl_fs_mkdir(fs, "/d");
  ecrire_fichier(fs, "/d/petit.txt", "petit\n", 6);
  fl_fs_rename(fs, "/surs/sur1", "/sur1b");
  fl_file *t = NULL;
  if (fl_fs_open(fs, "/surs/sur2", FL_O_RDWR, &t) == FL_OK) {
    fl_file_truncate(t, 1000);
    fl_file_close(t);
  }
  fl_fs_unlink(fs, "/racine.txt");
  for (int i = 0; i < 40; i++) { /* l'index de la racine grandit */
    char p[32];
    snprintf(p, sizeof p, "/n%02d.txt", i);
    ecrire_fichier(fs, p, p, 8);
  }
  if (f) fl_file_close(f);
}

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
  static char x[65536], y[65536];
  while (same) {
    size_t na = fread(x, 1, sizeof x, fa), nb = fread(y, 1, sizeof y, fb);
    if (na != nb || memcmp(x, y, na)) same = 0;
    if (na == 0) break;
  }
  if (fa) fclose(fa);
  if (fb) fclose(fb);
  return same;
}

/* Ligne du manifeste `fichier` pour `chemin` (sans la fin de ligne), ou "". */
static void ligne_de(const char *fichier, const char *chemin, char *out, size_t cap) {
  out[0] = 0;
  FILE *f = fopen(fichier, "r");
  char l[1024];
  while (f && fgets(l, sizeof l, f)) {
    l[strcspn(l, "\n")] = 0;
    const char *c = strrchr(l, '\t');
    if (c && !strcmp(c + 1, chemin)) {
      snprintf(out, cap, "%s", l);
      break;
    }
  }
  if (f) fclose(f);
}

static void test_ntfs_coupure_partout(void) {
  uint8_t *sur[3];
  for (int i = 0; i < 3; i++) sur[i] = motif(300 * KIB, 100 + (uint32_t)i);
  uint8_t *data = motif(2048 * KIB, 5);
  char base[4096];
  TT_ASSERT_EQ(preparer_base(base, sizeof base, sur), FL_OK);
  /* ligne attendue du fichier sûr non touché */
  tm *m = tm_new();
  tm_set_ntfs_attrs(m);
  tm_apply_mkdir(m, "/surs", 0, 0, 0755);
  tm_apply_file(m, "/surs/sur3", sur[2], 300 * KIB, 0, 0, 0644);
  char man_attendu[4200], man_juge[4200], attendu[1024], vu[1024];
  snprintf(man_attendu, sizeof man_attendu, "%s.attendu", base);
  snprintf(man_juge, sizeof man_juge, "%s.juge", base);
  tm_write_manifest(m, man_attendu);
  tm_free(m);
  ligne_de(man_attendu, "/surs/sur3", attendu, sizeof attendu);

  uint64_t n = mesurer(base, data);
  uint64_t pas = n / 80 ? n / 80 : 1;
  int echecs = 0, sales = 0, incoherents = 0, essais = 0;
  for (uint64_t k = 0; k <= n && echecs < 3; k += pas) {
    essais++;
    char c[4096], out[8192];
    if (fu_copy_image(base, c, sizeof c) != 0) {
      echecs++;
      break;
    }
    fl_bdev *img = NULL, *fd = NULL;
    fl_fs *fs = NULL;
    if (fl_file_bdev_open(c, 512, false, &img) == FL_OK && fl_fault_bdev_new(img, k, FL_ENODEV, &fd) == FL_OK &&
        monter(fd, true, &fs) == FL_OK) {
      scenario(fs, data);
      fl_fs_unmount(fs); /* une erreur est permise */
    }
    fl_bdev_close(fd);
    fl_bdev_close(img);
    const char *pourquoi = NULL;
    out[0] = 0;
    int coherent = fu_ntfsjuge(out, sizeof out, "check", c, NULL);
    int drapeaux = fu_ntfs_flags(c);
    if (k == 0 && !memes_octets(c, base)) pourquoi = "image modifiée sans aucune écriture permise";
    else if (coherent != 0 && coherent != 3) pourquoi = "juge inutilisable";
    else if (drapeaux < 0) pourquoi = "drapeaux de $Volume illisibles";
    else if (coherent == 3 && !(drapeaux & 1)) pourquoi = "incohérent mais marqué propre";
    if (coherent == 3) incoherents++;
    /* réouverture en écriture : propre, ou lecture seule FL_RO_DIRTY */
    fl_statfs st = {0};
    fl_bdev *d2 = NULL;
    fl_fs *fs2 = NULL;
    int mrc = fl_file_bdev_open(c, 512, false, &d2);
    if (mrc == FL_OK) mrc = monter(d2, true, &fs2);
    if (mrc == FL_OK) mrc = fl_fs_statfs(fs2, &st);
    fl_fs_unmount(fs2);
    fl_bdev_close(d2);
    if (!pourquoi && mrc != FL_OK) pourquoi = "réouverture impossible";
    if (!pourquoi && (drapeaux & 1) && (!st.read_only || strcmp(st.ro_reason, FL_RO_DIRTY)))
      pourquoi = "volume sale rouvert sans FL_RO_DIRTY";
    if (st.read_only) sales++;
    if (!pourquoi) {
      char o2[4096];
      if (fu_ntfsjuge(o2, sizeof o2, "manifest-mft", c, man_juge) != 0) pourquoi = "enregistrements MFT illisibles";
      ligne_de(man_juge, "/surs/sur3", vu, sizeof vu);
      if (!pourquoi && strcmp(vu, attendu)) pourquoi = "fichier sûr abîmé";
    }
    if (pourquoi) {
      fprintf(stderr, "coupure après %llu écritures sur %llu : %s\n%s\n", (unsigned long long)k,
              (unsigned long long)n, pourquoi, out);
      echecs++;
    }
    unlink(c);
  }
  fprintf(stderr, "%d coupures, %d incohérentes (toutes marquées sales), %d rouvertes en lecture seule\n", essais,
          incoherents, sales);
  unlink(man_attendu);
  unlink(man_juge);
  unlink(base);
  for (int i = 0; i < 3; i++) free(sur[i]);
  free(data);
  TT_ASSERT(n > 10);
  TT_ASSERT(sales > 0);
  TT_ASSERT_EQ(echecs, 0);
}

static int compter(void *ctx, const fl_dirent *e) {
  (void)e;
  (*(size_t *)ctx)++;
  return 0;
}

/* Lecture en échec pendant un parcours de dossier : erreur propre, pas de lecture seule collante, volume
 * utilisable ensuite. */
static void test_ntfs_panne_lecture(void) {
  char c[4096];
  TT_ASSERT(fu_copy_image(tt_fixture("ntfs-arbre.img"), c, sizeof c) == 0);
  fl_bdev *img = NULL, *fd = NULL;
  fl_fs *fs = NULL;
  int rc = fl_file_bdev_open(c, 512, false, &img);
  if (rc == FL_OK) rc = fl_fault_bdev_new(img, UINT64_MAX, FL_EIO, &fd);
  if (rc == FL_OK) rc = monter(fd, true, &fs);
  size_t n = 0;
  if (fd) fl_fault_bdev_fail_reads(fd, FL_EIO);
  int r1 = rc == FL_OK ? fl_fs_readdir(fs, "/p1/p2/p3/p4/p5/p6/p7/p8", compter, &n) : rc;
  fl_stat st;
  int r2 = rc == FL_OK ? fl_fs_stat(fs, "/p1/p2/p3/p4/p5/p6/p7/p8/p9/p10", &st) : rc;
  if (fd) fl_fault_bdev_fail_reads(fd, 0);
  int r3 = rc == FL_OK ? fl_fs_stat(fs, "/p1/p2/p3/p4/p5/p6/p7/p8/p9/p10", &st) : rc;
  fl_statfs sf = {0};
  if (rc == FL_OK) fl_fs_statfs(fs, &sf);
  int mk = rc == FL_OK ? fl_fs_mkdir(fs, "/apres") : rc;
  int um = fs ? fl_fs_unmount(fs) : FL_EIO;
  fl_bdev_close(fd);
  fl_bdev_close(img);
  unlink(c);
  TT_ASSERT_EQ(rc, FL_OK);
  (void)r1; /* chemin peut-être en cache : seul r2 est sûr de lire le disque */
  TT_ASSERT_EQ(r2, FL_EIO);
  TT_ASSERT_EQ(r3, FL_OK);
  TT_ASSERT(!sf.read_only);
  TT_ASSERT_EQ(mk, FL_OK);
  TT_ASSERT_EQ(um, FL_OK);
}

/* Disque débranché (FL_ENODEV) pendant une écriture : erreurs propres, tout est libéré (ASan), l'éjection
 * ne se dit pas réussie ; l'image sale se rouvre ensuite en lecture seule. */
static void test_ntfs_debranche(void) {
  char c[4096];
  TT_ASSERT(fu_copy_image(tt_fixture("ntfs-vide.img"), c, sizeof c) == 0);
  uint8_t *data = motif(2048 * KIB, 8);
  for (int tour = 0; tour < 6; tour++) {
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
    TT_ASSERT_EQ(um, rw ? FL_EIO : FL_OK);
    if (tour > 0) TT_ASSERT(!rw);
  }
  free(data);
  unlink(c);
}

/* La dernière écriture de la session (drapeau effacé au démontage) échoue : le démontage le dit. */
static void test_ntfs_demontage_en_echec(void) {
  uint64_t total = 0;
  for (int passe = 0; passe < 2; passe++) {
    char c[4096];
    TT_ASSERT(fu_copy_image(tt_fixture("ntfs-vide.img"), c, sizeof c) == 0);
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
    int drapeaux = fu_ntfs_flags(c);
    unlink(c);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT_EQ(um, passe ? FL_EIO : FL_OK);
    TT_ASSERT_EQ(drapeaux, passe ? 1 : 0); /* le drapeau effacé en dernier : resté posé */
    TT_ASSERT(total > 1);
  }
}

int main(void) {
  if (!fu_ntfs_tools_available()) {
    printf("SAUTÉ : outils NTFS absents ou plateforme Android (juges réservés au PC)\n");
    return 77;
  }
  TT_RUN(test_ntfs_coupure_partout);
  TT_RUN(test_ntfs_panne_lecture);
  TT_RUN(test_ntfs_debranche);
  TT_RUN(test_ntfs_demontage_en_echec);
  TT_MAIN_END();
}
