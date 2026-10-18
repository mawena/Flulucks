#define _GNU_SOURCE
/* Tampon d'écriture différée du glue ext4 (correctif 0.2.1, wbuf.c) : une transaction de journal par
 * 4 Mio au lieu d'une par fl_file_pwrite, données tamponnées visibles de toute autre opération, contenu
 * exact quel que soit l'ordre des écritures, erreur différée jamais perdue, bornes vérifiées tout de
 * suite.
 *
 * Chaque test part d'une copie d'une image du banc (T3) ; juges : e2fsck -fn et debugfs (indépendant de
 * lwext4). Réservé au PC : sauté (code 77) si e2fsck/debugfs/dumpe2fs sont absents, et sur Android. */
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
#include "fs/wbuf.h"
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

static uint64_t g_alea = 88172645463325252ull;
static uint64_t alea(uint64_t n) {
  g_alea ^= g_alea << 13;
  g_alea ^= g_alea >> 7;
  g_alea ^= g_alea << 17;
  return g_alea % n;
}

static int64_t horloge(void) { return 1760000000LL; }

typedef struct {
  fl_bdev *file, *dev; /* dev : un fault_bdev au-dessus de file (compteurs) */
  fl_fs *fs;
} vol;

/* Toujours avec un fault_bdev : après `writes_ok` écritures réussies, toute écriture rend FL_EIO
 * (UINT64_MAX : jamais). */
static int vol_monter(vol *v, const char *img, uint64_t writes_ok) {
  memset(v, 0, sizeof *v);
  int rc = fl_file_bdev_open(img, 512, false, &v->file);
  if (rc != FL_OK) return rc;
  rc = fl_fault_bdev_new(v->file, writes_ok, FL_EIO, &v->dev);
  if (rc == FL_OK) rc = fl_fs_mount(v->dev, FL_CONTENT_EXT4, true, &v->fs);
  if (rc != FL_OK) {
    if (v->dev) fl_bdev_close(v->dev);
    fl_bdev_close(v->file);
    memset(v, 0, sizeof *v);
  }
  return rc;
}

/* Démonte (le code du démontage est rendu) et ferme les périphériques. */
static int vol_demonter(vol *v) {
  int rc = fl_fs_unmount(v->fs);
  fl_bdev_close(v->dev);
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
  if (s.read_only) fprintf(stderr, "lecture seule : %s\n", s.ro_reason);
  return !s.read_only;
}

static int e2fsck_propre(const char *img) {
  static char out[1 << 16];
  int rc = fu_e2fsck_n(img, out, sizeof out);
  if (rc != 0) fprintf(stderr, "e2fsck -fn = %d\n%s\n", rc, out);
  return rc;
}

/* Rejeu du journal seul (et compteurs), puis -fn propre. */
static int recuperable(const char *img) {
  static char fy[1 << 16], fn[1 << 16];
  int rejoue = 0, fy_rc = 0;
  int rc = fu_e2fsck_recover(img, &rejoue, &fy_rc, fy, sizeof fy, fn, sizeof fn);
  if (rc != 0) fprintf(stderr, "récupération : %d\n-- -fy --\n%s\n-- -fn --\n%s\n", rc, fy, fn);
  return rc;
}

/* Contenu du fichier lu par debugfs égal à attendu[0, n). */
static bool debugfs_egal(const char *img, const char *chemin, const uint8_t *attendu, size_t n) {
  uint8_t *lu = malloc(n + 1);
  if (!lu) return false;
  int64_t got = fu_debugfs_cat(img, chemin, lu, n + 1);
  bool ok = got == (int64_t)n && memcmp(lu, attendu, n) == 0;
  if (!ok) fprintf(stderr, "debugfs %s : %lld octets lus, %zu attendus\n", chemin, (long long)got, n);
  free(lu);
  return ok;
}

/* Relit [0, n) par f et compare. */
static bool relire(fl_file *f, const uint8_t *attendu, size_t n) {
  uint8_t *lu = malloc(n + 16);
  if (!lu) return false;
  int64_t got = fl_file_pread(f, lu, n + 16, 0);
  bool ok = got == (int64_t)n && memcmp(lu, attendu, n) == 0;
  if (!ok) fprintf(stderr, "relecture : %lld octets, %zu attendus\n", (long long)got, n);
  free(lu);
  return ok;
}

static uint64_t taille(fl_fs *fs, const char *chemin) {
  fl_stat st;
  if (fl_fs_stat(fs, chemin, &st) != FL_OK) return UINT64_MAX;
  return st.size;
}

/* Écritures réussies d'une session qui monte `fixture` puis crée `chemin` (sessions déterministes :
 * même copie, même horloge). */
static uint64_t ecritures_apres_creation(const char *fixture, const char *chemin) {
  char img[4096];
  vol v;
  if (copie_de(fixture, img, sizeof img) != 0 || vol_monter(&v, img, UINT64_MAX) != FL_OK) return 0;
  fl_file *f = NULL;
  uint64_t w = 0;
  if (fl_fs_open(v.fs, chemin, FL_O_RDWR | FL_O_CREAT, &f) == FL_OK) w = fl_fault_bdev_writes(v.dev);
  fl_file_close(f);
  vol_demonter(&v);
  unlink(img);
  return w;
}

/* ---------- tests ---------- */

/* 16 Mio en pwrite de 4 Kio (le DocumentsProvider transmet de petits morceaux) : avant le correctif,
 * 922 vidages de cache et 2714 écritures par Mio (une transaction par pwrite) ; désormais une
 * transaction par 4 Mio. */
static void test_tampon_debit(void) {
  const size_t total = 16 * MIB, tr = 4096;
  char img[4096];
  vol v;
  uint8_t *data = malloc(total);
  TT_ASSERT(data != NULL);
  remplir(data, total, 11);
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, UINT64_MAX), FL_OK);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/debit.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  uint64_t w0 = fl_fault_bdev_writes(v.dev), f0 = fl_fault_bdev_flushes(v.dev);
  for (size_t o = 0; o < total; o += tr) TT_ASSERT_EQ(fl_file_pwrite(f, data + o, tr, o), (int64_t)tr);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  uint64_t dw = fl_fault_bdev_writes(v.dev) - w0, df = fl_fault_bdev_flushes(v.dev) - f0;
  fprintf(stderr, "16 Mio en pwrite de 4 Kio : %.1f écritures/Mio, %.1f vidages/Mio\n", dw / 16.0, df / 16.0);
  TT_ASSERT(df <= 2 * 16);
  TT_ASSERT(dw <= 300 * 16);
  TT_ASSERT_EQ(taille(v.fs, "/debit.bin"), total);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(e2fsck_propre(img), 0);
  TT_ASSERT(debugfs_egal(img, "/debit.bin", data, total));
  free(data);
  unlink(img);
}

typedef struct {
  const char *nom;
  bool vu;
  uint64_t taille;
} cherche;

static int cherche_cb(void *ctx, const fl_dirent *e) {
  cherche *c = ctx;
  if (!strcmp(e->name, c->nom)) {
    c->vu = true;
    c->taille = e->st.size;
  }
  return 0;
}

/* Données tamponnées par le descripteur A : lues par un descripteur B du même fichier, par A, vues par
 * stat et readdir, sans fermeture ; idem après un renommage du fichier pendant que des données sont
 * tamponnées. */
static void test_tampon_coherence(void) {
  enum { MORCEAU = 1000, N = 200 };
  static uint8_t data[MORCEAU * N];
  remplir(data, sizeof data, 12);
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, UINT64_MAX), FL_OK);
  fl_file *a = NULL, *b = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/coh.bin", FL_O_RDWR | FL_O_CREAT, &a), FL_OK);
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/coh.bin", FL_O_RDONLY, &b), FL_OK);
  uint64_t w0 = fl_fault_bdev_writes(v.dev);
  for (int i = 0; i < 64; i++)
    TT_ASSERT_EQ(fl_file_pwrite(a, data + i * MORCEAU, MORCEAU, (uint64_t)i * MORCEAU), MORCEAU);
  TT_ASSERT_EQ(fl_fault_bdev_writes(v.dev), w0); /* tamponné : rien d'écrit encore */
  TT_ASSERT_EQ(taille(v.fs, "/coh.bin"), 64 * MORCEAU);
  TT_ASSERT(relire(b, data, 64 * MORCEAU));

  for (int i = 64; i < 128; i++)
    TT_ASSERT_EQ(fl_file_pwrite(a, data + i * MORCEAU, MORCEAU, (uint64_t)i * MORCEAU), MORCEAU);
  TT_ASSERT(relire(b, data, 128 * MORCEAU));
  for (int i = 128; i < 160; i++)
    TT_ASSERT_EQ(fl_file_pwrite(a, data + i * MORCEAU, MORCEAU, (uint64_t)i * MORCEAU), MORCEAU);
  uint64_t w1 = fl_fault_bdev_writes(v.dev);
  TT_ASSERT_EQ(fl_fs_rename(v.fs, "/coh.bin", "/coh2.bin"), FL_OK);
  TT_ASSERT(fl_fault_bdev_writes(v.dev) > w1);
  TT_ASSERT_EQ(taille(v.fs, "/coh2.bin"), 160 * MORCEAU);
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/coh.bin", &st), FL_ENOENT);
  TT_ASSERT(relire(b, data, 160 * MORCEAU));

  /* après le renommage, encore tamponné : relu par A lui-même, puis vu par readdir */
  for (int i = 160; i < 180; i++)
    TT_ASSERT_EQ(fl_file_pwrite(a, data + i * MORCEAU, MORCEAU, (uint64_t)i * MORCEAU), MORCEAU);
  TT_ASSERT(relire(a, data, 180 * MORCEAU));
  for (int i = 180; i < N; i++)
    TT_ASSERT_EQ(fl_file_pwrite(a, data + i * MORCEAU, MORCEAU, (uint64_t)i * MORCEAU), MORCEAU);
  cherche c = {"coh2.bin", false, 0};
  TT_ASSERT_EQ(fl_fs_readdir(v.fs, "/", cherche_cb, &c), FL_OK);
  TT_ASSERT(c.vu);
  TT_ASSERT_EQ(c.taille, sizeof data);
  TT_ASSERT_EQ(fl_file_close(b), FL_OK);
  TT_ASSERT_EQ(fl_file_close(a), FL_OK);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(e2fsck_propre(img), 0);
  TT_ASSERT(debugfs_egal(img, "/coh2.bin", data, sizeof data));
  unlink(img);
}

/* Écritures dans le désordre, chevauchantes, avec trous, à cheval sur la capacité du tampon, d'au moins
 * 4 Mio (directes), puis aléatoires (moitié en prolongement) : contenu final exact, relu en cours de
 * route et par debugfs. */
static void test_tampon_non_contigu(void) {
  const size_t cap = 16 * MIB;
  uint8_t *modele = calloc(1, cap), *tmp = malloc(cap);
  TT_ASSERT(modele && tmp);
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, UINT64_MAX), FL_OK);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/desordre.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  static const struct {
    uint64_t off;
    size_t len;
  } fixes[] = {
      {0, 1000},          {1000, 3000},       {500, 100},         /* recul dans le tampon */
      {8000, 50},         {4000, 4000},       {8050, 1},          /* trou, puis comblé */
      {5 * MIB, 4 * MIB}, {5 * MIB + 10, 20},                     /* directe, puis par-dessus */
      {3 * MIB, 2 * MIB + 1},                                     /* chevauche la directe */
      {11 * MIB, 4096},   {10 * MIB, 4096},   {10 * MIB + 4096, 4 * MIB - 4096 - 1}, /* à rebours */
      {10 * MIB + 4 * MIB - 4096 - 1 - 2 * MIB, 3}, /* dans la zone déjà écrite */
  };
  uint64_t fin = 0;
  uint64_t graine = 100;
  for (size_t i = 0; i < sizeof fixes / sizeof *fixes; i++) {
    TT_ASSERT(fixes[i].off + fixes[i].len <= cap);
    remplir(tmp, fixes[i].len, graine++);
    TT_ASSERT_EQ(fl_file_pwrite(f, tmp, fixes[i].len, fixes[i].off), (int64_t)fixes[i].len);
    memcpy(modele + fixes[i].off, tmp, fixes[i].len);
    if (fixes[i].off + fixes[i].len > fin) fin = fixes[i].off + fixes[i].len;
  }
  TT_ASSERT(relire(f, modele, fin));
  /* 4 Kio contigus de 0 à 6 Mio : le tampon se remplit, est vidé à 4 Mio, puis repart */
  for (uint64_t o = 0; o < 6 * MIB; o += 4096) {
    remplir(tmp, 4096, graine++);
    TT_ASSERT_EQ(fl_file_pwrite(f, tmp, 4096, o), 4096);
    memcpy(modele + o, tmp, 4096);
  }
  TT_ASSERT(relire(f, modele, fin));
  uint64_t prec = 0;
  for (int i = 0; i < 400; i++) {
    size_t len = 1 + (size_t)alea(200000);
    uint64_t off = alea(2) && prec + len <= cap ? prec : alea(cap - len);
    remplir(tmp, len, graine++);
    TT_ASSERT_EQ(fl_file_pwrite(f, tmp, len, off), (int64_t)len);
    memcpy(modele + off, tmp, len);
    prec = off + len;
    if (prec > fin) fin = prec;
    if (i % 50 == 49) TT_ASSERT(relire(f, modele, fin));
  }
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_EQ(taille(v.fs, "/desordre.bin"), fin);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(e2fsck_propre(img), 0);
  TT_ASSERT(debugfs_egal(img, "/desordre.bin", modele, fin));
  free(modele);
  free(tmp);
  unlink(img);
}

/* Écritures alternées dans deux fichiers (un seul tampon par volume : chacune vide celui de l'autre) :
 * contenu exact des deux. */
static void test_tampon_deux_fichiers(void) {
  const size_t total = 2 * MIB, tr = 4096;
  uint8_t *da = malloc(total), *db = malloc(total);
  TT_ASSERT(da && db);
  remplir(da, total, 31);
  remplir(db, total, 32);
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, UINT64_MAX), FL_OK);
  fl_file *a = NULL, *b = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/a.bin", FL_O_RDWR | FL_O_CREAT, &a), FL_OK);
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/b.bin", FL_O_RDWR | FL_O_CREAT, &b), FL_OK);
  for (size_t o = 0; o < total; o += tr) {
    TT_ASSERT_EQ(fl_file_pwrite(a, da + o, tr, o), (int64_t)tr);
    TT_ASSERT_EQ(fl_file_pwrite(b, db + o, tr, o), (int64_t)tr);
    /* puis des rafales : 8 morceaux de suite dans chacun */
    if (o % (64 * tr) == 0 && o + 9 * tr <= total) {
      for (int k = 1; k <= 8; k++) TT_ASSERT_EQ(fl_file_pwrite(a, da + o + k * tr, tr, o + k * tr), (int64_t)tr);
      for (int k = 1; k <= 8; k++) TT_ASSERT_EQ(fl_file_pwrite(b, db + o + k * tr, tr, o + k * tr), (int64_t)tr);
    }
  }
  TT_ASSERT(relire(a, da, total));
  TT_ASSERT(relire(b, db, total));
  TT_ASSERT_EQ(fl_file_close(a), FL_OK);
  TT_ASSERT_EQ(fl_file_close(b), FL_OK);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(e2fsck_propre(img), 0);
  TT_ASSERT(debugfs_egal(img, "/a.bin", da, total));
  TT_ASSERT(debugfs_egal(img, "/b.bin", db, total));
  free(da);
  free(db);
  unlink(img);
}

/* Panne d'écriture au vidage de données déjà acquittées : les petits pwrite ont rendu OK, la fermeture
 * (a), la synchronisation (b) ou la fermeture après une lecture d'un autre fichier qui a déclenché le
 * vidage (c) rendent FL_EIO, le volume est en lecture seule (RO_IOERR), plus rien n'est écrit, et le
 * disque reste récupérable par le seul rejeu du journal. Jamais FL_OK partout. */
static void test_tampon_erreur_differee(void) {
  uint64_t w_creation = ecritures_apres_creation("ext4-rw-reel.img", "/panne.bin");
  TT_ASSERT(w_creation > 0);
  uint8_t b[4096];
  remplir(b, sizeof b, 41);
  for (int variante = 0; variante < 3; variante++) {
    char img[4096];
    vol v;
    TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
    TT_ASSERT_EQ(vol_monter(&v, img, w_creation + 3), FL_OK);
    fl_file *f = NULL, *autre = NULL;
    TT_ASSERT_EQ(fl_fs_open(v.fs, "/panne.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
    TT_ASSERT_EQ(fl_fault_bdev_writes(v.dev), w_creation);
    TT_ASSERT(en_ecriture(v.fs));
    if (variante == 2) TT_ASSERT_EQ(fl_fs_open(v.fs, "/hello.txt", FL_O_RDONLY, &autre), FL_OK);
    for (int i = 0; i < 64; i++) TT_ASSERT_EQ(fl_file_pwrite(f, b, sizeof b, (uint64_t)i * sizeof b), (int64_t)sizeof b);
    TT_ASSERT_EQ(fl_fault_bdev_writes(v.dev), w_creation); /* tout est dans le tampon (statfs le viderait) */
    int rc;
    if (variante == 0) {
      rc = fl_file_close(f);
      f = NULL;
    } else if (variante == 1) {
      rc = fl_fs_sync(v.fs);
    } else {
      /* le vidage du tampon de /panne.bin échoue ; la lecture de l'autre fichier continue */
      char lu[32];
      TT_ASSERT_EQ(fl_file_pread(autre, lu, sizeof lu, 0), 16);
      TT_ASSERT_MEM_EQ(lu, "Bonjour FluLuks\n", 16);
      TT_ASSERT_EQ(fl_file_close(autre), FL_OK);
      rc = fl_file_close(f);
      f = NULL;
    }
    fprintf(stderr, "variante %d : %d\n", variante, rc);
    TT_ASSERT_EQ(rc, FL_EIO);
    TT_ASSERT(lecture_seule(v.fs, FL_RO_IOERR));
    if (f) {
      TT_ASSERT(fl_file_pwrite(f, b, sizeof b, 0) < 0);
      TT_ASSERT_EQ(fl_file_close(f), FL_EIO); /* erreur différée, collante */
    }
    TT_ASSERT_EQ(fl_fs_sync(v.fs), FL_EIO);
    TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/apres"), FL_EROFS);
    TT_ASSERT_EQ(vol_demonter(&v), FL_EIO);
    TT_ASSERT_EQ(recuperable(img), 0);
    unlink(img);
  }
}

/* Comme test_tampon_erreur_differee, en relevant les compteurs : rien n'est écrit après la panne, ni
 * vidage de cache, ni démontage. */
static void test_tampon_rien_apres_panne(void) {
  uint64_t w_creation = ecritures_apres_creation("ext4-rw-reel.img", "/panne.bin");
  TT_ASSERT(w_creation > 0);
  uint8_t b[4096];
  remplir(b, sizeof b, 42);
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, w_creation + 3), FL_OK);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/panne.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  for (int i = 0; i < 64; i++) TT_ASSERT_EQ(fl_file_pwrite(f, b, sizeof b, (uint64_t)i * sizeof b), (int64_t)sizeof b);
  TT_ASSERT_EQ(fl_fs_sync(v.fs), FL_EIO);
  uint64_t w = fl_fault_bdev_writes(v.dev), fl = fl_fault_bdev_flushes(v.dev);
  TT_ASSERT_EQ(w, w_creation + 3);
  TT_ASSERT(fl_file_pwrite(f, b, sizeof b, 0) < 0);
  TT_ASSERT_EQ(fl_file_close(f), FL_EIO);
  TT_ASSERT_EQ(fl_fs_sync(v.fs), FL_EIO);
  fl_bdev *dev = v.dev, *file = v.file;
  TT_ASSERT_EQ(fl_fs_unmount(v.fs), FL_EIO);
  TT_ASSERT_EQ(fl_fault_bdev_writes(dev), w);
  TT_ASSERT_EQ(fl_fault_bdev_flushes(dev), fl);
  fl_bdev_close(dev);
  fl_bdev_close(file);
  TT_ASSERT_EQ(recuperable(img), 0);
  uint64_t t = UINT64_MAX; /* la création est validée, pas le contenu perdu */
  TT_ASSERT_EQ(fu_debugfs_inode_field(img, "/panne.bin", "size", &t), 0);
  TT_ASSERT_EQ(t, 0);
  unlink(img);
}

/* Bornes vérifiées tout de suite, même avec des données tamponnées (test_politique_dernier_bloc) ; sur
 * un volume gelé par le vidage du tampon d'un autre fichier, l'écriture rend l'erreur tout de suite. */
static void test_tampon_bornes(void) {
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, UINT64_MAX), FL_OK);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/loin.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pwrite(f, "abc", 3, 0), 3); /* tamponné */
  uint64_t fin = (uint64_t)4096 << 32;
  TT_ASSERT_EQ(fl_file_pwrite(f, "x", 1, fin - 4096), FL_EINVAL);
  TT_ASSERT_EQ(fl_file_pwrite(f, "xy", 2, fin - 4096 - 1), FL_EINVAL);
  TT_ASSERT_EQ(fl_file_truncate(f, fin), FL_EINVAL);
  TT_ASSERT_EQ(fl_file_pwrite(f, "x", 1, fin - 4096 - 1), 1); /* avant-dernier bloc : permis */
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT(en_ecriture(v.fs));
  TT_ASSERT_EQ(taille(v.fs, "/loin.bin"), fin - 4096);
  TT_ASSERT_EQ(fl_fs_unlink(v.fs, "/loin.bin"), FL_OK);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(e2fsck_propre(img), 0);
  unlink(img);

  /* gel : le pwrite sur /hello.txt vide d'abord le tampon de /gele.bin, qui échoue */
  uint64_t w_creation = ecritures_apres_creation("ext4-rw-reel.img", "/gele.bin");
  TT_ASSERT(w_creation > 0);
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, w_creation + 3), FL_OK);
  fl_file *a = NULL, *b = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/gele.bin", FL_O_RDWR | FL_O_CREAT, &a), FL_OK);
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/hello.txt", FL_O_RDWR, &b), FL_OK);
  uint8_t d[8192];
  remplir(d, sizeof d, 51);
  TT_ASSERT_EQ(fl_file_pwrite(a, d, sizeof d, 0), (int64_t)sizeof d);
  int64_t r = fl_file_pwrite(b, "Salut", 5, 0);
  fprintf(stderr, "écriture sur volume gelé : %lld\n", (long long)r);
  TT_ASSERT_EQ(r, FL_EIO);
  TT_ASSERT(lecture_seule(v.fs, FL_RO_IOERR));
  TT_ASSERT_EQ(fl_file_pwrite(b, "Salut", 5, 0), FL_EROFS);
  TT_ASSERT_EQ(fl_file_pwrite(a, d, sizeof d, sizeof d), FL_EROFS);
  TT_ASSERT_EQ(fl_file_close(b), FL_EIO); /* écrit (tenté) : la synchro de la fermeture échoue */
  TT_ASSERT_EQ(fl_file_close(a), FL_EIO); /* erreur différée */
  TT_ASSERT_EQ(vol_demonter(&v), FL_EIO);
  TT_ASSERT_EQ(recuperable(img), 0);
  char lu[32];
  TT_ASSERT_EQ(fu_debugfs_cat(img, "/hello.txt", lu, sizeof lu), 16);
  TT_ASSERT_MEM_EQ(lu, "Bonjour FluLuks\n", 16);
  unlink(img);
}

/* Disque plein en pwrite de 4 Kio : près du plein, l'écriture est directe, FL_ENOSPC est rendu par le
 * pwrite qui ne tient pas (et non par un vidage différé) ; tout ce qui a été acquitté est sur le disque,
 * la fermeture réussit, le volume reste en écriture, e2fsck est propre. */
static void test_tampon_disque_plein(void) {
  const size_t tr = 4096;
  uint8_t b[4096];
  remplir(b, sizeof b, 61);
  char img[4096];
  vol v;
  TT_ASSERT(copie_de("ext4-rw-1k.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, UINT64_MAX), FL_OK);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/plein.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  uint64_t ecrit = 0;
  int64_t w = 0;
  for (int i = 0; i < 100000; i++) {
    w = fl_file_pwrite(f, b, tr, ecrit);
    if (w != (int64_t)tr) break;
    ecrit += tr;
  }
  fprintf(stderr, "disque plein après %llu octets (%lld)\n", (unsigned long long)ecrit, (long long)w);
  TT_ASSERT_EQ(w, FL_ENOSPC);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  uint64_t t = taille(v.fs, "/plein.bin");
  TT_ASSERT(t >= ecrit && t < ecrit + tr);
  TT_ASSERT(en_ecriture(v.fs));
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(e2fsck_propre(img), 0);
  uint8_t *lu = malloc((size_t)t + 1);
  TT_ASSERT(lu != NULL);
  TT_ASSERT_EQ(fu_debugfs_cat(img, "/plein.bin", lu, (size_t)t + 1), (int64_t)t);
  bool ok = true;
  for (uint64_t o = 0; ok && o < t; o += tr) ok = !memcmp(lu + o, b, t - o < tr ? (size_t)(t - o) : tr);
  free(lu);
  TT_ASSERT(ok);
  unlink(img);
}

/* Module seul (sans système de fichiers) : regroupement, vidages, écriture directe, erreur collante. */
typedef struct {
  uint8_t *disque;
  int appels;
  int echouer_apres; /* appels réussis avant l'échec, -1 : jamais */
} faux;

static int faux_ecrire(void *ctx, const void *p, size_t len, uint64_t off) {
  faux *x = ctx;
  if (x->echouer_apres >= 0 && x->appels >= x->echouer_apres) return FL_EIO;
  x->appels++;
  memcpy(x->disque + off, p, len);
  return FL_OK;
}

static void test_tampon_module(void) {
  const size_t cap = 3 * FL_WBUF_CAP;
  faux x = {calloc(1, cap), 0, -1};
  uint8_t *src = malloc(cap);
  TT_ASSERT(x.disque && src);
  remplir(src, cap, 71);
  fl_wbuf b = {0};
  TT_ASSERT_EQ(fl_wbuf_end(&b), 0);
  for (size_t o = 0; o < FL_WBUF_CAP; o += 4096) TT_ASSERT_EQ(fl_wbuf_write(&b, src + o, 4096, o, faux_ecrire, &x), FL_OK);
  TT_ASSERT_EQ(x.appels, 0);
  TT_ASSERT_EQ(fl_wbuf_end(&b), FL_WBUF_CAP);
  TT_ASSERT_EQ(fl_wbuf_write(&b, src + FL_WBUF_CAP, 1, FL_WBUF_CAP, faux_ecrire, &x), FL_OK); /* plein */
  TT_ASSERT_EQ(x.appels, 1);
  TT_ASSERT_EQ(fl_wbuf_end(&b), FL_WBUF_CAP + 1);
  TT_ASSERT_EQ(fl_wbuf_write(&b, src, 10, 0, faux_ecrire, &x), FL_OK); /* non contigu */
  TT_ASSERT_EQ(x.appels, 2);
  TT_ASSERT_EQ(fl_wbuf_write(&b, src + FL_WBUF_CAP, FL_WBUF_CAP, FL_WBUF_CAP, faux_ecrire, &x), FL_OK);
  TT_ASSERT_EQ(x.appels, 4); /* vidage puis écriture directe */
  TT_ASSERT_EQ(fl_wbuf_end(&b), 0);
  TT_ASSERT_EQ(fl_wbuf_flush(&b, faux_ecrire, &x), FL_OK);
  TT_ASSERT_EQ(x.appels, 4);
  TT_ASSERT_MEM_EQ(x.disque, src, 2 * FL_WBUF_CAP);
  /* échec d'un vidage : erreur collante, plus rien n'est écrit */
  x.echouer_apres = x.appels;
  TT_ASSERT_EQ(fl_wbuf_write(&b, src, 100, 0, faux_ecrire, &x), FL_OK);
  TT_ASSERT_EQ(fl_wbuf_write(&b, src, 100, 1000, faux_ecrire, &x), FL_EIO);
  TT_ASSERT_EQ(b.err, FL_EIO);
  TT_ASSERT_EQ(fl_wbuf_write(&b, src, 100, 2000, faux_ecrire, &x), FL_EIO);
  x.echouer_apres = -1;
  TT_ASSERT_EQ(fl_wbuf_write(&b, src, 100, 3000, faux_ecrire, &x), FL_EIO);
  TT_ASSERT_EQ(fl_wbuf_flush(&b, faux_ecrire, &x), FL_OK);
  TT_ASSERT_EQ(x.appels, 4);
  /* échec d'une écriture directe : rendu, pas collant */
  fl_wbuf c = {0};
  x.echouer_apres = x.appels;
  TT_ASSERT_EQ(fl_wbuf_write(&c, src, FL_WBUF_CAP, 0, faux_ecrire, &x), FL_EIO);
  TT_ASSERT_EQ(c.err, FL_OK);
  x.echouer_apres = -1;
  TT_ASSERT_EQ(fl_wbuf_write(&c, src, 5, 0, faux_ecrire, &x), FL_OK);
  fl_wbuf_free(&c);
  TT_ASSERT_EQ(x.appels, 4); /* abandonné sans écriture */
  fl_wbuf_free(&b);
  free(x.disque);
  free(src);
}

int main(void) {
  TT_RUN(test_tampon_module);
  if (!fu_tools_available()) {
    printf("SAUTÉ : e2fsck/debugfs/dumpe2fs absents ou plateforme Android (banc réservé au PC)\n");
    return tt_failures ? 1 : 77; /* SKIP_RETURN_CODE de CTest */
  }
  fl_fs_set_clock(horloge);
  TT_RUN(test_tampon_debit);
  TT_RUN(test_tampon_coherence);
  TT_RUN(test_tampon_non_contigu);
  TT_RUN(test_tampon_deux_fichiers);
  TT_RUN(test_tampon_erreur_differee);
  TT_RUN(test_tampon_rien_apres_panne);
  TT_RUN(test_tampon_bornes);
  TT_RUN(test_tampon_disque_plein);
  fl_fs_set_clock(NULL);
  nettoyer();
  TT_MAIN_END();
}
