#define _GNU_SOURCE
/* NTFS en écriture (phase 5, T4–T6) : chaque test écrit sur une copie, démonte, puis passe les juges
 * (ntfsfix -n, ntfsjuge.py check et manifeste contre le modèle). Réservé au PC : sauté sans les outils, et
 * sur Android. */
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

#define MIB ((size_t)1 << 20)

typedef struct {
  char copie[4096];
  fl_bdev *img, *dev; /* image, et compteur / panne par-dessus */
  fl_fs *fs;
  tm *m;
} vol;

/* Copie de la fixture, montée en écriture sur un disque de ss octets par secteur, avec son modèle
 * (arborescence source, ou racine seule) ; writes_ok, err : panne de fault_bdev (UINT64_MAX : aucune). */
static int ouvrir_panne(const char *fixture, uint32_t ss, const char *arbre, uint64_t writes_ok, int err, vol *v) {
  memset(v, 0, sizeof *v);
  if (fu_copy_image(tt_fixture(fixture), v->copie, sizeof v->copie) != 0) return FL_EIO;
  if (fl_file_bdev_open(v->copie, ss, false, &v->img) != FL_OK) return FL_EIO;
  if (fl_fault_bdev_new(v->img, writes_ok, err, &v->dev) != FL_OK) return FL_EIO;
  v->m = tm_new();
  if (!v->m) return FL_ENOMEM;
  if (arbre) {
    char src[4096];
    snprintf(src, sizeof src, "%s", tt_fixture(arbre));
    if (tm_from_dir(v->m, src) != 0) return FL_EIO;
  }
  tm_set_ntfs_attrs(v->m);
  return fl_fs_mount(v->dev, FL_CONTENT_NTFS, true, &v->fs);
}

static int ouvrir(const char *fixture, uint32_t ss, const char *arbre, vol *v) {
  return ouvrir_panne(fixture, ss, arbre, UINT64_MAX, FL_EIO, v);
}

static int fermer_disque(vol *v) {
  int rc = v->fs ? fl_fs_unmount(v->fs) : FL_OK;
  v->fs = NULL;
  fl_bdev_close(v->dev);
  fl_bdev_close(v->img);
  v->dev = v->img = NULL;
  return rc;
}

static void liberer(vol *v) {
  fermer_disque(v);
  tm_free(v->m);
  v->m = NULL;
  if (v->copie[0]) unlink(v->copie);
  v->copie[0] = 0;
}

/* Démonte puis juge : 0 si ntfsfix -n et ntfsjuge.py (cohérence et manifeste) sont d'accord avec le modèle. */
static int juger(vol *v) {
  int rc = fermer_disque(v);
  if (rc != FL_OK) {
    fprintf(stderr, "%s : démontage = %d\n", v->copie, rc);
    return -1;
  }
  char out[8192];
  int f = fu_ntfsfix_n(v->copie, out, sizeof out);
  if (f != 0) fprintf(stderr, "%s : ntfsfix = %d\n%s\n", v->copie, f, out);
  int d = tm_check_ntfs_judge(v->m, v->copie);
  if (d != 0) fprintf(stderr, "%s : juge = %d\n", v->copie, d);
  return f == 0 && d == 0 ? 0 : -1;
}

/* Démonte puis vérifie seulement la cohérence (ntfsfix -n, ntfsjuge.py check). */
static int juger_coherence(vol *v) {
  int rc = fermer_disque(v);
  char out[8192];
  int f = fu_ntfsfix_n(v->copie, out, sizeof out);
  if (f != 0) fprintf(stderr, "%s : ntfsfix = %d\n%s\n", v->copie, f, out);
  int c = fu_ntfsjuge(out, sizeof out, "check", v->copie, NULL);
  if (c != 0) fprintf(stderr, "%s : check = %d\n%s\n", v->copie, c, out);
  return rc == FL_OK && f == 0 && c == 0 ? 0 : -1;
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

static uint8_t *motif(size_t n, uint32_t graine) {
  uint8_t *b = malloc(n ? n : 1);
  for (size_t i = 0; b && i < n; i++) {
    graine = graine * 1103515245u + 12345u;
    b[i] = (uint8_t)(graine >> 16);
  }
  return b;
}

/* Images de départ des tests génériques : NTFS vide (secteurs de 512 o), NTFS rempli (4096 o). */
static const struct {
  const char *img;
  uint32_t ss;
  const char *arbre;
} IMGS[] = {{"ntfs-vide.img", 512, NULL}, {"ntfs-4k.img", 4096, "tree-ntfs"}};
#define NIMGS (sizeof IMGS / sizeof IMGS[0])

/* --- T4 : fichiers ------------------------------------------------------------------------------ */

static void test_ntfs_creer_ecrire(void) {
  size_t n = 3 * MIB;
  uint8_t *data = motif(n, 7), *back = malloc(n);
  TT_ASSERT(data && back);
  for (size_t i = 0; i < NIMGS; i++) {
    vol v;
    int rc = ouvrir(IMGS[i].img, IMGS[i].ss, IMGS[i].arbre, &v);
    fl_file *f = NULL;
    if (rc == FL_OK) rc = fl_fs_open(v.fs, "/a.txt", FL_O_RDWR | FL_O_CREAT, &f);
    for (size_t off = 0; rc == FL_OK && off < n; off += 128 * 1024) {
      int64_t w = fl_file_pwrite(f, data + off, 128 * 1024, off);
      if (w != 128 * 1024) rc = w < 0 ? (int)w : FL_EIO;
    }
    if (f && fl_file_close(f) != FL_OK && rc == FL_OK) rc = FL_EIO;
    if (rc == FL_OK) rc = relire(v.fs, "/a.txt", back, n);
    int same = rc == FL_OK && !memcmp(data, back, n);
    tm_apply_file(v.m, "/a.txt", data, n, 0, 0, 0644);
    int j = rc == FL_OK ? juger(&v) : -1;
    liberer(&v);
    if (rc != FL_OK || !same || j) fprintf(stderr, "%s : rc %d\n", IMGS[i].img, rc);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT(same);
    TT_ASSERT_EQ(j, 0);
  }
  free(data);
  free(back);
}

/* Écrase le milieu, ajoute à la fin, puis tronque vers le bas puis vers le haut. */
static void test_ntfs_ecrasement_ajout_troncature(void) {
  size_t n = 200000;
  uint8_t *data = motif(n, 3), *neuf = motif(5000, 9), *back = malloc(n + 100000);
  TT_ASSERT(data && neuf && back);
  for (size_t i = 0; i < NIMGS; i++) {
    vol v;
    int rc = ouvrir(IMGS[i].img, IMGS[i].ss, IMGS[i].arbre, &v);
    if (rc == FL_OK) rc = ecrire_fichier(v.fs, "/b.bin", data, n);
    fl_file *f = NULL;
    if (rc == FL_OK) rc = fl_fs_open(v.fs, "/b.bin", FL_O_RDWR, &f);
    if (rc == FL_OK && fl_file_pwrite(f, neuf, 5000, 70001) != 5000) rc = FL_EIO;  /* milieu */
    if (rc == FL_OK && fl_file_pwrite(f, neuf, 5000, n) != 5000) rc = FL_EIO;      /* ajout */
    if (rc == FL_OK) rc = fl_file_truncate(f, 123457);                              /* vers le bas */
    if (rc == FL_OK) rc = fl_file_truncate(f, 300000);                              /* vers le haut */
    if (f && fl_file_close(f) != FL_OK && rc == FL_OK) rc = FL_EIO;
    memcpy(data + 70001, neuf, 5000);
    uint8_t *att = calloc(1, 300000);
    memcpy(att, data, 123457);
    if (rc == FL_OK) rc = relire(v.fs, "/b.bin", back, 300000);
    int same = rc == FL_OK && !memcmp(att, back, 300000);
    tm_apply_file(v.m, "/b.bin", att, 300000, 0, 0, 0644);
    free(att);
    int j = rc == FL_OK ? juger(&v) : -1;
    liberer(&v);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT(same);
    TT_ASSERT_EQ(j, 0);
  }
  free(data);
  free(neuf);
  free(back);
}

/* 4 Kio à 5 Gio dans un fichier vide d'un volume de 300 Mio : fichier creux. */
static void test_ntfs_trou_lointain(void) {
  vol v;
  int rc = ouvrir("ntfs-vide.img", 512, NULL, &v);
  uint8_t *data = motif(4096, 11), b[4096];
  uint64_t loin = 5ull << 30;
  fl_file *f = NULL;
  if (rc == FL_OK) rc = fl_fs_open(v.fs, "/loin.bin", FL_O_RDWR | FL_O_CREAT, &f);
  if (rc == FL_OK && fl_file_pwrite(f, data, 4096, loin) != 4096) rc = FL_EIO;
  int64_t z = -1, e = -1;
  int zeros = 0;
  if (rc == FL_OK) {
    z = fl_file_pread(f, b, sizeof b, loin / 2);
    zeros = z == 4096 && !memcmp(b, (uint8_t[4096]){0}, 4096);
    e = fl_file_pread(f, b, sizeof b, loin);
  }
  if (f && fl_file_close(f) != FL_OK && rc == FL_OK) rc = FL_EIO;
  fl_stat st = {0};
  if (rc == FL_OK) rc = fl_fs_stat(v.fs, "/loin.bin", &st);
  int j = rc == FL_OK ? juger_coherence(&v) : -1;
  liberer(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(st.size, loin + 4096);
  TT_ASSERT(zeros);
  TT_ASSERT_EQ(e, 4096);
  TT_ASSERT_MEM_EQ(b, data, 4096);
  TT_ASSERT_EQ(j, 0);
  free(data);
}

static int64_t horloge_fixe(void) { return 1800000000; }

static void test_ntfs_horodatage(void) {
  vol v;
  int rc = ouvrir("ntfs-vide.img", 512, NULL, &v);
  fl_fs_set_clock(horloge_fixe);
  if (rc == FL_OK) rc = ecrire_fichier(v.fs, "/h.txt", "heure\n", 6);
  fl_fs_set_clock(NULL);
  fl_stat sf = {0}, sd = {0};
  if (rc == FL_OK) rc = fl_fs_stat(v.fs, "/h.txt", &sf);
  if (rc == FL_OK) rc = fl_fs_stat(v.fs, "/", &sd); /* le dossier parent */
  liberer(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(sf.mtime, 1800000000);
  TT_ASSERT_EQ(sd.mtime, 1800000000);
}

static void test_ntfs_drapeau_sale(void) {
  vol v;
  int rc = ouvrir("ntfs-vide.img", 512, NULL, &v);
  fl_file *f = NULL;
  if (rc == FL_OK) rc = fl_fs_open(v.fs, "/s.txt", FL_O_RDWR | FL_O_CREAT, &f);
  if (rc == FL_OK && fl_file_pwrite(f, "sale\n", 5, 0) != 5) rc = FL_EIO;
  int pendant = rc == FL_OK ? fu_ntfs_flags(v.copie) : -1;
  if (f && fl_file_close(f) != FL_OK && rc == FL_OK) rc = FL_EIO;
  int apres = rc == FL_OK ? fu_ntfs_flags(v.copie) : -1;
  tm_apply_file(v.m, "/s.txt", "sale\n", 5, 0, 0, 0644);
  int j = rc == FL_OK ? juger(&v) : -1;
  int fin = fu_ntfs_flags(v.copie);
  liberer(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(pendant, 0x0001);
  TT_ASSERT_EQ(apres, 0);
  TT_ASSERT_EQ(fin, 0);
  TT_ASSERT_EQ(j, 0);
}

static void test_ntfs_disque_plein(void) {
  vol v;
  int rc = ouvrir("ntfs-512-sur-4k.img", 512, NULL, &v); /* 32 Mio, ouverte ici sur 512 o : écriture permise */
  uint8_t *data = motif(MIB, 5);
  fl_file *f = NULL;
  if (rc == FL_OK) rc = fl_fs_open(v.fs, "/plein.bin", FL_O_RDWR | FL_O_CREAT, &f);
  int64_t w = 0;
  uint64_t off = 0;
  for (int i = 0; rc == FL_OK && i < 64; i++) {
    w = fl_file_pwrite(f, data, MIB, off);
    if (w != (int64_t)MIB) break;
    off += MIB;
  }
  int tronque = f ? fl_file_truncate(f, 0) : FL_EIO;
  if (f && fl_file_close(f) != FL_OK && rc == FL_OK) rc = FL_EIO;
  fl_statfs st = {0};
  if (rc == FL_OK) rc = fl_fs_statfs(v.fs, &st);
  tm_apply_file(v.m, "/plein.bin", "", 0, 0, 0, 0644);
  int j = rc == FL_OK ? juger(&v) : -1;
  liberer(&v);
  free(data);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(w, FL_ENOSPC);
  TT_ASSERT(off >= 16 * MIB && off < 32 * MIB);
  TT_ASSERT_EQ(tronque, FL_OK);
  TT_ASSERT(!st.read_only);
  TT_ASSERT(st.free_bytes > 16 * MIB);
  TT_ASSERT_EQ(j, 0);
}

static void test_ntfs_compresse_lecture_seule(void) {
  vol v;
  int rc = ouvrir("ntfs-arbre.img", 512, NULL, &v);
  fl_file *f = NULL;
  int o = rc == FL_OK ? fl_fs_open(v.fs, "/compressé.txt", FL_O_RDWR, &f) : rc;
  if (o == FL_OK) fl_file_close(f);
  rc = fermer_disque(&v);
  int diff = comparer_fichiers(tt_fixture("ntfs-arbre.img"), v.copie);
  liberer(&v);
  TT_ASSERT_EQ(o, FL_ENOTSUP);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(diff, 0);
}

static int cb_taille(void *ctx, const fl_dirent *e) {
  if (!strcmp(e->name, "p.bin")) *(uint64_t *)ctx = e->st.size;
  return 0;
}

static void test_ntfs_partage_lecteur_ecrivain(void) {
  vol v;
  int rc = ouvrir("ntfs-vide.img", 512, NULL, &v);
  uint8_t *data = motif(300000, 13), b[1000];
  fl_file *w = NULL, *r = NULL;
  if (rc == FL_OK) rc = fl_fs_open(v.fs, "/p.bin", FL_O_RDWR | FL_O_CREAT, &w);
  if (rc == FL_OK) rc = fl_fs_open(v.fs, "/p.bin", FL_O_RDONLY, &r);
  int vu = 1;
  uint64_t taille_liste = 0;
  for (size_t off = 0; rc == FL_OK && off < 300000; off += 100000) {
    if (fl_file_pwrite(w, data + off, 100000, off) != 100000) rc = FL_EIO;
    if (rc == FL_OK && (fl_file_pread(r, b, sizeof b, off + 99000) != (int64_t)sizeof b ||
                        memcmp(b, data + off + 99000, sizeof b)))
      vu = 0;
    fl_stat st;
    if (rc == FL_OK && (fl_fs_stat(v.fs, "/p.bin", &st) != FL_OK || st.size != off + 100000)) vu = 0;
    if (rc == FL_OK) fl_fs_readdir(v.fs, "/", cb_taille, &taille_liste);
    if (taille_liste != off + 100000) vu = 0;
  }
  if (w && fl_file_close(w) != FL_OK && rc == FL_OK) rc = FL_EIO;
  if (r && fl_file_close(r) != FL_OK && rc == FL_OK) rc = FL_EIO;
  tm_apply_file(v.m, "/p.bin", data, 300000, 0, 0, 0644);
  int j = rc == FL_OK ? juger(&v) : -1;
  liberer(&v);
  free(data);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT(vu);
  TT_ASSERT_EQ(j, 0);
}

static void test_ntfs_ecriture_erreur_io(void) {
  /* première passe : écritures faites jusqu'à l'ouverture (création comprise) ; seconde passe : panne
   * deux écritures plus loin, pendant les données */
  uint8_t *data = motif(MIB, 17);
  vol v;
  int rc = ouvrir("ntfs-vide.img", 512, NULL, &v);
  fl_file *f = NULL;
  if (rc == FL_OK) rc = fl_fs_open(v.fs, "/e.bin", FL_O_RDWR | FL_O_CREAT, &f);
  uint64_t w0 = v.dev ? fl_fault_bdev_writes(v.dev) : 0;
  if (f) fl_file_close(f);
  liberer(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  rc = ouvrir_panne("ntfs-vide.img", 512, NULL, w0 + 2, FL_EIO, &v);
  f = NULL;
  if (rc == FL_OK) rc = fl_fs_open(v.fs, "/e.bin", FL_O_RDWR | FL_O_CREAT, &f);
  int64_t w = FL_OK;
  for (size_t off = 0; rc == FL_OK && off < MIB && w >= 0; off += 64 * 1024) w = fl_file_pwrite(f, data + off, 64 * 1024, off);
  int c = f ? fl_file_close(f) : FL_EIO;
  fl_statfs st = {0};
  int s = v.fs ? fl_fs_statfs(v.fs, &st) : FL_EIO;
  int um = v.fs ? fl_fs_unmount(v.fs) : FL_EIO;
  v.fs = NULL;
  liberer(&v);
  free(data);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT(w == FL_EIO || c == FL_EIO);
  TT_ASSERT_EQ(s, FL_OK);
  TT_ASSERT(st.read_only);
  TT_ASSERT_STR_EQ(st.ro_reason, FL_RO_IOERR);
  TT_ASSERT_EQ(um, FL_EIO);
}


/* --- T5 : dossiers et noms ----------------------------------------------------------------------- */

static void test_ntfs_mkdir_rmdir(void) {
  for (size_t i = 0; i < NIMGS; i++) {
    vol v;
    int rc = ouvrir(IMGS[i].img, IMGS[i].ss, IMGS[i].arbre, &v);
    if (rc == FL_OK) rc = fl_fs_mkdir(v.fs, "/d1");
    if (rc == FL_OK) rc = fl_fs_mkdir(v.fs, "/d1/d2");
    if (rc == FL_OK) rc = ecrire_fichier(v.fs, "/d1/d2/f.txt", "f\n", 2);
    int existe = rc == FL_OK ? fl_fs_mkdir(v.fs, "/d1") : rc;
    int sans_parent = rc == FL_OK ? fl_fs_mkdir(v.fs, "/absent/x") : rc;
    int non_vide = rc == FL_OK ? fl_fs_rmdir(v.fs, "/d1") : rc;
    int pas_dossier = rc == FL_OK ? fl_fs_rmdir(v.fs, "/d1/d2/f.txt") : rc;
    int racine = rc == FL_OK ? fl_fs_rmdir(v.fs, "/") : rc;
    if (rc == FL_OK) rc = fl_fs_mkdir(v.fs, "/vide");
    if (rc == FL_OK) rc = fl_fs_rmdir(v.fs, "/vide");
    tm_apply_mkdir(v.m, "/d1", 0, 0, 0755);
    tm_apply_mkdir(v.m, "/d1/d2", 0, 0, 0755);
    tm_apply_file(v.m, "/d1/d2/f.txt", "f\n", 2, 0, 0, 0644);
    int j = rc == FL_OK ? juger(&v) : -1;
    liberer(&v);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT_EQ(existe, FL_EEXIST);
    TT_ASSERT_EQ(sans_parent, FL_ENOENT);
    TT_ASSERT_EQ(non_vide, FL_ENOTEMPTY);
    TT_ASSERT_EQ(pas_dossier, FL_ENOTDIR);
    TT_ASSERT_EQ(racine, FL_EINVAL);
    TT_ASSERT_EQ(j, 0);
  }
}

static void test_ntfs_unlink(void) {
  vol v;
  int rc = ouvrir("ntfs-4k.img", 4096, "tree-ntfs", &v);
  fl_file *f = NULL;
  if (rc == FL_OK) rc = fl_fs_open(v.fs, "/abc.txt", FL_O_RDONLY, &f);
  int ouvert = rc == FL_OK ? fl_fs_unlink(v.fs, "/abc.txt") : rc;
  if (f) fl_file_close(f);
  int dossier = rc == FL_OK ? fl_fs_unlink(v.fs, "/p1") : rc;
  if (rc == FL_OK) rc = fl_fs_unlink(v.fs, "/ABC.TXT"); /* sans la casse, comme Windows */
  int absent = rc == FL_OK ? fl_fs_unlink(v.fs, "/abc.txt") : rc;
  for (int k = 0; rc == FL_OK && k < 1000; k++) { /* un gros index qui rétrécit */
    char p[64];
    snprintf(p, sizeof p, "/mille/f%04d.txt", k);
    rc = fl_fs_unlink(v.fs, p);
    tm_apply_unlink(v.m, p);
  }
  if (rc == FL_OK) rc = fl_fs_rmdir(v.fs, "/mille");
  tm_apply_rmdir(v.m, "/mille");
  tm_apply_unlink(v.m, "/abc.txt");
  int j = rc == FL_OK ? juger(&v) : -1;
  liberer(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(ouvert, FL_EBUSY);
  TT_ASSERT_EQ(dossier, FL_EISDIR);
  TT_ASSERT_EQ(absent, FL_ENOENT);
  TT_ASSERT_EQ(j, 0);
}

static void test_ntfs_unlink_lien_dur(void) {
  vol v;
  int rc = ouvrir("ntfs-arbre.img", 512, NULL, &v);
  if (rc == FL_OK) rc = fl_fs_unlink(v.fs, "/abc.txt");
  char b[16] = {0};
  if (rc == FL_OK) rc = relire(v.fs, "/lien-dur.txt", b, 11);
  int j = rc == FL_OK ? juger_coherence(&v) : -1;
  liberer(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_MEM_EQ(b, "minuscules\n", 11);
  TT_ASSERT_EQ(j, 0);
}

static void test_ntfs_rename_deplacer(void) {
  for (size_t i = 0; i < NIMGS; i++) {
    vol v;
    int rc = ouvrir(IMGS[i].img, IMGS[i].ss, IMGS[i].arbre, &v);
    if (rc == FL_OK) rc = fl_fs_mkdir(v.fs, "/src");
    if (rc == FL_OK) rc = fl_fs_mkdir(v.fs, "/src/sous");
    if (rc == FL_OK) rc = ecrire_fichier(v.fs, "/src/sous/f.txt", "contenu\n", 8);
    if (rc == FL_OK) rc = fl_fs_mkdir(v.fs, "/dst");
    if (rc == FL_OK) rc = ecrire_fichier(v.fs, "/a.txt", "a\n", 2);
    if (rc == FL_OK) rc = fl_fs_rename(v.fs, "/a.txt", "/dst/b.txt");        /* fichier, autre dossier */
    if (rc == FL_OK) rc = fl_fs_rename(v.fs, "/src", "/dst/src2");           /* dossier et son contenu */
    if (rc == FL_OK) rc = ecrire_fichier(v.fs, "/dst/src2/sous/g.txt", "g\n", 2); /* le dossier déplacé vit */
    tm_apply_mkdir(v.m, "/dst", 0, 0, 0755);
    tm_apply_mkdir(v.m, "/dst/src2", 0, 0, 0755);
    tm_apply_mkdir(v.m, "/dst/src2/sous", 0, 0, 0755);
    tm_apply_file(v.m, "/dst/src2/sous/f.txt", "contenu\n", 8, 0, 0, 0644);
    tm_apply_file(v.m, "/dst/src2/sous/g.txt", "g\n", 2, 0, 0, 0644);
    tm_apply_file(v.m, "/dst/b.txt", "a\n", 2, 0, 0, 0644);
    int j = rc == FL_OK ? juger(&v) : -1;
    liberer(&v);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT_EQ(j, 0);
  }
}

static void test_ntfs_rename_refus(void) {
  vol v;
  int rc = ouvrir("ntfs-vide.img", 512, NULL, &v);
  if (rc == FL_OK) rc = fl_fs_mkdir(v.fs, "/d");
  if (rc == FL_OK) rc = fl_fs_mkdir(v.fs, "/d/e");
  if (rc == FL_OK) rc = ecrire_fichier(v.fs, "/x.txt", "x\n", 2);
  if (rc == FL_OK) rc = ecrire_fichier(v.fs, "/y.txt", "y\n", 2);
  int existe = rc == FL_OK ? fl_fs_rename(v.fs, "/x.txt", "/y.txt") : rc;
  int existe_casse = rc == FL_OK ? fl_fs_rename(v.fs, "/x.txt", "/Y.TXT") : rc;
  int sous = rc == FL_OK ? fl_fs_rename(v.fs, "/d", "/d/e/f") : rc;
  int sous_casse = rc == FL_OK ? fl_fs_rename(v.fs, "/d", "/D/e/f") : rc;
  int lui = rc == FL_OK ? fl_fs_rename(v.fs, "/d", "/d/g") : rc;
  int absent = rc == FL_OK ? fl_fs_rename(v.fs, "/absent", "/z") : rc;
  int sans_parent = rc == FL_OK ? fl_fs_rename(v.fs, "/x.txt", "/absent/z") : rc;
  tm_apply_mkdir(v.m, "/d", 0, 0, 0755);
  tm_apply_mkdir(v.m, "/d/e", 0, 0, 0755);
  tm_apply_file(v.m, "/x.txt", "x\n", 2, 0, 0, 0644);
  tm_apply_file(v.m, "/y.txt", "y\n", 2, 0, 0, 0644);
  int j = rc == FL_OK ? juger(&v) : -1;
  liberer(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(existe, FL_EEXIST);
  TT_ASSERT_EQ(existe_casse, FL_EEXIST);
  TT_ASSERT_EQ(sous, FL_EINVAL);
  TT_ASSERT_EQ(sous_casse, FL_EINVAL);
  TT_ASSERT_EQ(lui, FL_EINVAL);
  TT_ASSERT_EQ(absent, FL_ENOENT);
  TT_ASSERT_EQ(sans_parent, FL_ENOENT);
  TT_ASSERT_EQ(j, 0);
}

static int cb_noms(void *ctx, const fl_dirent *e) {
  char *acc = ctx;
  strncat(acc, e->name, 4096 - strlen(acc) - 2);
  strcat(acc, "|");
  return 0;
}

static void test_ntfs_rename_casse(void) {
  vol v;
  int rc = ouvrir("ntfs-vide.img", 512, NULL, &v);
  if (rc == FL_OK) rc = ecrire_fichier(v.fs, "/a.txt", "a\n", 2);
  if (rc == FL_OK) rc = fl_fs_mkdir(v.fs, "/dossier");
  if (rc == FL_OK) rc = fl_fs_rename(v.fs, "/a.txt", "/A.txt");
  if (rc == FL_OK) rc = fl_fs_rename(v.fs, "/dossier", "/Dossier");
  if (rc == FL_OK) rc = fl_fs_rename(v.fs, "/A.txt", "/A.txt"); /* lui-même : sans effet */
  char noms[4096] = "";
  if (rc == FL_OK) rc = fl_fs_readdir(v.fs, "/", cb_noms, noms);
  tm_apply_file(v.m, "/A.txt", "a\n", 2, 0, 0, 0644);
  tm_apply_mkdir(v.m, "/Dossier", 0, 0, 0755);
  int j = rc == FL_OK ? juger(&v) : -1;
  liberer(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_STR_EQ(noms, "A.txt|Dossier|");
  TT_ASSERT_EQ(j, 0);
}

static void test_ntfs_noms_interdits(void) {
  static const char *const noms[] = {"/a:b", "/a*b", "/a?b", "/a\"b", "/a<b", "/a>b", "/a|b", "/a\\b",
                                     "/CON", "/nul.txt", "/com1", "/LPT9.log", "/nom.", "/nom ", "/a\x01"};
  vol v;
  int rc = ouvrir("ntfs-vide.img", 512, NULL, &v);
  if (rc == FL_OK) rc = ecrire_fichier(v.fs, "/ok.txt", "ok\n", 3);
  int mauvais = 0;
  for (size_t i = 0; rc == FL_OK && i < sizeof noms / sizeof *noms; i++) {
    fl_file *f = NULL;
    int c = fl_fs_open(v.fs, noms[i], FL_O_RDWR | FL_O_CREAT, &f);
    if (f) fl_file_close(f);
    int m = fl_fs_mkdir(v.fs, noms[i]);
    int r = fl_fs_rename(v.fs, "/ok.txt", noms[i]);
    if (c != FL_EINVAL || m != FL_EINVAL || r != FL_EINVAL) {
      fprintf(stderr, "« %s » : création %d, mkdir %d, renommage %d\n", noms[i], c, m, r);
      mauvais++;
    }
  }
  tm_apply_file(v.m, "/ok.txt", "ok\n", 3, 0, 0, 0644);
  int j = rc == FL_OK ? juger(&v) : -1;
  liberer(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(mauvais, 0);
  TT_ASSERT_EQ(j, 0);
}

static void test_ntfs_noms_casse(void) {
  vol v;
  int rc = ouvrir("ntfs-4k.img", 4096, "tree-ntfs", &v);
  fl_file *f = NULL;
  int c = rc == FL_OK ? fl_fs_open(v.fs, "/ABC.TXT", FL_O_RDWR | FL_O_CREAT, &f) : rc; /* ouvre abc.txt */
  if (f) fl_file_close(f);
  int m = rc == FL_OK ? fl_fs_mkdir(v.fs, "/P1") : rc;
  int j = rc == FL_OK ? juger(&v) : -1;
  liberer(&v);
  TT_ASSERT_EQ(c, FL_OK);
  TT_ASSERT_EQ(m, FL_EEXIST);
  TT_ASSERT_EQ(j, 0);
}

static void test_ntfs_noms_unicode(void) {
  char n255[300] = "/";
  for (int k = 0; k < 125; k++) strcat(n255, "é");
  strcat(n255, "n.txt"); /* 255 octets UTF-8 */
  vol v;
  int rc = ouvrir("ntfs-vide.img", 512, NULL, &v);
  if (rc == FL_OK) rc = ecrire_fichier(v.fs, "/😀.bin", "emoji\n", 6);
  if (rc == FL_OK) rc = ecrire_fichier(v.fs, n255, "long\n", 5);
  if (rc == FL_OK) rc = fl_fs_mkdir(v.fs, "/été ☀");
  tm_apply_file(v.m, "/😀.bin", "emoji\n", 6, 0, 0, 0644);
  tm_apply_file(v.m, n255, "long\n", 5, 0, 0, 0644);
  tm_apply_mkdir(v.m, "/été ☀", 0, 0, 0755);
  int j = rc == FL_OK ? juger(&v) : -1;
  liberer(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(j, 0);
}

static void test_ntfs_metafichiers_proteges(void) {
  vol v;
  int rc = ouvrir("ntfs-vide.img", 512, NULL, &v);
  int u = rc == FL_OK ? fl_fs_unlink(v.fs, "/$MFT") : rc;
  int r = rc == FL_OK ? fl_fs_rename(v.fs, "/$Bitmap", "/x") : rc;
  int d = rc == FL_OK ? fl_fs_rmdir(v.fs, "/$Extend") : rc;
  int j = rc == FL_OK ? juger(&v) : -1;
  liberer(&v);
  TT_ASSERT_EQ(u, FL_ENOENT);
  TT_ASSERT_EQ(r, FL_ENOENT);
  TT_ASSERT_EQ(d, FL_ENOENT);
  TT_ASSERT_EQ(j, 0);
}


/* --- T6 : débit -------------------------------------------------------------------------------- */

/* 32 Mio par pwrite de 128 Kio, puis fermeture : écritures et lectures du disque comptées de l'ouverture à
 * la fermeture comprise. */
static void test_ntfs_debit(void) {
  size_t n = 32 * MIB, pas = 128 * 1024;
  uint8_t *data = motif(n, 21);
  TT_ASSERT(data != NULL);
  vol v;
  int rc = ouvrir("ntfs-vide.img", 512, NULL, &v);
  uint64_t w0 = v.dev ? fl_fault_bdev_writes(v.dev) : 0, r0 = v.dev ? fl_fault_bdev_reads(v.dev) : 0;
  fl_file *f = NULL;
  if (rc == FL_OK) rc = fl_fs_open(v.fs, "/debit.bin", FL_O_RDWR | FL_O_CREAT, &f);
  for (size_t off = 0; rc == FL_OK && off < n; off += pas)
    if (fl_file_pwrite(f, data + off, pas, off) != (int64_t)pas) rc = FL_EIO;
  if (f && fl_file_close(f) != FL_OK && rc == FL_OK) rc = FL_EIO;
  uint64_t w = v.dev ? fl_fault_bdev_writes(v.dev) - w0 : 0, r = v.dev ? fl_fault_bdev_reads(v.dev) - r0 : 0;
  tm_apply_file(v.m, "/debit.bin", data, n, 0, 0, 0644);
  int j = rc == FL_OK ? juger(&v) : -1;
  liberer(&v);
  free(data);
  fprintf(stderr, "32 Mio : %llu écritures, %llu lectures\n", (unsigned long long)w, (unsigned long long)r);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(j, 0);
  TT_ASSERT(w <= 64);
  TT_ASSERT(r <= 32);
}

/* Écritures non alignées qui chevauchent des secteurs, relues avant et après sync, sur des disques de 512 et
 * de 4096 octets par secteur. */
static void test_ntfs_cache_coherent(void) {
  for (size_t i = 0; i < NIMGS; i++) {
    vol v;
    int rc = ouvrir(IMGS[i].img, IMGS[i].ss, IMGS[i].arbre, &v);
    size_t n = 100000;
    uint8_t *att = calloc(1, n), *back = malloc(n), *p = motif(n, 23);
    fl_file *f = NULL;
    if (rc == FL_OK) rc = fl_fs_open(v.fs, "/c.bin", FL_O_RDWR | FL_O_CREAT, &f);
    static const size_t offs[][2] = {{1, 700}, {511, 3}, {4095, 2}, {4000, 9000}, {70001, 29999}, {12, 1}, {600, 5000}};
    for (size_t k = 0; rc == FL_OK && k < sizeof offs / sizeof *offs; k++) {
      size_t o = offs[k][0], l = offs[k][1];
      if (fl_file_pwrite(f, p + o, l, o) != (int64_t)l) rc = FL_EIO;
      memcpy(att + o, p + o, l);
      size_t got = 0;
      while (rc == FL_OK && got < n) {
        int64_t r = fl_file_pread(f, back + got, n - got, got);
        if (r <= 0) break;
        got += (size_t)r;
      }
      if (rc == FL_OK && memcmp(back, att, got)) rc = FL_ECORRUPT;
    }
    if (rc == FL_OK) rc = fl_fs_sync(v.fs);
    if (f && fl_file_close(f) != FL_OK && rc == FL_OK) rc = FL_EIO;
    if (rc == FL_OK) rc = relire(v.fs, "/c.bin", back, n);
    int same = rc == FL_OK && !memcmp(back, att, n);
    tm_apply_file(v.m, "/c.bin", att, n, 0, 0, 0644);
    int j = rc == FL_OK ? juger(&v) : -1;
    liberer(&v);
    free(att);
    free(back);
    free(p);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT(same);
    TT_ASSERT_EQ(j, 0);
  }
}


static int cb_rien(void *ctx, const fl_dirent *e) {
  (void)e;
  (*(int *)ctx)++;
  return 0;
}

/* Une écriture différée qui échoue au vidage (fermeture) : erreur rendue par la fermeture, lecture seule
 * collante, volume encore lisible, démontage en échec. */
static void test_ntfs_cache_erreur(void) {
  uint8_t *data = motif(MIB, 29);
  vol v;
  int rc = ouvrir("ntfs-vide.img", 512, NULL, &v);
  fl_file *f = NULL;
  if (rc == FL_OK) rc = fl_fs_open(v.fs, "/x.bin", FL_O_RDWR | FL_O_CREAT, &f);
  for (size_t off = 0; rc == FL_OK && off < MIB; off += 64 * 1024)
    if (fl_file_pwrite(f, data + off, 64 * 1024, off) != 64 * 1024) rc = FL_EIO;
  uint64_t avant = v.dev ? fl_fault_bdev_writes(v.dev) : 0;
  if (f && fl_file_close(f) != FL_OK && rc == FL_OK) rc = FL_EIO;
  uint64_t apres = v.dev ? fl_fault_bdev_writes(v.dev) : 0;
  liberer(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT(apres > avant + 1); /* le vidage différé a bien lieu à la fermeture */
  rc = ouvrir_panne("ntfs-vide.img", 512, NULL, avant, FL_EIO, &v);
  f = NULL;
  int64_t w = FL_OK;
  if (rc == FL_OK) rc = fl_fs_open(v.fs, "/x.bin", FL_O_RDWR | FL_O_CREAT, &f);
  for (size_t off = 0; rc == FL_OK && off < MIB && w >= 0; off += 64 * 1024) w = fl_file_pwrite(f, data + off, 64 * 1024, off);
  int c = f ? fl_file_close(f) : FL_EIO;
  fl_statfs st = {0};
  int s = v.fs ? fl_fs_statfs(v.fs, &st) : FL_EIO;
  int n = 0;
  int lis = v.fs ? fl_fs_readdir(v.fs, "/", cb_rien, &n) : FL_EIO;
  int um = v.fs ? fl_fs_unmount(v.fs) : FL_EIO;
  v.fs = NULL;
  liberer(&v);
  free(data);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(w, 64 * 1024); /* toutes les écritures acquittées (différées) */
  TT_ASSERT_EQ(c, FL_EIO);
  TT_ASSERT_EQ(s, FL_OK);
  TT_ASSERT(st.read_only);
  TT_ASSERT_STR_EQ(st.ro_reason, FL_RO_IOERR);
  TT_ASSERT_EQ(lis, FL_OK);
  TT_ASSERT_EQ(um, FL_EIO);
}

int main(void) {
  if (!fu_ntfs_tools_available()) {
    printf("SAUTÉ : outils NTFS absents ou plateforme Android (juges réservés au PC)\n");
    return 77;
  }
  TT_RUN(test_ntfs_creer_ecrire);
  TT_RUN(test_ntfs_ecrasement_ajout_troncature);
  TT_RUN(test_ntfs_trou_lointain);
  TT_RUN(test_ntfs_horodatage);
  TT_RUN(test_ntfs_drapeau_sale);
  TT_RUN(test_ntfs_disque_plein);
  TT_RUN(test_ntfs_compresse_lecture_seule);
  TT_RUN(test_ntfs_partage_lecteur_ecrivain);
  TT_RUN(test_ntfs_ecriture_erreur_io);
  TT_RUN(test_ntfs_mkdir_rmdir);
  TT_RUN(test_ntfs_unlink);
  TT_RUN(test_ntfs_unlink_lien_dur);
  TT_RUN(test_ntfs_rename_deplacer);
  TT_RUN(test_ntfs_rename_refus);
  TT_RUN(test_ntfs_rename_casse);
  TT_RUN(test_ntfs_noms_interdits);
  TT_RUN(test_ntfs_noms_casse);
  TT_RUN(test_ntfs_noms_unicode);
  TT_RUN(test_ntfs_metafichiers_proteges);
  TT_RUN(test_ntfs_debit);
  TT_RUN(test_ntfs_cache_coherent);
  TT_RUN(test_ntfs_cache_erreur);
  TT_MAIN_END();
}
