#define _GNU_SOURCE
/* NTFS par libntfs-3g (phase 5) : montage, statfs, politique de lecture seule, lecture. Tourne aussi sur
 * Android (fixtures poussées) : les écritures ne portent que sur des copies, dans le dossier temporaire
 * du banc ; les comparaisons au juge ntfsjuge.py sont réservées au PC. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* Un volume monté et ce qui le porte : image (copie si want_rw), compteur d'écritures, système de fichiers. */
typedef struct {
  char copie[4096]; /* "" si la fixture est montée directement (lecture seule) */
  fl_bdev *img, *dev;
  fl_fs *fs;
} vol;

static int monter_ss(const char *fixture, uint32_t ss, bool want_rw, vol *v) {
  memset(v, 0, sizeof *v);
  const char *src = tt_fixture(fixture);
  if (want_rw) {
    if (fu_copy_image(src, v->copie, sizeof v->copie) != 0) return FL_EIO;
    src = v->copie;
  }
  if (fl_file_bdev_open(src, ss, !want_rw, &v->img) != FL_OK) return FL_EIO;
  if (fl_fault_bdev_new(v->img, UINT64_MAX, FL_EIO, &v->dev) != FL_OK) return FL_EIO;
  v->dev->read_only = !want_rw;
  return fl_fs_mount(v->dev, FL_CONTENT_NTFS, want_rw, &v->fs);
}

static int monter(const char *fixture, bool want_rw, vol *v) { return monter_ss(fixture, 512, want_rw, v); }

/* Démonte (code rendu), écritures comptées dans *ecritures si non NULL ; la copie reste pour l'appelant. */
static int demonter_garder(vol *v, uint64_t *ecritures) {
  int rc = v->fs ? fl_fs_unmount(v->fs) : FL_OK;
  v->fs = NULL;
  if (ecritures) *ecritures = v->dev ? fl_fault_bdev_writes(v->dev) : 0;
  fl_bdev_close(v->dev);
  fl_bdev_close(v->img);
  v->dev = v->img = NULL;
  return rc;
}

static void demonter(vol *v) {
  if (demonter_garder(v, NULL) != FL_OK) fprintf(stderr, "démontage refusé\n");
  if (v->copie[0]) unlink(v->copie);
  v->copie[0] = 0;
}

/* 0 si les deux fichiers sont identiques octet pour octet (cmp n'existe pas partout, Android compris). */
static int comparer_fichiers(const char *a, const char *b) {
  FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
  int rc = fa && fb ? 0 : -1;
  static char ba[1 << 16], bb[1 << 16];
  while (rc == 0) {
    size_t na = fread(ba, 1, sizeof ba, fa), nb = fread(bb, 1, sizeof bb, fb);
    if (na != nb || memcmp(ba, bb, na)) rc = 1;
    if (na == 0) break;
  }
  if (fa) fclose(fa);
  if (fb) fclose(fb);
  return rc;
}

static int compter(void *ctx, const fl_dirent *e) {
  (void)e;
  (*(size_t *)ctx)++;
  return 0;
}

static void test_ntfs_raisons_tiennent(void) {
  TT_ASSERT(strlen(FL_RO_HIBER) <= 127);
  TT_ASSERT(strlen(FL_RO_LOGFILE) <= 127);
  TT_ASSERT(strlen(FL_RO_VERSION) <= 127);
  TT_ASSERT(strlen(FL_RO_TAILLE) <= 127);
  TT_ASSERT(fl_content_supported(FL_CONTENT_NTFS));
}

static void test_ntfs_monte_arbre(void) {
  vol v;
  int rc = monter("ntfs-arbre.img", true, &v);
  TT_ASSERT_EQ(rc, FL_OK);
  fl_statfs st;
  rc = fl_fs_statfs(v.fs, &st);
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT(!st.read_only);
  TT_ASSERT_STR_EQ(st.ro_reason, "");
  TT_ASSERT_STR_EQ(st.label, "FLUNTFS");
  TT_ASSERT_EQ(st.total_bytes, (uint64_t)16383 * 4096); /* 64 Mio : 16 383 clusters (mkntfs) */
  TT_ASSERT(st.free_bytes > 0 && st.free_bytes < st.total_bytes);
  if (fu_ntfs_tools_available()) TT_ASSERT_EQ((int64_t)st.free_bytes, fu_ntfs_free(tt_fixture("ntfs-arbre.img")));
}

/* Lecture seule pour `raison`, et aucune écriture entre le montage et le démontage. */
static void attendre_lecture_seule(const char *img, uint32_t ss, const char *raison) {
  vol v;
  int rc = monter_ss(img, ss, true, &v);
  if (rc != FL_OK) fprintf(stderr, "%s : montage = %d\n", img, rc);
  TT_ASSERT_EQ(rc, FL_OK);
  fl_statfs st;
  rc = fl_fs_statfs(v.fs, &st);
  int mk = fl_fs_mkdir(v.fs, "/nouveau");
  size_t n = 0;
  int lisible = fl_fs_readdir(v.fs, "/", compter, &n);
  uint64_t w = 0;
  int um = demonter_garder(&v, &w);
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  if (!st.read_only || strcmp(st.ro_reason, raison)) fprintf(stderr, "%s : « %s »\n", img, st.ro_reason);
  TT_ASSERT(st.read_only);
  TT_ASSERT_STR_EQ(st.ro_reason, raison);
  TT_ASSERT_EQ(mk, FL_EROFS);
  TT_ASSERT_EQ(lisible, FL_OK);
  TT_ASSERT_EQ(um, FL_OK);
  TT_ASSERT_EQ(w, 0);
}

static void test_ntfs_politique_sale(void) { attendre_lecture_seule("ntfs-sale.img", 512, FL_RO_DIRTY); }
static void test_ntfs_politique_chkdsk(void) { attendre_lecture_seule("ntfs-chkdsk.img", 512, FL_RO_DIRTY); }
static void test_ntfs_politique_hiber(void) { attendre_lecture_seule("ntfs-hiber.img", 512, FL_RO_HIBER); }
static void test_ntfs_politique_journal(void) { attendre_lecture_seule("ntfs-journal.img", 512, FL_RO_LOGFILE); }
/* Windows 8 et suivants, démarrage rapide : $LogFile propre mais en version 2.0 (métadonnées gardées en cache). */
static void test_ntfs_politique_demarrage_rapide(void) {
  attendre_lecture_seule("ntfs-rapide.img", 512, FL_RO_HIBER);
}
/* Volume de 32 Mio sur un disque de 28 Mio (clone sur un disque plus petit, capacité mal annoncée). */
static void test_ntfs_politique_tronque(void) { attendre_lecture_seule("ntfs-tronque.img", 512, FL_RO_TAILLE); }
static void test_ntfs_politique_secteur(void) { attendre_lecture_seule("ntfs-512-sur-4k.img", 4096, FL_RO_SECTOR); }

static void test_ntfs_politique_utilisateur(void) {
  vol v;
  TT_ASSERT_EQ(monter("ntfs-arbre.img", false, &v), FL_OK);
  fl_statfs st;
  int rc = fl_fs_statfs(v.fs, &st);
  uint64_t w = 0;
  demonter_garder(&v, &w);
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT(st.read_only);
  TT_ASSERT_STR_EQ(st.ro_reason, FL_RO_USER);
  TT_ASSERT_EQ(w, 0);
}

static void test_ntfs_4k(void) {
  vol v;
  TT_ASSERT_EQ(monter_ss("ntfs-4k.img", 4096, true, &v), FL_OK);
  fl_statfs st;
  int rc = fl_fs_statfs(v.fs, &st);
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT(!st.read_only);
  TT_ASSERT_STR_EQ(st.label, "FLUNTFS4K");
}

/* Monter en écriture puis démonter sans rien faire : aucune écriture, image identique. */
static void test_ntfs_demontage_propre(void) {
  vol v;
  TT_ASSERT_EQ(monter("ntfs-arbre.img", true, &v), FL_OK);
  uint64_t w = 1;
  int rc = demonter_garder(&v, &w);
  int diff = comparer_fichiers(tt_fixture("ntfs-arbre.img"), v.copie);
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(w, 0);
  TT_ASSERT_EQ(diff, 0);
}

static void test_ntfs_pas_ntfs(void) {
  vol v;
  int rc = monter("fat32.img", false, &v);
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_ECORRUPT);
  rc = monter("ext4-plain.img", false, &v);
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_ECORRUPT);
}


/* --- lecture (T3) ---------------------------------------------------------------------------- */

static tm *modele(const char *arbre) {
  tm *m = tm_new();
  char src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture(arbre));
  if (!m || tm_from_dir(m, src) != 0) {
    tm_free(m);
    return NULL;
  }
  tm_set_ntfs_attrs(m);
  return m;
}

/* ntfs-4k : l'arbre seul (sans spéciaux), sur un disque de 4096 o. */
static void test_ntfs_arbre_modele(void) {
  vol v;
  TT_ASSERT_EQ(monter_ss("ntfs-4k.img", 4096, false, &v), FL_OK);
  tm *m = modele("tree-ntfs");
  int d = m ? tm_check_fl(m, v.fs) : -1;
  tm_free(m);
  demonter(&v);
  TT_ASSERT_EQ(d, 0);
}

typedef struct {
  char noms[64][700];
  fl_dirent e[64];
  size_t n;
} liste;

static int cb_liste(void *ctx, const fl_dirent *e) {
  liste *l = ctx;
  if (l->n < 64) {
    snprintf(l->noms[l->n], sizeof l->noms[l->n], "%s", e->name);
    l->e[l->n++] = *e;
  }
  return 0;
}

static const fl_dirent *trouver(const liste *l, const char *nom) {
  for (size_t i = 0; i < l->n; i++)
    if (!strcmp(l->noms[i], nom)) return &l->e[i];
  return NULL;
}

/* Lit tout le fichier (au plus cap octets) ; octets lus ou fl_err. */
static int64_t lire_tout(fl_fs *fs, const char *path, void *buf, size_t cap) {
  fl_file *f = NULL;
  int rc = fl_fs_open(fs, path, FL_O_RDONLY, &f);
  if (rc != FL_OK) return rc;
  int64_t got = 0;
  for (;;) {
    int64_t n = fl_file_pread(f, (char *)buf + got, cap - (size_t)got, (uint64_t)got);
    if (n < 0) {
      got = n;
      break;
    }
    if (n == 0 || (size_t)(got += n) == cap) break;
  }
  fl_file_close(f);
  return got;
}

static void test_ntfs_speciaux(void) {
  vol v;
  TT_ASSERT_EQ(monter("ntfs-arbre.img", false, &v), FL_OK);
  liste *l = calloc(1, sizeof *l);
  int rc = fl_fs_readdir(v.fs, "/", cb_liste, l);
  char buf[300 * 1024];
  int64_t abc = lire_tout(v.fs, "/abc.txt", buf, sizeof buf);
  int64_t dur = lire_tout(v.fs, "/lien-dur.txt", buf + 100, 100);
  int same = abc == 11 && dur == 11 && !memcmp(buf, buf + 100, 11) && !memcmp(buf, "minuscules\n", 11);
  int64_t comp = lire_tout(v.fs, "/compressé.txt", buf, sizeof buf);
  int comp_ok = comp == 256 * 1024;
  static const char motif[] = "FluLuks NTFS\n";
  for (int64_t i = 0; comp_ok && i < comp; i++) comp_ok = buf[i] == motif[i % (sizeof motif - 1)];
  fl_stat sc, sl, sj, sd;
  int r1 = fl_fs_stat(v.fs, "/clairsemé.bin", &sc), r2 = fl_fs_stat(v.fs, "/lien", &sl);
  int r3 = fl_fs_stat(v.fs, "/jonction", &sj), r4 = fl_fs_stat(v.fs, "/dédup.bin", &sd);
  fl_file *f = NULL;
  int o1 = fl_fs_open(v.fs, "/lien", FL_O_RDONLY, &f), o2 = fl_fs_open(v.fs, "/dédup.bin", FL_O_RDONLY, &f);
  char debut[16], fin[11];
  int64_t z = -1, e = -1;
  if (fl_fs_open(v.fs, "/clairsemé.bin", FL_O_RDONLY, &f) == FL_OK) {
    z = fl_file_pread(f, debut, sizeof debut, 0);
    e = fl_file_pread(f, fin, sizeof fin, 5368709120ull - 11);
    fl_file_close(f);
  }
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT(trouver(l, "abc.txt") && trouver(l, "lien-dur.txt") && trouver(l, "été ☀"));
  TT_ASSERT(!trouver(l, "$MFT") && !trouver(l, "$Extend") && !trouver(l, "."));
  for (size_t i = 0; i < l->n; i++) TT_ASSERT(strncmp(l->noms[i], "nom-trop-long-", 14) != 0);
  TT_ASSERT_EQ(l->n, 19); /* tree-ntfs (13 à la racine) + 6 spéciaux visibles */
  TT_ASSERT_EQ(trouver(l, "abc.txt")->st.size, 11); /* le flux nommé n'est pas compté */
  TT_ASSERT_EQ(trouver(l, "lien")->st.type, FL_FT_SYMLINK);
  free(l);
  TT_ASSERT(same);
  TT_ASSERT(comp_ok);
  TT_ASSERT(r1 == FL_OK && r2 == FL_OK && r3 == FL_OK && r4 == FL_OK);
  TT_ASSERT_EQ(sc.size, 5368709120ull);
  TT_ASSERT(sl.type == FL_FT_SYMLINK && sl.mode == 0120777 && sl.size == 0);
  TT_ASSERT(sj.type == FL_FT_SYMLINK && sj.mode == 0120777);
  TT_ASSERT(sd.type == FL_FT_OTHER && sd.mode == 0100000);
  TT_ASSERT_EQ(o1, FL_ENOTSUP);
  TT_ASSERT_EQ(o2, FL_ENOTSUP);
  TT_ASSERT_EQ(z, (int64_t)sizeof debut);
  TT_ASSERT_MEM_EQ(debut, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", sizeof debut);
  TT_ASSERT_EQ(e, 11);
  TT_ASSERT_MEM_EQ(fin, "FIN-FLULUKS", 11);
}

static void test_ntfs_metafichiers(void) {
  vol v;
  TT_ASSERT_EQ(monter("ntfs-arbre.img", false, &v), FL_OK);
  fl_stat st;
  int a = fl_fs_stat(v.fs, "/$MFT", &st), b = fl_fs_stat(v.fs, "/$Extend/$Reparse", &st);
  int c = fl_fs_readdir(v.fs, "/$Extend", cb_liste, &(liste){0});
  int d = fl_fs_stat(v.fs, "/abc.txt/x", &st), e = fl_fs_stat(v.fs, "/absent/x", &st);
  demonter(&v);
  TT_ASSERT_EQ(a, FL_ENOENT);
  TT_ASSERT_EQ(b, FL_ENOENT);
  TT_ASSERT_EQ(c, FL_ENOENT);
  TT_ASSERT_EQ(d, FL_ENOTDIR);
  TT_ASSERT_EQ(e, FL_ENOENT);
}

static void test_ntfs_casse(void) {
  vol v;
  TT_ASSERT_EQ(monter("ntfs-casse.img", false, &v), FL_OK);
  liste *l = calloc(1, sizeof *l);
  int rc = fl_fs_readdir(v.fs, "/", cb_liste, l);
  char a[8] = {0}, b[8] = {0}, c[8] = {0};
  int64_t na = lire_tout(v.fs, "/Casse.txt", a, sizeof a), nb = lire_tout(v.fs, "/casse.txt", b, sizeof b);
  int64_t nc = lire_tout(v.fs, "/CASSE.TXT", c, sizeof c);
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT(trouver(l, "Casse.txt") && trouver(l, "casse.txt"));
  free(l);
  TT_ASSERT(na == 2 && !memcmp(a, "A\n", 2));
  TT_ASSERT(nb == 2 && !memcmp(b, "b\n", 2));
  TT_ASSERT(nc == 2 && (!memcmp(c, "A\n", 2) || !memcmp(c, "b\n", 2)));
}

static void test_ntfs_chiffre(void) {
  vol v;
  TT_ASSERT_EQ(monter("ntfs-chiffre.img", false, &v), FL_OK);
  fl_stat st;
  int s = fl_fs_stat(v.fs, "/abc.txt", &st);
  char b[16];
  int64_t r = lire_tout(v.fs, "/abc.txt", b, sizeof b);
  demonter(&v);
  TT_ASSERT_EQ(s, FL_OK);
  TT_ASSERT_EQ(st.type, FL_FT_FILE);
  TT_ASSERT_EQ(r, FL_ENOTSUP);
}

static void test_ntfs_mtime(void) {
  vol v;
  TT_ASSERT_EQ(monter("ntfs-arbre.img", false, &v), FL_OK);
  fl_stat st;
  int rc = fl_fs_stat(v.fs, "/vide.txt", &st);
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(st.mtime, 1760000001); /* UTC, à la seconde (pas d'arrondi à 2 s comme en FAT) */
  TT_ASSERT_EQ(st.size, 0);
  TT_ASSERT_EQ(st.mode, 0100644);
}

static void test_ntfs_mille(void) {
  vol v;
  TT_ASSERT_EQ(monter("ntfs-arbre.img", false, &v), FL_OK);
  uint64_t avant = fl_fault_bdev_reads(v.dev);
  size_t n = 0;
  int rc = fl_fs_readdir(v.fs, "/mille", compter, &n);
  uint64_t lectures = fl_fault_bdev_reads(v.dev) - avant;
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(n, 1000);
  if (lectures >= 2000) fprintf(stderr, "/mille : %llu lectures\n", (unsigned long long)lectures);
  TT_ASSERT(lectures < 2000);
}

static void test_ntfs_lecture_aleatoire(void) {
  vol v;
  TT_ASSERT_EQ(monter("ntfs-arbre.img", false, &v), FL_OK);
  char src[4096];
  snprintf(src, sizeof src, "%s/gros.bin", tt_fixture("tree-ntfs"));
  FILE *g = fopen(src, "rb");
  TT_ASSERT(g != NULL);
  uint8_t *ref = malloc(3 << 20);
  size_t nref = fread(ref, 1, 3 << 20, g);
  fclose(g);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/gros.bin", FL_O_RDONLY, &f), FL_OK);
  uint64_t avant = fl_fault_bdev_reads(v.dev);
  unsigned graine = 42;
  int faux = 0;
  for (int i = 0; i < 500; i++) {
    uint64_t off = (uint64_t)(rand_r(&graine) % (nref - 4096));
    uint8_t b[4096];
    if (fl_file_pread(f, b, sizeof b, off) != (int64_t)sizeof b || memcmp(b, ref + off, sizeof b)) faux++;
  }
  uint64_t lectures = fl_fault_bdev_reads(v.dev) - avant;
  fl_file_close(f);
  demonter(&v);
  free(ref);
  TT_ASSERT_EQ(nref, (size_t)3 << 20);
  TT_ASSERT_EQ(faux, 0);
  if (lectures >= 1500) fprintf(stderr, "500 pread : %llu lectures\n", (unsigned long long)lectures);
  TT_ASSERT(lectures < 1500);
}

static void test_ntfs_luks2(void) {
  fl_bdev *img = NULL, *dec = NULL;
  fl_luks *l = NULL;
  fl_fs *fs = NULL;
  char pass[256];
  FILE *f = fopen(tt_fixture("pass.txt"), "rb");
  size_t n = f ? fread(pass, 1, sizeof pass, f) : 0;
  if (f) fclose(f);
  int rc = n ? fl_file_bdev_open(tt_fixture("luks2-ntfs.img"), 512, true, &img) : FL_EIO;
  if (rc == FL_OK) rc = fl_luks_open(img, &l);
  if (rc == FL_OK) rc = fl_luks_unlock(l, pass, n, &dec);
  fl_probe_result pr = {0};
  if (rc == FL_OK) rc = fl_probe(dec, &pr);
  if (rc == FL_OK) rc = fl_fs_mount(dec, pr.content, false, &fs);
  char b[16];
  int64_t r = rc == FL_OK ? lire_tout(fs, "/abc.txt", b, sizeof b) : -1;
  size_t racine = 0;
  if (rc == FL_OK) fl_fs_readdir(fs, "/", compter, &racine);
  fl_fs_unmount(fs);
  fl_bdev_close(dec);
  fl_luks_close(l);
  fl_bdev_close(img);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(pr.content, FL_CONTENT_NTFS);
  TT_ASSERT(r == 11 && !memcmp(b, "minuscules\n", 11));
  TT_ASSERT_EQ(racine, 19);
}

int main(void) {
  TT_RUN(test_ntfs_raisons_tiennent);
  TT_RUN(test_ntfs_monte_arbre);
  TT_RUN(test_ntfs_politique_sale);
  TT_RUN(test_ntfs_politique_chkdsk);
  TT_RUN(test_ntfs_politique_hiber);
  TT_RUN(test_ntfs_politique_journal);
  TT_RUN(test_ntfs_politique_demarrage_rapide);
  TT_RUN(test_ntfs_politique_secteur);
  TT_RUN(test_ntfs_politique_tronque);
  TT_RUN(test_ntfs_politique_utilisateur);
  TT_RUN(test_ntfs_4k);
  TT_RUN(test_ntfs_demontage_propre);
  TT_RUN(test_ntfs_pas_ntfs);
  TT_RUN(test_ntfs_arbre_modele);
  TT_RUN(test_ntfs_speciaux);
  TT_RUN(test_ntfs_metafichiers);
  TT_RUN(test_ntfs_casse);
  TT_RUN(test_ntfs_chiffre);
  TT_RUN(test_ntfs_mtime);
  TT_RUN(test_ntfs_mille);
  TT_RUN(test_ntfs_lecture_aleatoire);
  TT_RUN(test_ntfs_luks2);
  TT_MAIN_END();
}
