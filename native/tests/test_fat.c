#define _GNU_SOURCE
/* FAT12/16/32 et exFAT par FatFs (phase 4) : montage, statfs, politique de lecture seule, lecture.
 * Tourne aussi sur Android (fixtures poussées) : les écritures ne portent que sur des copies, dans le
 * dossier temporaire du banc. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fault_bdev.h"
#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fluluks/luks.h"
#include "fluluks/probe.h"
#include "fs/ro_reasons.h"
#include "fsck_util.h"
#include "tree_model.h"
#include "tt.h"

/* Un volume monté et ce qui le porte. */
typedef struct {
  char copie[4096]; /* "" si la fixture est montée directement (lecture seule) */
  fl_bdev *dev;
  fl_fs *fs;
} vol;

/* Monte une fixture : copie (want_rw) ou image elle-même en lecture seule. ss : taille de secteur du
 * périphérique. Rend le code de fl_fs_mount (ou FL_EIO si la copie ou l'ouverture échoue). */
static int monter_ss(const char *fixture, uint32_t ss, bool want_rw, vol *v) {
  memset(v, 0, sizeof *v);
  const char *src = tt_fixture(fixture);
  if (want_rw) {
    if (fu_copy_image(src, v->copie, sizeof v->copie) != 0) return FL_EIO;
    src = v->copie;
  }
  if (fl_file_bdev_open(src, ss, !want_rw, &v->dev) != FL_OK) return FL_EIO;
  fl_probe_result pr;
  if (fl_probe(v->dev, &pr) != FL_OK) return FL_EIO;
  return fl_fs_mount(v->dev, pr.content, want_rw, &v->fs);
}

static int monter(const char *fixture, bool want_rw, vol *v) { return monter_ss(fixture, 512, want_rw, v); }

static void demonter(vol *v) {
  if (v->fs && fl_fs_unmount(v->fs) != FL_OK) fprintf(stderr, "démontage refusé\n");
  v->fs = NULL;
  fl_bdev_close(v->dev);
  v->dev = NULL;
  if (v->copie[0]) unlink(v->copie);
  v->copie[0] = 0;
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* Octets de données (clusters × taille de cluster), calculés depuis le secteur de démarrage. */
static uint64_t octets_donnees(fl_bdev *dev) {
  uint8_t b[512];
  if (fl_bdev_read_bytes(dev, 0, sizeof b, b) != FL_OK) return 0;
  if (!memcmp(b + 3, "EXFAT   ", 8)) return (uint64_t)le32(b + 92) << (b[108] + b[109]);
  uint32_t bps = le16(b + 11), spc = b[13], rsvd = le16(b + 14), nfats = b[16], rootent = le16(b + 17);
  uint32_t tot = le16(b + 19) ? le16(b + 19) : le32(b + 32), fatsz = le16(b + 22) ? le16(b + 22) : le32(b + 36);
  uint32_t data = rsvd + nfats * fatsz + (rootent * 32 + bps - 1) / bps;
  return (uint64_t)((tot - data) / spc) * spc * bps;
}

static void test_fat_montage_types(void) {
  static const struct {
    const char *img, *label;
  } cas[] = {{"fat12-arbre.img", "FLUFAT12"}, {"fat16-arbre.img", "FLUFAT16"}, {"fat32-arbre.img", "FLUFAT32"},
             {"fat32-4k.img", "FLUFAT4K"},    {"exfat-arbre.img", "FLUEXFAT"}};
  for (size_t i = 0; i < sizeof cas / sizeof cas[0]; i++) {
    vol v;
    int rc = monter(cas[i].img, true, &v);
    if (rc != FL_OK) fprintf(stderr, "%s : montage = %d\n", cas[i].img, rc);
    if (rc != FL_OK) demonter(&v);
    TT_ASSERT_EQ(rc, FL_OK);
    fl_statfs st;
    rc = fl_fs_statfs(v.fs, &st);
    uint64_t attendu = octets_donnees(v.dev);
    demonter(&v);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT(!st.read_only);
    TT_ASSERT_STR_EQ(st.ro_reason, "");
    TT_ASSERT_STR_EQ(st.label, cas[i].label);
    TT_ASSERT_EQ(st.total_bytes, attendu);
    TT_ASSERT(st.free_bytes > 0 && st.free_bytes < st.total_bytes);
  }
}

static void test_fat_libre(void) {
  if (!fu_fat_tools_available()) return; /* juge absent (Android) */
  vol v;
  TT_ASSERT_EQ(monter("fat32-vide.img", true, &v), FL_OK);
  fl_statfs st;
  int rc = fl_fs_statfs(v.fs, &st);
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(st.free_bytes, fu_fat_free(tt_fixture("fat32-vide.img")));
}

static int compter(void *ctx, const fl_dirent *e) {
  (void)e;
  (*(size_t *)ctx)++;
  return 0;
}

/* Volume en écriture refusée pour `raison`, mais lisible. */
static void attendre_lecture_seule(const char *img, uint32_t ss, const char *raison) {
  vol v;
  int rc = monter_ss(img, ss, true, &v);
  if (rc != FL_OK) fprintf(stderr, "%s : montage = %d\n", img, rc);
  if (rc != FL_OK) demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  fl_statfs st;
  rc = fl_fs_statfs(v.fs, &st);
  int mk = fl_fs_mkdir(v.fs, "/nouveau");
  fl_stat fst;
  int lisible = fl_fs_readdir(v.fs, "/", compter, &(size_t){0});
  if (lisible == FL_OK && strcmp(img, "fat32-512-sur-4k.img")) lisible = fl_fs_stat(v.fs, "/gros.bin", &fst);
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(lisible, FL_OK);
  if (!st.read_only || strcmp(st.ro_reason, raison)) fprintf(stderr, "%s : « %s »\n", img, st.ro_reason);
  TT_ASSERT(st.read_only);
  TT_ASSERT_STR_EQ(st.ro_reason, raison);
  TT_ASSERT_EQ(mk, FL_EROFS);
}

static void test_fat_sale(void) {
  static const char *const sales[] = {"fat16-sale.img", "fat32-sale.img", "fat32-fat1-sale.img", "exfat-sale.img"};
  for (size_t i = 0; i < sizeof sales / sizeof sales[0]; i++) attendre_lecture_seule(sales[i], 512, FL_RO_DIRTY);
  attendre_lecture_seule("fat32-media.img", 512, FL_RO_MEDIA);
  attendre_lecture_seule("exfat-media.img", 512, FL_RO_MEDIA);
  attendre_lecture_seule("exfat-somme.img", 512, FL_RO_CSUM);
}

static void test_fat_secteur_512_sur_4k(void) { attendre_lecture_seule("fat32-512-sur-4k.img", 4096, FL_RO_SECTOR); }

static void test_fat_lecture_seule_demandee(void) {
  vol v;
  TT_ASSERT_EQ(monter("fat32-arbre.img", false, &v), FL_OK);
  fl_statfs st;
  int rc = fl_fs_statfs(v.fs, &st);
  int mk = fl_fs_mkdir(v.fs, "/x");
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT(st.read_only);
  TT_ASSERT_STR_EQ(st.ro_reason, FL_RO_USER);
  TT_ASSERT_EQ(mk, FL_EROFS);
}

static void test_fat_emplacements(void) {
  vol v[9];
  for (int tour = 0; tour < 2; tour++) {
    for (int i = 0; i < 8; i++) TT_ASSERT_EQ(monter("exfat-arbre.img", false, &v[i]), FL_OK);
    int rc = monter("fat16-arbre.img", false, &v[8]);
    demonter(&v[8]);
    for (int i = 0; i < 8; i++) demonter(&v[i]);
    TT_ASSERT_EQ(rc, FL_EBUSY);
    vol c;
    rc = monter("sig-exfat.img", false, &c);
    demonter(&c);
    TT_ASSERT_EQ(rc, FL_ECORRUPT);
  }
}

static void test_fat_raisons_tiennent(void) {
  TT_ASSERT(strlen(FL_RO_DIRTY) <= 127);
  TT_ASSERT(strlen(FL_RO_MEDIA) <= 127);
  TT_ASSERT(strlen(FL_RO_SECTOR) <= 127);
}

/* --- lecture (T3) ---------------------------------------------------------------------------- */

/* Modèle d'une arborescence source, attributs FAT ; sans mille/ ni gros.bin si petit (FAT12). */
static tm *modele(const char *arbre, int petit) {
  tm *m = tm_new();
  char src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture(arbre));
  if (!m || tm_from_dir(m, src) != 0) {
    tm_free(m);
    return NULL;
  }
  tm_set_fat_attrs(m);
  if (petit) {
    char p[64];
    for (int i = 0; i < 1000; i++) {
      snprintf(p, sizeof p, "/mille/f%04d.txt", i);
      tm_apply_unlink(m, p);
    }
    tm_apply_rmdir(m, "/mille");
    tm_apply_unlink(m, "/gros.bin");
  }
  return m;
}

static int verifier_arbre(fl_fs *fs, const char *arbre, int petit) {
  tm *m = modele(arbre, petit);
  if (!m) return -1;
  int d = tm_check_fl(m, fs);
  tm_free(m);
  return d;
}

static void arbre(const char *img, const char *src, int petit) {
  vol v;
  int rc = monter(img, false, &v);
  if (rc != FL_OK) demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  int d = verifier_arbre(v.fs, src, petit);
  demonter(&v);
  if (d) fprintf(stderr, "%s : %d écart(s)\n", img, d);
  TT_ASSERT_EQ(d, 0);
}

static void test_fat_arbre_fat12(void) { arbre("fat12-arbre.img", "tree-fat", 1); }
static void test_fat_arbre_fat16(void) { arbre("fat16-arbre.img", "tree-fat", 0); }
static void test_fat_arbre_fat32(void) { arbre("fat32-arbre.img", "tree-fat", 0); }
static void test_fat_arbre_fat32_4k(void) { arbre("fat32-4k.img", "tree-fat", 0); }
static void test_fat_arbre_exfat(void) { arbre("exfat-arbre.img", "tree-exfat", 0); }

static void test_fat_arbre_luks2(void) {
  fl_bdev *img = NULL, *dec = NULL;
  fl_luks *l = NULL;
  fl_fs *fs = NULL;
  char pass[256];
  FILE *f = fopen(tt_fixture("pass.txt"), "rb");
  size_t n = f ? fread(pass, 1, sizeof pass, f) : 0;
  if (f) fclose(f);
  int rc = n ? fl_file_bdev_open(tt_fixture("luks2-fat32.img"), 512, true, &img) : FL_EIO;
  if (rc == FL_OK) rc = fl_luks_open(img, &l);
  if (rc == FL_OK) rc = fl_luks_unlock(l, pass, n, &dec);
  fl_probe_result pr = {0};
  if (rc == FL_OK) rc = fl_probe(dec, &pr);
  if (rc == FL_OK) rc = fl_fs_mount(dec, pr.content, false, &fs);
  int d = rc == FL_OK ? verifier_arbre(fs, "tree-fat", 0) : -1;
  fl_fs_unmount(fs);
  fl_bdev_close(dec);
  fl_luks_close(l);
  fl_bdev_close(img);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(pr.content, FL_CONTENT_FAT32);
  TT_ASSERT_EQ(d, 0);
}

typedef struct {
  char noms[64][256];
  size_t n;
} liste;

static int lister(void *ctx, const fl_dirent *e) {
  liste *l = ctx;
  if (l->n < 64) snprintf(l->noms[l->n++], 256, "%s", e->name);
  return 0;
}

static int contient(const liste *l, const char *nom) {
  for (size_t i = 0; i < l->n; i++)
    if (!strcmp(l->noms[i], nom)) return 1;
  return 0;
}

static void test_fat_noms(void) {
  vol v;
  TT_ASSERT_EQ(monter("fat32-arbre.img", false, &v), FL_OK);
  liste *l = calloc(1, sizeof *l);
  int rc = l ? fl_fs_readdir(v.fs, "/", lister, l) : FL_ENOMEM;
  char n255[300] = "/";
  for (int i = 0; i < 125; i++) strcat(n255, "é");
  strcat(n255, "n.txt");
  fl_stat st;
  int s255 = fl_fs_stat(v.fs, n255, &st), sabc = fl_fs_stat(v.fs, "/ABC.TXT", &st),
      smix = fl_fs_stat(v.fs, "/mixed.txt", &st);
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  size_t n = l->n;
  int ok = contient(l, "été ☀.txt") && contient(l, "abc.txt") && contient(l, "UPPER.TXT") &&
           contient(l, "MiXeD.TxT") && contient(l, "nom long 1.txt") && contient(l, n255 + 1) &&
           !contient(l, ".") && !contient(l, "..");
  free(l);
  TT_ASSERT_EQ(n, 11); /* 9 fichiers, mille/ et p1/ */
  TT_ASSERT(ok);
  TT_ASSERT_EQ(s255, FL_OK);
  TT_ASSERT_EQ(sabc, FL_OK);
  TT_ASSERT_EQ(smix, FL_OK);
}

static void test_fat_mtime(void) {
  static const char *const imgs[] = {"fat32-arbre.img", "exfat-arbre.img"};
  for (size_t i = 0; i < 2; i++) {
    vol v;
    TT_ASSERT_EQ(monter(imgs[i], false, &v), FL_OK);
    fl_stat st;
    int rc = fl_fs_stat(v.fs, "/vide.txt", &st);
    demonter(&v);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT_EQ(st.mtime, 1760000000); /* 1760000001 (source), arrondi aux 2 s */
    TT_ASSERT_EQ(st.type, FL_FT_FILE);
    TT_ASSERT_EQ(st.size, 0);
  }
}

static void test_fat_types(void) {
  vol v;
  TT_ASSERT_EQ(monter("exfat-arbre.img", false, &v), FL_OK);
  fl_file *f = NULL;
  fl_stat st;
  int o = fl_fs_open(v.fs, "/mille", FL_O_RDONLY, &f);
  int s1 = fl_fs_stat(v.fs, "/gros.bin/x", &st);
  int r1 = fl_fs_readdir(v.fs, "/gros.bin", compter, &(size_t){0});
  int s2 = fl_fs_stat(v.fs, "/absent", &st);
  int s3 = fl_fs_stat(v.fs, "/", &st);
  demonter(&v);
  TT_ASSERT_EQ(o, FL_EISDIR);
  TT_ASSERT_EQ(s1, FL_ENOTDIR);
  TT_ASSERT_EQ(r1, FL_ENOTDIR);
  TT_ASSERT_EQ(s2, FL_ENOENT);
  TT_ASSERT_EQ(s3, FL_OK);
  TT_ASSERT_EQ(st.type, FL_FT_DIR);
  TT_ASSERT_EQ(st.mode, 040755);
}

typedef struct {
  fl_fs *fs;
  size_t vus, stat_ok, arret;
} rappel;

static int rappeler(void *ctx, const fl_dirent *e) {
  rappel *r = ctx;
  r->vus++;
  char p[512];
  snprintf(p, sizeof p, "/mille/%s", e->name);
  fl_stat st;
  if (fl_fs_stat(r->fs, p, &st) == FL_OK && st.size == e->st.size) r->stat_ok++;
  return r->arret && r->vus == r->arret ? 7 : 0;
}

static void test_fat_readdir_rappel(void) {
  vol v;
  TT_ASSERT_EQ(monter("fat16-arbre.img", false, &v), FL_OK);
  rappel r = {v.fs, 0, 0, 0};
  int rc = fl_fs_readdir(v.fs, "/mille", rappeler, &r);
  rappel r2 = {v.fs, 0, 0, 150};
  int rc2 = fl_fs_readdir(v.fs, "/mille", rappeler, &r2);
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(r.vus, 1000);
  TT_ASSERT_EQ(r.stat_ok, 1000);
  TT_ASSERT_EQ(rc2, 7);
  TT_ASSERT_EQ(r2.vus, 150);
}

/* Contenu attendu de gros.bin : relu depuis l'arbre source. */
static uint8_t *source_gros(void) {
  char p[4096];
  snprintf(p, sizeof p, "%s", tt_fixture("tree-fat/gros.bin"));
  FILE *f = fopen(p, "rb");
  uint8_t *b = malloc(3 << 20);
  if (!f || !b || fread(b, 1, 3 << 20, f) != 3 << 20) {
    if (f) fclose(f);
    free(b);
    return NULL;
  }
  fclose(f);
  return b;
}

static void test_fat_deux_lecteurs(void) {
  uint8_t *src = source_gros();
  TT_ASSERT(src != NULL);
  vol v;
  TT_ASSERT_EQ(monter("exfat-arbre.img", false, &v), FL_OK);
  fl_file *a = NULL, *b = NULL;
  int ok = fl_fs_open(v.fs, "/gros.bin", FL_O_RDONLY, &a) == FL_OK &&
           fl_fs_open(v.fs, "/GROS.BIN", FL_O_RDONLY, &b) == FL_OK;
  uint8_t x[5000], y[5000];
  for (int i = 0; ok && i < 50; i++) {
    uint64_t oa = (uint64_t)i * 61000, ob = (3u << 20) - 5000 - (uint64_t)i * 47000;
    ok = fl_file_pread(a, x, sizeof x, oa) == (int64_t)sizeof x && fl_file_pread(b, y, sizeof y, ob) == (int64_t)sizeof y &&
         !memcmp(x, src + oa, sizeof x) && !memcmp(y, src + ob, sizeof y);
  }
  int64_t fin = ok ? fl_file_pread(a, x, 10, 3u << 20) : -1;
  int64_t court = ok ? fl_file_pread(b, x, 100, (3u << 20) - 30) : -1;
  fl_file_close(a);
  fl_file_close(b);
  demonter(&v);
  free(src);
  TT_ASSERT(ok);
  TT_ASSERT_EQ(fin, 0);
  TT_ASSERT_EQ(court, 30);
}

static void test_fat_lecture_aleatoire(void) {
  uint8_t *src = source_gros();
  TT_ASSERT(src != NULL);
  fl_bdev *img = NULL, *cnt = NULL;
  fl_fs *fs = NULL;
  fl_file *f = NULL;
  int rc = fl_file_bdev_open(tt_fixture("fat32-arbre.img"), 512, true, &img);
  if (rc == FL_OK) rc = fl_fault_bdev_new(img, UINT64_MAX, FL_EIO, &cnt);
  if (rc == FL_OK) rc = fl_fs_mount(cnt, FL_CONTENT_FAT32, false, &fs);
  if (rc == FL_OK) rc = fl_fs_open(fs, "/gros.bin", FL_O_RDONLY, &f);
  uint8_t buf[4096];
  uint32_t x = 12345;
  int ok = rc == FL_OK && fl_file_pread(f, buf, sizeof buf, 0) == (int64_t)sizeof buf;
  uint64_t avant = cnt ? fl_fault_bdev_reads(cnt) : 0;
  for (int i = 0; ok && i < 500; i++) {
    x = x * 1103515245u + 12345u;
    uint64_t off = (uint64_t)(x % ((3u << 20) - sizeof buf));
    ok = fl_file_pread(f, buf, sizeof buf, off) == (int64_t)sizeof buf && !memcmp(buf, src + off, sizeof buf);
  }
  uint64_t lectures = cnt ? fl_fault_bdev_reads(cnt) - avant : 0;
  /* taille de cluster : secteurs par cluster × 512 */
  uint8_t bs[512] = {0};
  fl_bdev_read_bytes(img, 0, sizeof bs, bs);
  uint64_t par_pread = sizeof buf / ((uint64_t)bs[13] * 512) + 2;
  fl_file_close(f);
  fl_fs_unmount(fs);
  fl_bdev_close(cnt);
  fl_bdev_close(img);
  free(src);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT(ok);
  if (lectures > 500 * par_pread) fprintf(stderr, "%llu lectures pour 500 pread\n", (unsigned long long)lectures);
  TT_ASSERT(lectures <= 500 * par_pread);
}

int main(void) {
  setenv("TZ", "UTC", 1);
  tzset();
  TT_RUN(test_fat_montage_types);
  TT_RUN(test_fat_libre);
  TT_RUN(test_fat_sale);
  TT_RUN(test_fat_secteur_512_sur_4k);
  TT_RUN(test_fat_lecture_seule_demandee);
  TT_RUN(test_fat_emplacements);
  TT_RUN(test_fat_raisons_tiennent);
  TT_RUN(test_fat_arbre_fat12);
  TT_RUN(test_fat_arbre_fat16);
  TT_RUN(test_fat_arbre_fat32);
  TT_RUN(test_fat_arbre_fat32_4k);
  TT_RUN(test_fat_arbre_exfat);
  TT_RUN(test_fat_arbre_luks2);
  TT_RUN(test_fat_noms);
  TT_RUN(test_fat_mtime);
  TT_RUN(test_fat_types);
  TT_RUN(test_fat_readdir_rappel);
  TT_RUN(test_fat_deux_lecteurs);
  TT_RUN(test_fat_lecture_aleatoire);
  TT_MAIN_END();
}
