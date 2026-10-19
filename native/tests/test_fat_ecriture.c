#define _GNU_SOURCE
/* FAT/exFAT en écriture (phase 4, T4–T6) : chaque test écrit sur une copie, démonte, puis passe les juges
 * (fsck.fat / fsck.exfat, fatjuge.py contre le modèle). Réservé au PC : sauté sans les outils, et sur
 * Android. « test_fat_ecriture lent » : seulement les tests longs. */
#include <fcntl.h>
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
#include "fluluks/probe.h"
#include "fs/ro_reasons.h"
#include "fsck_util.h"
#include "tree_model.h"
#include "tt.h"

#define MIB ((size_t)1 << 20)

typedef struct {
  char copie[4096];
  fl_bdev *dev;
  fl_fs *fs;
  tm *m;
} vol;

/* Copie de la fixture, montée en écriture, avec son modèle : arborescence source (arbre) ou racine seule. */
static int ouvrir(const char *fixture, const char *arbre, vol *v) {
  memset(v, 0, sizeof *v);
  if (fu_copy_image(tt_fixture(fixture), v->copie, sizeof v->copie) != 0) return FL_EIO;
  if (fl_file_bdev_open(v->copie, 512, false, &v->dev) != FL_OK) return FL_EIO;
  fl_probe_result pr;
  if (fl_probe(v->dev, &pr) != FL_OK) return FL_EIO;
  v->m = tm_new();
  if (!v->m) return FL_ENOMEM;
  if (arbre) {
    char src[4096];
    snprintf(src, sizeof src, "%s", tt_fixture(arbre));
    if (tm_from_dir(v->m, src) != 0) return FL_EIO;
  }
  tm_set_fat_attrs(v->m);
  return fl_fs_mount(v->dev, pr.content, true, &v->fs);
}

static void liberer(vol *v) {
  if (v->fs) fl_fs_unmount(v->fs);
  v->fs = NULL;
  fl_bdev_close(v->dev);
  v->dev = NULL;
  tm_free(v->m);
  v->m = NULL;
  if (v->copie[0]) unlink(v->copie);
  v->copie[0] = 0;
}

/* Démonte puis juge : 0 si fsck et fatjuge sont d'accord avec le modèle. */
static int juger(vol *v) {
  int rc = fl_fs_unmount(v->fs);
  v->fs = NULL;
  fl_bdev_close(v->dev);
  v->dev = NULL;
  if (rc != FL_OK) {
    fprintf(stderr, "%s : démontage = %d\n", v->copie, rc);
    return -1;
  }
  char out[8192];
  int f = fu_fsck_fat_n(v->copie, out, sizeof out);
  if (f != 0) fprintf(stderr, "%s : fsck = %d\n%s\n", v->copie, f, out);
  int d = tm_check_fat_judge(v->m, v->copie);
  if (d != 0) fprintf(stderr, "%s : juge = %d\n", v->copie, d);
  return f == 0 && d == 0 ? 0 : -1;
}

static int ecrire_fichier(fl_fs *fs, const char *p, const void *data, size_t n) {
  fl_file *f = NULL;
  int rc = fl_fs_open(fs, p, FL_O_RDWR | FL_O_CREAT | FL_O_TRUNC, &f);
  if (rc != FL_OK) return rc;
  int64_t w = n ? fl_file_pwrite(f, data, n, 0) : 0;
  int c = fl_file_close(f);
  return w < 0 ? (int)w : c;
}

static int relire(fl_fs *fs, const char *p, void *buf, size_t n) {
  fl_file *f = NULL;
  int rc = fl_fs_open(fs, p, FL_O_RDONLY, &f);
  if (rc != FL_OK) return rc;
  size_t got = 0;
  while (got < n) {
    int64_t r = fl_file_pread(f, (uint8_t *)buf + got, n - got, got);
    if (r <= 0) break;
    got += (size_t)r;
  }
  fl_file_close(f);
  return got == n ? FL_OK : FL_EIO;
}

static uint8_t *motif(size_t n, uint32_t graine) {
  uint8_t *b = malloc(n ? n : 1);
  for (size_t i = 0; b && i < n; i++) {
    graine = graine * 1103515245u + 12345u;
    b[i] = (uint8_t)(graine >> 16);
  }
  return b;
}

/* Images de départ des tests génériques : FAT32 et exFAT vides, FAT16 rempli. */
static const char *const IMGS[][2] = {{"fat32-vide.img", NULL}, {"exfat-vide.img", NULL}, {"fat16-arbre.img", "tree-fat"}};
#define NIMGS (sizeof IMGS / sizeof IMGS[0])

static void test_fat_creer_ecrire(void) {
  static const size_t parts[] = {1, 4095, 2, 1500, 3000, 400, 1002}; /* 10 000 octets */
  uint8_t *data = motif(10000, 7), *back = malloc(10000);
  TT_ASSERT(data && back);
  for (size_t i = 0; i < NIMGS; i++) {
    vol v;
    int rc = ouvrir(IMGS[i][0], IMGS[i][1], &v);
    fl_file *f = NULL;
    if (rc == FL_OK) rc = fl_fs_open(v.fs, "/a.txt", FL_O_RDWR | FL_O_CREAT, &f);
    size_t off = 0;
    for (size_t k = 0; rc == FL_OK && k < sizeof parts / sizeof parts[0]; k++) {
      int64_t w = fl_file_pwrite(f, data + off, parts[k], off);
      if (w != (int64_t)parts[k]) rc = w < 0 ? (int)w : FL_EIO;
      off += parts[k];
    }
    if (f && fl_file_close(f) != FL_OK && rc == FL_OK) rc = FL_EIO;
    if (rc == FL_OK) rc = relire(v.fs, "/a.txt", back, 10000);
    int same = rc == FL_OK && !memcmp(data, back, 10000);
    tm_apply_file(v.m, "/a.txt", data, 10000, 0, 0, 0644);
    int j = rc == FL_OK ? juger(&v) : -1;
    liberer(&v);
    if (rc != FL_OK || !same || j) fprintf(stderr, "%s : rc %d\n", IMGS[i][0], rc);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT(same);
    TT_ASSERT_EQ(j, 0);
  }
  free(data);
  free(back);
}

static void test_fat_trou(void) {
  size_t n = MIB + 4;
  uint8_t *attendu = calloc(1, n), *back = malloc(n);
  TT_ASSERT(attendu && back);
  attendu[n - 1] = 0xA5;
  uint8_t *zeros = calloc(1, 200000);
  for (size_t i = 0; i < NIMGS; i++) {
    vol v;
    int rc = ouvrir(IMGS[i][0], IMGS[i][1], &v);
    fl_file *f = NULL;
    if (rc == FL_OK) rc = fl_fs_open(v.fs, "/trou.bin", FL_O_RDWR | FL_O_CREAT, &f);
    if (rc == FL_OK && fl_file_pwrite(f, &attendu[n - 1], 1, n - 1) != 1) rc = FL_EIO;
    fl_file_close(f);
    f = NULL;
    if (rc == FL_OK) rc = fl_fs_open(v.fs, "/tronq.bin", FL_O_RDWR | FL_O_CREAT, &f);
    if (rc == FL_OK) rc = fl_file_truncate(f, 200000);
    fl_file_close(f);
    fl_stat st;
    if (rc == FL_OK) rc = fl_fs_stat(v.fs, "/trou.bin", &st);
    if (rc == FL_OK && st.size != n) rc = FL_EIO;
    if (rc == FL_OK) rc = relire(v.fs, "/trou.bin", back, n);
    int same = rc == FL_OK && !memcmp(back, attendu, n);
    tm_apply_file(v.m, "/trou.bin", attendu, n, 0, 0, 0644);
    tm_apply_file(v.m, "/tronq.bin", zeros, 200000, 0, 0, 0644);
    int j = rc == FL_OK ? juger(&v) : -1;
    liberer(&v);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT(same);
    TT_ASSERT_EQ(j, 0);
  }
  free(attendu);
  free(back);
  free(zeros);
}

static void test_fat_tronquer(void) {
  uint8_t *data = motif(3 * MIB, 11);
  TT_ASSERT(data != NULL);
  for (size_t i = 0; i < NIMGS; i++) {
    vol v;
    int rc = ouvrir(IMGS[i][0], IMGS[i][1], &v);
    fl_statfs avant, apres;
    if (rc == FL_OK) rc = fl_fs_statfs(v.fs, &avant);
    if (rc == FL_OK) rc = ecrire_fichier(v.fs, "/g.bin", data, 3 * MIB);
    fl_file *f = NULL;
    if (rc == FL_OK) rc = fl_fs_open(v.fs, "/g.bin", FL_O_RDWR, &f);
    if (rc == FL_OK) rc = fl_file_truncate(f, 5000);
    fl_file_close(f);
    if (rc == FL_OK) rc = fl_fs_statfs(v.fs, &apres);
    /* taille de cluster : total / nombre de clusters inconnu ici ; 5000 octets tiennent dans 2 clusters
     * d'au moins 4 Kio, ou 10 de 512 octets : au plus 8 Kio + 1 cluster */
    uint64_t pris = rc == FL_OK ? avant.free_bytes - apres.free_bytes : 0;
    tm_apply_file(v.m, "/g.bin", data, 5000, 0, 0, 0644);
    int j = rc == FL_OK ? juger(&v) : -1;
    liberer(&v);
    TT_ASSERT_EQ(rc, FL_OK);
    if (pris < 5000 || pris > 5000 + 8192) fprintf(stderr, "%s : %llu octets pris\n", IMGS[i][0], (unsigned long long)pris);
    TT_ASSERT(pris >= 5000 && pris <= 5000 + 8192);
    TT_ASSERT_EQ(j, 0);
  }
  free(data);
}

static void test_fat_fichier_4gio(void) {
  vol v;
  TT_ASSERT_EQ(ouvrir("fat32-vide.img", NULL, &v), FL_OK);
  fl_file *f = NULL;
  int rc = fl_fs_open(v.fs, "/gros.bin", FL_O_RDWR | FL_O_CREAT, &f);
  time_t t0 = time(NULL);
  uint8_t b = 1;
  int64_t w = rc == FL_OK ? fl_file_pwrite(f, &b, 1, 0xFFFFFFFFull) : rc;
  int t = rc == FL_OK ? fl_file_truncate(f, 0x100000000ull) : rc;
  time_t dt = time(NULL) - t0;
  fl_file_close(f);
  fl_stat st = {0};
  fl_fs_stat(v.fs, "/gros.bin", &st);
  tm_apply_file(v.m, "/gros.bin", "", 0, 0, 0, 0644);
  int j = juger(&v);
  liberer(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(w, FL_EFBIG);
  TT_ASSERT_EQ(t, FL_EFBIG);
  TT_ASSERT(dt <= 1);
  TT_ASSERT_EQ(st.size, 0);
  TT_ASSERT_EQ(j, 0);
}

/* exFAT : fichier de 4 Gio + 2 Mio (1 Mio écrit à 4 Gio + 1 Mio, zéros avant). Long : label lent. */
static void test_fat_exfat_plus_de_4gio(void) {
  vol v;
  TT_ASSERT_EQ(ouvrir("exfat-grand.img", NULL, &v), FL_OK);
  uint8_t *data = motif(MIB, 5);
  uint64_t off = (4ull << 30) + MIB;
  fl_file *f = NULL;
  int rc = fl_fs_open(v.fs, "/plus4g.bin", FL_O_RDWR | FL_O_CREAT, &f);
  if (rc == FL_OK && fl_file_pwrite(f, data, MIB, off) != (int64_t)MIB) rc = FL_EIO;
  uint8_t z[4096], back[4096];
  memset(z, 0, sizeof z);
  if (rc == FL_OK && (fl_file_pread(f, back, sizeof back, 3ull << 30) != (int64_t)sizeof back || memcmp(back, z, sizeof z)))
    rc = FL_EIO;
  if (f && fl_file_close(f) != FL_OK && rc == FL_OK) rc = FL_EIO;
  /* modèle : même contenu dans un fichier creux local (tm_from_dir le lit tranche par tranche) */
  char dir[4096], src[4200], fic[4300];
  int j = -1;
  if (rc == FL_OK && fu_tmpdir(dir, sizeof dir) == 0) {
    snprintf(src, sizeof src, "%s/plus4g-XXXXXX", dir);
    if (mkdtemp(src)) {
      snprintf(fic, sizeof fic, "%s/plus4g.bin", src);
      int fd = open(fic, O_CREAT | O_WRONLY | O_TRUNC, 0644);
      int ok = fd >= 0 && pwrite(fd, data, MIB, (off_t)off) == (ssize_t)MIB;
      if (fd >= 0) close(fd);
      tm_free(v.m);
      v.m = tm_new();
      if (ok && v.m && tm_from_dir(v.m, src) == 0) {
        tm_set_fat_attrs(v.m);
        j = juger(&v);
      }
      unlink(fic);
      rmdir(src);
    }
  }
  free(data);
  liberer(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(j, 0);
}

static void test_fat_partage_lecteur_ecrivain(void) {
  uint8_t *data = motif(64 * 1024, 3), back[64 * 1024];
  for (size_t i = 0; i < 2; i++) {
    vol v;
    int rc = ouvrir(IMGS[i][0], IMGS[i][1], &v);
    fl_file *w = NULL, *r = NULL;
    if (rc == FL_OK) rc = fl_fs_open(v.fs, "/p.bin", FL_O_RDWR | FL_O_CREAT, &w);
    if (rc == FL_OK) rc = fl_fs_open(v.fs, "/P.BIN", FL_O_RDONLY, &r);
    if (rc == FL_OK && fl_file_pwrite(w, data, sizeof back, 0) != (int64_t)sizeof back) rc = FL_EIO;
    if (rc == FL_OK && fl_file_pread(r, back, sizeof back, 0) != (int64_t)sizeof back) rc = FL_EIO;
    int same = rc == FL_OK && !memcmp(back, data, sizeof back);
    fl_file_close(r);
    if (w && fl_file_close(w) != FL_OK && rc == FL_OK) rc = FL_EIO;
    tm_apply_file(v.m, "/p.bin", data, sizeof back, 0, 0, 0644);
    int j = rc == FL_OK ? juger(&v) : -1;
    liberer(&v);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT(same);
    TT_ASSERT_EQ(j, 0);
  }
  free(data);
}

static void test_fat_partage_deux_ecrivains(void) {
  uint8_t *data = motif(64 * 1024, 9);
  for (size_t i = 0; i < 2; i++) {
    vol v;
    int rc = ouvrir(IMGS[i][0], IMGS[i][1], &v);
    for (int ordre = 0; rc == FL_OK && ordre < 2; ordre++) {
      const char *p = ordre ? "/d2.bin" : "/d1.bin";
      fl_file *a = NULL, *b = NULL;
      rc = fl_fs_open(v.fs, p, FL_O_RDWR | FL_O_CREAT, &a);
      if (rc == FL_OK) rc = fl_fs_open(v.fs, p, FL_O_RDWR, &b);
      if (rc == FL_OK && fl_file_pwrite(b, data + 32768, 32768, 32768) != 32768) rc = FL_EIO;
      if (rc == FL_OK && fl_file_pwrite(a, data, 32768, 0) != 32768) rc = FL_EIO;
      int ca = FL_OK, cb = FL_OK;
      if (ordre) {
        ca = fl_file_close(a);
        cb = fl_file_close(b);
      } else {
        cb = fl_file_close(b);
        ca = fl_file_close(a);
      }
      if (rc == FL_OK && (ca != FL_OK || cb != FL_OK)) rc = FL_EIO;
      tm_apply_file(v.m, p, data, 65536, 0, 0, 0644);
    }
    int j = rc == FL_OK ? juger(&v) : -1;
    liberer(&v);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT_EQ(j, 0);
  }
  free(data);
}

/* Drapeau « sale » lu directement dans l'image (hors moteur). */
static int drapeau_sale(const char *img, int exfat) {
  uint8_t b[512];
  int fd = open(img, O_RDONLY);
  if (fd < 0) return -1;
  ssize_t r = pread(fd, b, sizeof b, 0);
  close(fd);
  if (r != (ssize_t)sizeof b) return -1;
  return exfat ? (b[106] & 2) != 0 : (b[0x41] & 1) != 0;
}

static void test_fat_drapeau_sale(void) {
  for (int ex = 0; ex < 2; ex++) {
    vol v;
    int rc = ouvrir(ex ? "exfat-vide.img" : "fat32-vide.img", NULL, &v);
    int etats[5] = {-1, -1, -1, -1, -1};
    etats[0] = drapeau_sale(v.copie, ex);
    fl_file *f = NULL;
    if (rc == FL_OK) rc = fl_fs_open(v.fs, "/s.txt", FL_O_RDWR | FL_O_CREAT, &f);
    if (rc == FL_OK && fl_file_pwrite(f, "x", 1, 0) != 1) rc = FL_EIO;
    etats[1] = drapeau_sale(v.copie, ex);
    if (rc == FL_OK) rc = fl_file_close(f);
    if (rc == FL_OK) rc = fl_fs_sync(v.fs);
    etats[2] = drapeau_sale(v.copie, ex);
    fl_file *g = NULL;
    if (rc == FL_OK) rc = fl_fs_open(v.fs, "/t.txt", FL_O_RDWR | FL_O_CREAT, &g);
    etats[3] = drapeau_sale(v.copie, ex);
    if (g && fl_file_close(g) != FL_OK && rc == FL_OK) rc = FL_EIO;
    tm_apply_file(v.m, "/s.txt", "x", 1, 0, 0, 0644);
    tm_apply_file(v.m, "/t.txt", "", 0, 0, 0, 0644);
    int j = rc == FL_OK ? juger(&v) : -1;
    etats[4] = drapeau_sale(v.copie, ex);
    /* remontage en écriture : pas de lecture seule */
    fl_statfs st = {0};
    fl_bdev *d = NULL;
    fl_fs *fs = NULL;
    if (fl_file_bdev_open(v.copie, 512, false, &d) == FL_OK &&
        fl_fs_mount(d, ex ? FL_CONTENT_EXFAT : FL_CONTENT_FAT32, true, &fs) == FL_OK)
      fl_fs_statfs(fs, &st);
    else
      st.read_only = true;
    fl_fs_unmount(fs);
    fl_bdev_close(d);
    liberer(&v);
    TT_ASSERT_EQ(rc, FL_OK);
    if (etats[0] || !etats[1] || etats[2] || !etats[3] || etats[4])
      fprintf(stderr, "%s : drapeau %d %d %d %d %d\n", ex ? "exFAT" : "FAT32", etats[0], etats[1], etats[2], etats[3],
              etats[4]);
    TT_ASSERT(etats[0] == 0 && etats[1] == 1 && etats[2] == 0 && etats[3] == 1 && etats[4] == 0);
    TT_ASSERT_EQ(j, 0);
    TT_ASSERT(!st.read_only);
  }
}

static void test_fat_attribut_lecture_seule(void) {
  static const char *const imgs[][2] = {{"fat16-arbre.img", "tree-fat"}, {"exfat-arbre.img", "tree-exfat"}};
  for (size_t i = 0; i < 2; i++) {
    char copie[4096], out[512];
    TT_ASSERT(fu_copy_image(tt_fixture(imgs[i][0]), copie, sizeof copie) == 0);
    int s = fu_fatjuge(out, sizeof out, "set-ro", copie, "/vide.txt");
    fl_bdev *d = NULL;
    fl_fs *fs = NULL;
    fl_stat st = {0};
    fl_file *f = NULL;
    int rc = s == 0 && fl_file_bdev_open(copie, 512, false, &d) == FL_OK ? FL_OK : FL_EIO;
    fl_probe_result pr;
    if (rc == FL_OK) rc = fl_probe(d, &pr);
    if (rc == FL_OK) rc = fl_fs_mount(d, pr.content, true, &fs);
    if (rc == FL_OK) rc = fl_fs_stat(fs, "/vide.txt", &st);
    int o = rc == FL_OK ? fl_fs_open(fs, "/vide.txt", FL_O_RDWR, &f) : rc;
    fl_file_close(f);
    fl_fs_unmount(fs);
    fl_bdev_close(d);
    unlink(copie);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT_EQ(st.mode, 0100444);
    TT_ASSERT_EQ(o, FL_EROFS);
  }
}

static void test_fat_disque_plein(void) {
  static const char *const imgs[][2] = {{"fat16-arbre.img", "tree-fat"}, {"exfat-vide.img", NULL}};
  uint8_t *data = motif(MIB, 21), *back = malloc(MIB);
  TT_ASSERT(data && back);
  for (size_t i = 0; i < 2; i++) {
    vol v;
    int rc = ouvrir(imgs[i][0], imgs[i][1], &v);
    fl_statfs avant = {0}, apres = {0};
    if (rc == FL_OK) rc = fl_fs_statfs(v.fs, &avant);
    fl_file *f = NULL;
    if (rc == FL_OK) rc = fl_fs_open(v.fs, "/plein.bin", FL_O_RDWR | FL_O_CREAT, &f);
    uint64_t k = 0;
    int64_t w = 0;
    while (rc == FL_OK && (w = fl_file_pwrite(f, data, MIB, k * MIB)) == (int64_t)MIB) k++;
    int c = f ? fl_file_close(f) : FL_OK;
    fl_stat st = {0};
    int s = fl_fs_stat(v.fs, "/plein.bin", &st);
    int lu = s == FL_OK && k > 0 ? relire(v.fs, "/plein.bin", back, MIB) : FL_EIO;
    int same = lu == FL_OK && !memcmp(back, data, MIB);
    fl_file *t = NULL;
    int u = fl_fs_open(v.fs, "/plein.bin", FL_O_RDWR | FL_O_TRUNC, &t);
    if (t && fl_file_close(t) != FL_OK && u == FL_OK) u = FL_EIO;
    tm_apply_file(v.m, "/plein.bin", "", 0, 0, 0, 0644);
    if (rc == FL_OK) rc = fl_fs_statfs(v.fs, &apres);
    int j = rc == FL_OK ? juger(&v) : -1;
    liberer(&v);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT_EQ(w, FL_ENOSPC);
    TT_ASSERT(k > 0);
    TT_ASSERT_EQ(c, FL_OK);
    TT_ASSERT_EQ(s, FL_OK);
    TT_ASSERT(st.size >= k * MIB && st.size < (k + 1) * MIB);
    TT_ASSERT(same);
    TT_ASSERT_EQ(u, FL_OK);
    TT_ASSERT_EQ(apres.free_bytes, avant.free_bytes);
    TT_ASSERT_EQ(j, 0);
  }
  free(data);
  free(back);
}

static uint8_t *lire_image(const char *p, size_t *n) {
  struct stat sb;
  if (stat(p, &sb) != 0) return NULL;
  uint8_t *b = malloc((size_t)sb.st_size);
  int fd = open(p, O_RDONLY);
  ssize_t r = b && fd >= 0 ? pread(fd, b, (size_t)sb.st_size, 0) : -1;
  if (fd >= 0) close(fd);
  if (r != sb.st_size) {
    free(b);
    return NULL;
  }
  *n = (size_t)sb.st_size;
  return b;
}

static void test_fat_lecture_seule_tient(void) {
  static const char *const imgs[] = {"fat32-sale.img", "exfat-sale.img"};
  for (size_t i = 0; i < 2; i++) {
    vol v;
    int rc = ouvrir(imgs[i], NULL, &v);
    size_t n1 = 0, n2 = 0;
    uint8_t *avant = lire_image(v.copie, &n1);
    fl_file *f = NULL;
    int o = rc == FL_OK ? fl_fs_open(v.fs, "/gros.bin", FL_O_RDWR, &f) : rc;
    int c = rc == FL_OK ? fl_fs_open(v.fs, "/n.txt", FL_O_RDWR | FL_O_CREAT, &f) : rc;
    int mk = rc == FL_OK ? fl_fs_mkdir(v.fs, "/d") : rc;
    int u = rc == FL_OK ? fl_fs_unlink(v.fs, "/vide.txt") : rc;
    int rn = rc == FL_OK ? fl_fs_rename(v.fs, "/abc.txt", "/b.txt") : rc;
    int um = fl_fs_unmount(v.fs);
    v.fs = NULL;
    uint8_t *apres = lire_image(v.copie, &n2);
    int same = avant && apres && n1 == n2 && !memcmp(avant, apres, n1);
    free(avant);
    free(apres);
    liberer(&v);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT_EQ(o, FL_EROFS);
    TT_ASSERT_EQ(c, FL_EROFS);
    TT_ASSERT_EQ(mk, FL_EROFS);
    TT_ASSERT_EQ(u, FL_EROFS);
    TT_ASSERT_EQ(rn, FL_EROFS);
    TT_ASSERT_EQ(um, FL_OK);
    TT_ASSERT(same);
  }
}

int main(int argc, char **argv) {
  if (!fu_fat_tools_available()) {
    printf("SAUTÉ : outils FAT/exFAT absents ou plateforme Android (banc réservé au PC)\n");
    return 77;
  }
  setenv("TZ", "UTC", 1);
  tzset();
  if (argc > 1 && !strcmp(argv[1], "lent")) {
    TT_RUN(test_fat_exfat_plus_de_4gio);
    TT_MAIN_END();
  }
  TT_RUN(test_fat_creer_ecrire);
  TT_RUN(test_fat_trou);
  TT_RUN(test_fat_tronquer);
  TT_RUN(test_fat_fichier_4gio);
  TT_RUN(test_fat_partage_lecteur_ecrivain);
  TT_RUN(test_fat_partage_deux_ecrivains);
  TT_RUN(test_fat_drapeau_sale);
  TT_RUN(test_fat_attribut_lecture_seule);
  TT_RUN(test_fat_disque_plein);
  TT_RUN(test_fat_lecture_seule_tient);
  TT_MAIN_END();
}
