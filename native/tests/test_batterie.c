#define _GNU_SOURCE
/* Batterie de non-corruption ext4 (phase 3, T8). Des opérations pseudo-aléatoires (graine affichée,
 * rejouable par FL_SEED=<n>) sont appliquées à la fois au volume et au modèle (tree_model) : création,
 * ajout à la fin, réécriture au milieu, troncature vers le haut et vers le bas, renommage et
 * déplacement (fichiers et dossiers), suppression, mkdir/rmdir, noms Unicode jusqu'à 255 octets,
 * profondeur jusqu'à 40. Tous les 200 pas et à la fin : démontage, « e2fsck -fn » propre, debugfs voit
 * le modèle, le moteur relit chaque fichier de la batterie, puis remontage.
 *
 * Chaque batterie laisse son image et son manifeste dans build/native/batterie/ (<nom>.ecrite.img,
 * <nom>.manifest, <nom>.pass pour LUKS), pour tools/fixtures/verify_rw_with_kernel.sh (sudo, lancé
 * par l'utilisateur).
 *
 * « test_batterie lent » (test CTest test_batterie_lent, label lent) : la variante longue et le
 * fichier dense de 4,1 Gio. Réservé au PC : sauté (code 77) sans e2fsck/debugfs, et sur Android. */
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fluluks/luks.h"
#include "fsck_util.h"
#include "mbedtls/sha256.h"
#include "tree_model.h"
#include "tt.h"

#define MIB ((size_t)1 << 20)
#define GIB ((uint64_t)1 << 30)
#define FICHIER_MAX (256 * 1024)
#define PROFONDEUR_MAX 40

static int64_t horloge(void) { return 1760000000LL; }

static tm *g_base;

/* ---------- aléa ---------- */

static uint64_t g_x;
static uint64_t alea(void) {
  g_x ^= g_x << 13;
  g_x ^= g_x >> 7;
  g_x ^= g_x << 17;
  return g_x;
}
static uint32_t entre(uint32_t n) { return n ? (uint32_t)(alea() % n) : 0; }
static void remplir(uint8_t *b, size_t n) {
  for (size_t i = 0; i < n; i += 8) {
    uint64_t v = alea();
    memcpy(b + i, &v, n - i < 8 ? n - i : 8);
  }
}

static uint64_t graine(uint64_t defaut) {
  const char *s = getenv("FL_SEED");
  return s && *s ? strtoull(s, NULL, 0) : defaut;
}

/* ---------- volume : clair, secteurs de 4 Kio, ou LUKS ---------- */

typedef struct {
  const char *fixture;  /* image du banc copiée */
  uint32_t secteur;     /* 512 ou 4096 */
  bool luks;            /* fixture LUKS + ext4, phrase de passe pass.txt */
} genre;

typedef struct {
  const genre *g;
  char img[4096];
  fl_bdev *file, *dev;
  fl_luks *l;
  fl_fs *fs;
} vol;

static int phrase(char *buf, size_t cap, size_t *n) {
  char p[4096];
  snprintf(p, sizeof p, "%s", tt_fixture("pass.txt"));
  FILE *f = fopen(p, "rb");
  if (!f) return -1;
  *n = fread(buf, 1, cap, f);
  fclose(f);
  return 0;
}

static int vol_monter(vol *v) {
  int rc = fl_file_bdev_open(v->img, v->g->secteur, false, &v->file);
  if (rc != FL_OK) return rc;
  v->dev = v->file;
  if (v->g->luks) {
    char pass[256];
    size_t n = 0;
    rc = phrase(pass, sizeof pass, &n) == 0 ? FL_OK : FL_EIO;
    if (rc == FL_OK) rc = fl_luks_open(v->file, &v->l);
    if (rc == FL_OK) rc = fl_luks_unlock(v->l, pass, n, &v->dev);
    if (rc != FL_OK) v->dev = v->file;
  }
  if (rc == FL_OK) rc = fl_fs_mount(v->dev, FL_CONTENT_EXT4, true, &v->fs);
  if (rc == FL_OK) {
    fl_statfs s;
    if (fl_fs_statfs(v->fs, &s) != FL_OK || s.read_only) {
      fprintf(stderr, "%s : lecture seule (%s)\n", v->g->fixture, s.ro_reason);
      fl_fs_unmount(v->fs);
      rc = FL_EROFS;
    }
  }
  if (rc != FL_OK) {
    v->fs = NULL;
    if (v->dev != v->file) fl_bdev_close(v->dev);
    fl_luks_close(v->l);
    fl_bdev_close(v->file);
    v->file = v->dev = NULL;
    v->l = NULL;
  }
  return rc;
}

static int vol_demonter(vol *v) {
  int rc = fl_fs_unmount(v->fs);
  if (v->dev != v->file) fl_bdev_close(v->dev);
  fl_luks_close(v->l);
  fl_bdev_close(v->file);
  v->fs = NULL;
  v->file = v->dev = NULL;
  v->l = NULL;
  return rc;
}

/* Le juge, sur l'image démontée : e2fsck -fn propre et debugfs voit le modèle (LUKS : sur l'ext4
 * extrait). */
static int juger(const vol *v, const tm *m) {
  static char out[1 << 16];
  char ext[4096];
  const char *img = v->img;
  if (v->g->luks) {
    char pass[4096];
    snprintf(pass, sizeof pass, "%s", tt_fixture("pass.txt"));
    if (fu_extract_luks_ext4(v->img, pass, ext, sizeof ext) != 0) return -1;
    img = ext;
  }
  int rc = fu_e2fsck_n(img, out, sizeof out);
  if (rc != 0) fprintf(stderr, "e2fsck -fn = %d\n%s\n", rc, out);
  int e = tm_check_debugfs(m, img);
  if (e != 0) fprintf(stderr, "tm_check_debugfs : %d écarts\n", e);
  if (v->g->luks) unlink(ext);
  return rc == 0 && e == 0 ? 0 : -1;
}

/* ---------- état de la batterie ---------- */

typedef struct {
  char *chemin;
  uint8_t *data;
  size_t len;
} fich;

typedef struct {
  char *chemin;
  int prof;
} doss;

typedef struct {
  fich *f;
  size_t nf, capf;
  doss *d;
  size_t nd, capd;
  uint32_t uid, gid; /* de la racine : hérités par tout ce que la batterie crée */
  tm *m;
} etat;

static void etat_libre(etat *e) {
  for (size_t i = 0; i < e->nf; i++) {
    free(e->f[i].chemin);
    free(e->f[i].data);
  }
  for (size_t i = 0; i < e->nd; i++) free(e->d[i].chemin);
  free(e->f);
  free(e->d);
  tm_free(e->m);
  memset(e, 0, sizeof *e);
}

static bool existe(const etat *e, const char *chemin) {
  for (size_t i = 0; i < e->nf; i++)
    if (!strcmp(e->f[i].chemin, chemin)) return true;
  for (size_t i = 0; i < e->nd; i++)
    if (!strcmp(e->d[i].chemin, chemin)) return true;
  return false;
}

static bool sous(const char *chemin, const char *dossier) {
  size_t n = strlen(dossier);
  return !strncmp(chemin, dossier, n) && chemin[n] == '/';
}

static void ajouter_fich(etat *e, const char *chemin, uint8_t *data, size_t len) {
  if (e->nf == e->capf) {
    e->capf = e->capf ? 2 * e->capf : 64;
    e->f = realloc(e->f, e->capf * sizeof *e->f);
  }
  e->f[e->nf++] = (fich){strdup(chemin), data, len};
}

static void ajouter_doss(etat *e, const char *chemin, int prof) {
  if (e->nd == e->capd) {
    e->capd = e->capd ? 2 * e->capd : 64;
    e->d = realloc(e->d, e->capd * sizeof *e->d);
  }
  e->d[e->nd++] = (doss){strdup(chemin), prof};
}

static const char *const k_morceaux[] = {
    "a",         "fichier",          "Dossier",       "caf\xc3\xa9",   "\xc3\xa0\xc3\xa7\xc3\xa8\xc3\xaa",
    "\xf0\x9f\x98\x80", "\xe6\x97\xa5\xe6\x9c\xac", "\xe2\x98\x80", " espace ", "x.y",
    "-_~",       "Z",                "\xd0\x96\xd0\xb8\xd0\xb7\xd0\xbd\xd1\x8c",
};

/* Nom aléatoire, libre dans `parent`, chemin complet ≤ 4000 octets. */
static void nom_libre(const etat *e, const char *parent, char *out, size_t cap) {
  for (;;) {
    char nom[300] = "";
    size_t cible = entre(10) == 0 ? 200 + entre(56) : 1 + entre(24);
    size_t lp = strlen(parent);
    if (lp > 3500) cible = 8;
    while (strlen(nom) < cible) {
      const char *m = k_morceaux[entre(sizeof k_morceaux / sizeof *k_morceaux)];
      if (strlen(nom) + strlen(m) > 255) break;
      strcat(nom, m);
    }
    if (!nom[0]) strcpy(nom, "n");
    char suffixe[16];
    snprintf(suffixe, sizeof suffixe, "%u", entre(100000));
    if (strlen(nom) + strlen(suffixe) <= 255) strcat(nom, suffixe);
    snprintf(out, cap, "%s/%s", parent, nom);
    if (!existe(e, out)) return;
  }
}

/* ---------- opérations ---------- */

static int ecrire_tout(fl_fs *fs, const char *chemin, int flags, const uint8_t *data, size_t len, uint64_t off) {
  fl_file *f = NULL;
  int rc = fl_fs_open(fs, chemin, flags, &f);
  if (rc != FL_OK) return rc;
  int64_t w = len ? fl_file_pwrite(f, data, len, off) : 0;
  rc = fl_file_close(f);
  if (w != (int64_t)len) return w < 0 ? (int)w : FL_EIO;
  return rc;
}

static int verifier_fich(fl_fs *fs, const fich *x) {
  fl_file *f = NULL;
  int rc = fl_fs_open(fs, x->chemin, FL_O_RDONLY, &f);
  if (rc != FL_OK) return rc;
  uint8_t *b = malloc(x->len + 1);
  int64_t n = fl_file_pread(f, b, x->len + 1, 0);
  fl_file_close(f);
  rc = n == (int64_t)x->len && !memcmp(b, x->data, x->len) ? FL_OK : FL_ECORRUPT;
  if (rc != FL_OK) fprintf(stderr, "%s : %lld octets lus, %zu attendus\n", x->chemin, (long long)n, x->len);
  free(b);
  return rc;
}

static int appliquer_fich(etat *e, const fich *x) {
  return tm_apply_file(e->m, x->chemin, x->data, x->len, e->uid, e->gid, 0644);
}

/* Un pas : FL_OK, ou l'erreur du moteur ou du modèle (-100). */
static int pas(etat *e, fl_fs *fs) {
  uint32_t r = entre(100);
  if (e->nf == 0) r = 0;
  if (r < 20) { /* création */
    const doss *p = &e->d[entre((uint32_t)e->nd)];
    char c[4200];
    nom_libre(e, p->chemin, c, sizeof c);
    size_t len = entre(5) ? entre(64 * 1024) : 0;
    uint8_t *data = malloc(len ? len : 1);
    remplir(data, len);
    int rc = ecrire_tout(fs, c, FL_O_RDWR | FL_O_CREAT, data, len, 0);
    if (rc != FL_OK) {
      free(data);
      return rc;
    }
    ajouter_fich(e, c, data, len);
    return appliquer_fich(e, &e->f[e->nf - 1]) ? -100 : FL_OK;
  }
  fich *x = &e->f[entre((uint32_t)e->nf)];
  if (r < 32 || r < 44) { /* ajout à la fin, ou réécriture au milieu (peut prolonger) */
    size_t add = 1 + entre(32 * 1024);
    size_t off = r < 32 ? x->len : entre((uint32_t)x->len + 1);
    if (off + add > FICHIER_MAX) return FL_OK;
    uint8_t *nb = realloc(x->data, off + add > x->len ? off + add : x->len);
    x->data = nb;
    uint8_t *src = malloc(add);
    remplir(src, add);
    int rc = ecrire_tout(fs, x->chemin, FL_O_RDWR, src, add, off);
    if (rc == FL_OK) {
      memcpy(x->data + off, src, add);
      if (off + add > x->len) x->len = off + add;
    }
    free(src);
    return rc != FL_OK ? rc : appliquer_fich(e, x) ? -100 : FL_OK;
  }
  if (r < 54) { /* troncature vers le haut (trou) ou vers le bas */
    size_t t = entre(2) ? entre((uint32_t)x->len + 1) : x->len + entre(64 * 1024);
    if (t > FICHIER_MAX) t = FICHIER_MAX;
    fl_file *f = NULL;
    int rc = fl_fs_open(fs, x->chemin, FL_O_RDWR, &f);
    if (rc != FL_OK) return rc;
    rc = fl_file_truncate(f, t);
    int rc2 = fl_file_close(f);
    if (rc == FL_OK) rc = rc2;
    if (rc != FL_OK) return rc;
    uint8_t *nb = realloc(x->data, t ? t : 1);
    x->data = nb;
    if (t > x->len) memset(x->data + x->len, 0, t - x->len);
    x->len = t;
    return appliquer_fich(e, x) ? -100 : FL_OK;
  }
  if (r < 62) { /* renommage ou déplacement d'un fichier */
    const doss *p = &e->d[entre((uint32_t)e->nd)];
    char c[4200];
    nom_libre(e, p->chemin, c, sizeof c);
    int rc = fl_fs_rename(fs, x->chemin, c);
    if (rc != FL_OK) return rc;
    if (tm_apply_rename(e->m, x->chemin, c)) return -100;
    free(x->chemin);
    x->chemin = strdup(c);
    return FL_OK;
  }
  if (r < 66 && e->nd > 1) { /* déplacement d'un dossier (jamais sous lui-même) */
    doss *d = &e->d[1 + entre((uint32_t)e->nd - 1)];
    const doss *p = &e->d[entre((uint32_t)e->nd)];
    if (p == d || sous(p->chemin, d->chemin)) return FL_OK;
    int prof_sous = 0;
    for (size_t i = 0; i < e->nd; i++)
      if (sous(e->d[i].chemin, d->chemin) && e->d[i].prof - d->prof > prof_sous) prof_sous = e->d[i].prof - d->prof;
    if (p->prof + 1 + prof_sous > PROFONDEUR_MAX) return FL_OK;
    char c[4200];
    nom_libre(e, p->chemin, c, sizeof c);
    /* les chemins des descendants doivent rester ≤ 4095 octets */
    size_t ld = strlen(d->chemin), lc = strlen(c);
    for (size_t i = 0; i < e->nf; i++)
      if (sous(e->f[i].chemin, d->chemin) && strlen(e->f[i].chemin) - ld + lc > 4000) return FL_OK;
    for (size_t i = 0; i < e->nd; i++)
      if (sous(e->d[i].chemin, d->chemin) && strlen(e->d[i].chemin) - ld + lc > 4000) return FL_OK;
    int rc = fl_fs_rename(fs, d->chemin, c);
    if (rc != FL_OK) return rc;
    if (tm_apply_rename(e->m, d->chemin, c)) return -100;
    int dp = p->prof + 1 - d->prof;
    char *ancien = strdup(d->chemin);
    for (size_t i = 0; i < e->nf; i++)
      if (sous(e->f[i].chemin, ancien)) {
        char n[8400];
        snprintf(n, sizeof n, "%s%s", c, e->f[i].chemin + ld);
        free(e->f[i].chemin);
        e->f[i].chemin = strdup(n);
      }
    for (size_t i = 0; i < e->nd; i++)
      if (sous(e->d[i].chemin, ancien)) {
        char n[8400];
        snprintf(n, sizeof n, "%s%s", c, e->d[i].chemin + ld);
        free(e->d[i].chemin);
        e->d[i].chemin = strdup(n);
        e->d[i].prof += dp;
      }
    free(d->chemin);
    d->chemin = strdup(c);
    d->prof += dp;
    free(ancien);
    return FL_OK;
  }
  if (r < 76) { /* suppression d'un fichier */
    int rc = fl_fs_unlink(fs, x->chemin);
    if (rc != FL_OK) return rc;
    if (tm_apply_unlink(e->m, x->chemin)) return -100;
    free(x->chemin);
    free(x->data);
    *x = e->f[--e->nf];
    return FL_OK;
  }
  if (r < 86) { /* mkdir : parfois au bout de la chaîne la plus profonde */
    const doss *p = &e->d[entre((uint32_t)e->nd)];
    if (entre(3) == 0)
      for (size_t i = 0; i < e->nd; i++)
        if (e->d[i].prof > p->prof && e->d[i].prof < PROFONDEUR_MAX) p = &e->d[i];
    if (p->prof >= PROFONDEUR_MAX) return FL_OK;
    char c[4200];
    nom_libre(e, p->chemin, c, sizeof c);
    int prof = p->prof + 1;
    int rc = fl_fs_mkdir(fs, c);
    if (rc != FL_OK) return rc;
    if (tm_apply_mkdir(e->m, c, e->uid, e->gid, 0755)) return -100;
    ajouter_doss(e, c, prof);
    return FL_OK;
  }
  if (r < 91 && e->nd > 1) { /* rmdir d'un dossier vide */
    size_t k = 1 + entre((uint32_t)e->nd - 1);
    for (size_t i = 0; i < e->nf; i++)
      if (sous(e->f[i].chemin, e->d[k].chemin)) return FL_OK;
    for (size_t i = 0; i < e->nd; i++)
      if (sous(e->d[i].chemin, e->d[k].chemin)) return FL_OK;
    int rc = fl_fs_rmdir(fs, e->d[k].chemin);
    if (rc != FL_OK) return rc;
    if (tm_apply_rmdir(e->m, e->d[k].chemin)) return -100;
    free(e->d[k].chemin);
    e->d[k] = e->d[--e->nd];
    return FL_OK;
  }
  return verifier_fich(fs, x); /* relecture */
}

/* Dossier des images laissées pour la vérification par le noyau. */
static void dossier_batterie(char *out, size_t cap) {
  char fx[4096];
  snprintf(fx, sizeof fx, "%s", tt_fixture("x"));
  snprintf(out, cap, "%s/../batterie", dirname(fx)); /* build/native/batterie */
  mkdir(out, 0755);
}

static int copier(const char *src, const char *dst) {
  char out[4096];
  char *const argv[] = {(char *)"cp", (char *)"--sparse=always", (char *)src, (char *)dst, NULL};
  return fu_run(argv, out, sizeof out);
}

/* Une batterie de `n` pas, juge tous les `tous` pas. */
static void batterie(const char *nom, const genre *g, int n, int tous, uint64_t defaut) {
  vol v = {.g = g};
  etat e = {0};
  uint64_t s = graine(defaut);
  g_x = s * 0x9E3779B97F4A7C15ull + 1;
  fprintf(stderr, "%s : graine %llu (FL_SEED=%llu pour rejouer), %d pas\n", nom, (unsigned long long)s,
          (unsigned long long)s, n);
  char src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture(g->fixture));
  TT_ASSERT_EQ(fu_copy_image(src, v.img, sizeof v.img), 0);
  TT_ASSERT_EQ(vol_monter(&v), FL_OK);
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/", &st), FL_OK);
  e.uid = st.uid;
  e.gid = st.gid;
  e.m = tm_clone(g_base);
  TT_ASSERT(e.m != NULL);
  int rc = fl_fs_mkdir(v.fs, "/b");
  if (rc == FL_OK) rc = tm_apply_mkdir(e.m, "/b", e.uid, e.gid, 0755) ? -100 : FL_OK;
  ajouter_doss(&e, "/b", 1);
  int k = 0, ok = rc == FL_OK;
  for (k = 1; ok && k <= n; k++) {
    rc = pas(&e, v.fs);
    if (rc != FL_OK) {
      fprintf(stderr, "%s : pas %d, erreur %d (graine %llu)\n", nom, k, rc, (unsigned long long)s);
      ok = 0;
      break;
    }
    if (k % tous == 0 || k == n) {
      for (size_t i = 0; ok && i < e.nf; i++) ok = verifier_fich(v.fs, &e.f[i]) == FL_OK;
      if (vol_demonter(&v) != FL_OK) ok = 0;
      if (ok && juger(&v, e.m) != 0) {
        fprintf(stderr, "%s : juge en échec au pas %d (graine %llu)\n", nom, k, (unsigned long long)s);
        ok = 0;
      }
      if (ok && k < n && vol_monter(&v) != FL_OK) ok = 0;
    }
  }
  if (v.fs) vol_demonter(&v);
  fprintf(stderr, "%s : %zu fichiers, %zu dossiers\n", nom, e.nf, e.nd);
  if (ok) {
    char dir[4200], dst[4400];
    dossier_batterie(dir, sizeof dir);
    snprintf(dst, sizeof dst, "%s/%s.ecrite.img", dir, nom);
    TT_ASSERT_EQ(copier(v.img, dst), 0);
    snprintf(dst, sizeof dst, "%s/%s.manifest", dir, nom);
    TT_ASSERT_EQ(tm_write_manifest(e.m, dst), 0);
    if (g->luks) {
      snprintf(dst, sizeof dst, "%s/%s.pass", dir, nom);
      char pass[4096];
      snprintf(pass, sizeof pass, "%s", tt_fixture("pass.txt"));
      TT_ASSERT_EQ(copier(pass, dst), 0);
    }
  }
  unlink(v.img);
  etat_libre(&e);
  TT_ASSERT(ok);
}

static const genre G_REEL = {"ext4-rw-reel.img", 512, false};
static const genre G_1K = {"ext4-rw-1k.img", 512, false};
static const genre G_UNINIT = {"ext4-rw-uninit.img", 512, false};
static const genre G_SEED = {"ext4-rw-seed.img", 512, false};
static const genre G_LUKS2 = {"luks2-rw-reel.img", 512, true};
static const genre G_4K = {"ext4-rw-reel.img", 4096, false};
static const genre G_GROS = {"ext4-rw-gros.img", 512, false};

static void test_batterie_reel(void) { batterie("reel", &G_REEL, 2000, 200, 1); }
static void test_batterie_1k(void) { batterie("1k", &G_1K, 1000, 200, 2); }
static void test_batterie_uninit(void) { batterie("uninit", &G_UNINIT, 1000, 200, 3); }
static void test_batterie_seed(void) { batterie("seed", &G_SEED, 1000, 200, 4); }
static void test_batterie_luks2(void) { batterie("luks2", &G_LUKS2, 1000, 200, 5); }
static void test_batterie_secteurs_4k(void) { batterie("secteurs-4k", &G_4K, 1000, 200, 6); }

static void test_batterie_longue(void) {
  for (uint64_t s = 1; s <= 5; s++) {
    char nom[32];
    snprintf(nom, sizeof nom, "longue-%llu", (unsigned long long)s);
    batterie(nom, &G_REEL, 20000, 1000, s);
  }
}

/* Lectures du moteur à quelques décalages d'un fichier de `taille` octets, motif `motif` (NULL :
 * zéros) écrit à `debut`. */
static bool relire_motif(fl_fs *fs, const char *chemin, uint64_t off, const uint8_t *attendu, size_t n) {
  fl_file *f = NULL;
  if (fl_fs_open(fs, chemin, FL_O_RDONLY, &f) != FL_OK) return false;
  uint8_t *b = malloc(n);
  bool ok = fl_file_pread(f, b, n, off) == (int64_t)n;
  for (size_t i = 0; ok && i < n; i++) ok = b[i] == (attendu ? attendu[i] : 0);
  free(b);
  fl_file_close(f);
  return ok;
}

/* Un fichier creux de 5 Gio, écrit à 4,5 Gio puis agrandi par troncature. */
static void test_batterie_gros_fichiers(void) {
  vol v = {.g = &G_GROS};
  char src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture(G_GROS.fixture));
  TT_ASSERT_EQ(fu_copy_image(src, v.img, sizeof v.img), 0);
  TT_ASSERT_EQ(vol_monter(&v), FL_OK);
  uint8_t *b = malloc(MIB);
  TT_ASSERT(b != NULL);
  g_x = 77;
  remplir(b, MIB);
  const uint64_t a = 9 * GIB / 2, t = 5 * GIB;
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/creux.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pwrite(f, b, MIB, a), (int64_t)MIB);
  TT_ASSERT_EQ(fl_file_truncate(f, t), FL_OK);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT(relire_motif(v.fs, "/creux.bin", a, b, MIB));
  TT_ASSERT(relire_motif(v.fs, "/creux.bin", a - 4096, NULL, 4096));
  TT_ASSERT(relire_motif(v.fs, "/creux.bin", 0, NULL, 65536));
  TT_ASSERT(relire_motif(v.fs, "/creux.bin", t - 65536, NULL, 65536));
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  static char out[1 << 16];
  TT_ASSERT_EQ(fu_e2fsck_n(v.img, out, sizeof out), 0);
  uint64_t taille = 0;
  TT_ASSERT_EQ(fu_debugfs_inode_field(v.img, "/creux.bin", "size", &taille), 0);
  TT_ASSERT_EQ(taille, t);
  free(b);
  unlink(v.img);
}

/* Variante lente : un fichier dense de 4,1 Gio (au-delà de 2^32 octets), relu par sha256. */
static void test_batterie_gros_dense(void) {
  vol v = {.g = &G_GROS};
  char src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture(G_GROS.fixture));
  TT_ASSERT_EQ(fu_copy_image(src, v.img, sizeof v.img), 0);
  TT_ASSERT_EQ(vol_monter(&v), FL_OK);
  const uint64_t n = 41 * GIB / 10;
  uint8_t *b = malloc(MIB);
  TT_ASSERT(b != NULL);
  mbedtls_sha256_context h1, h2;
  mbedtls_sha256_init(&h1);
  mbedtls_sha256_init(&h2);
  TT_ASSERT(mbedtls_sha256_starts(&h1, 0) == 0 && mbedtls_sha256_starts(&h2, 0) == 0);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/dense.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  g_x = 41;
  for (uint64_t o = 0; o < n; o += MIB) {
    size_t k = n - o < MIB ? (size_t)(n - o) : MIB;
    remplir(b, k);
    mbedtls_sha256_update(&h1, b, k);
    TT_ASSERT_EQ(fl_file_pwrite(f, b, k, o), (int64_t)k);
  }
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(vol_monter(&v), FL_OK);
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/dense.bin", FL_O_RDONLY, &f), FL_OK);
  for (uint64_t o = 0; o < n; o += MIB) {
    size_t k = n - o < MIB ? (size_t)(n - o) : MIB;
    TT_ASSERT_EQ(fl_file_pread(f, b, k, o), (int64_t)k);
    mbedtls_sha256_update(&h2, b, k);
  }
  fl_file_close(f);
  uint8_t d1[32], d2[32];
  mbedtls_sha256_finish(&h1, d1);
  mbedtls_sha256_finish(&h2, d2);
  mbedtls_sha256_free(&h1);
  mbedtls_sha256_free(&h2);
  TT_ASSERT_MEM_EQ(d1, d2, 32);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  static char out[1 << 16];
  TT_ASSERT_EQ(fu_e2fsck_n(v.img, out, sizeof out), 0);
  free(b);
  unlink(v.img);
}

/* 20 000 fichiers de 0 à 8 Kio dans 50 dossiers (ext4-rw-gros.img : ext4-rw-reel.img n'a que 16 384
 * inodes). */
static void test_batterie_petits_fichiers(void) {
  vol v = {.g = &G_GROS};
  char src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture(G_GROS.fixture));
  TT_ASSERT_EQ(fu_copy_image(src, v.img, sizeof v.img), 0);
  TT_ASSERT_EQ(vol_monter(&v), FL_OK);
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/", &st), FL_OK);
  tm *m = tm_clone(g_base);
  TT_ASSERT(m != NULL);
  uint8_t b[8192];
  g_x = 2020;
  int ok = 1;
  for (int d = 0; ok && d < 50; d++) {
    char p[64];
    snprintf(p, sizeof p, "/p%02d", d);
    ok = fl_fs_mkdir(v.fs, p) == FL_OK && tm_apply_mkdir(m, p, st.uid, st.gid, 0755) == 0;
  }
  for (int i = 0; ok && i < 20000; i++) {
    char p[64];
    snprintf(p, sizeof p, "/p%02d/f%05d", i % 50, i);
    size_t n = entre(8193);
    remplir(b, n);
    ok = ecrire_tout(v.fs, p, FL_O_RDWR | FL_O_CREAT, b, n, 0) == FL_OK &&
         tm_apply_file(m, p, b, n, st.uid, st.gid, 0644) == 0;
    if (!ok) fprintf(stderr, "petits fichiers : échec au fichier %d\n", i);
  }
  TT_ASSERT(ok);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(juger(&v, m), 0);
  tm_free(m);
  unlink(v.img);
}

/* Disque plein : la dernière écriture rend FL_ENOSPC, le fichier partiel est lisible puis
 * supprimable, e2fsck est propre et la place revient. */
static void test_batterie_disque_plein(void) {
  vol v = {.g = &G_1K};
  char src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture(G_1K.fixture));
  TT_ASSERT_EQ(fu_copy_image(src, v.img, sizeof v.img), 0);
  TT_ASSERT_EQ(vol_monter(&v), FL_OK);
  fl_statfs s0, s1, s2;
  TT_ASSERT_EQ(fl_fs_statfs(v.fs, &s0), FL_OK);
  const size_t tr = 256 * 1024;
  uint8_t *b = malloc(tr);
  TT_ASSERT(b != NULL);
  g_x = 64;
  remplir(b, tr);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/plein.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  uint64_t ecrit = 0;
  int64_t w = 0;
  for (int i = 0; i < 1000; i++) {
    w = fl_file_pwrite(f, b, tr, ecrit);
    if (w != (int64_t)tr) break;
    ecrit += tr;
  }
  fprintf(stderr, "disque plein après %llu octets (%lld)\n", (unsigned long long)ecrit, (long long)w);
  TT_ASSERT_EQ(w, FL_ENOSPC);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_stat(v.fs, "/plein.bin", &st), FL_OK);
  TT_ASSERT(st.size >= ecrit && st.size < ecrit + tr);
  TT_ASSERT_EQ(fl_fs_statfs(v.fs, &s1), FL_OK);
  TT_ASSERT(!s1.read_only);
  TT_ASSERT(s1.free_bytes < 64 * 1024);
  /* le fichier partiel se relit : les tranches entières, puis la partielle (contenu de b) */
  for (uint64_t o = 0; o < st.size; o += tr) {
    size_t k = st.size - o < tr ? (size_t)(st.size - o) : tr;
    TT_ASSERT(relire_motif(v.fs, "/plein.bin", o, b, k));
  }
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  static char out[1 << 16];
  TT_ASSERT_EQ(fu_e2fsck_n(v.img, out, sizeof out), 0);
  TT_ASSERT_EQ(vol_monter(&v), FL_OK);
  TT_ASSERT_EQ(fl_fs_unlink(v.fs, "/plein.bin"), FL_OK);
  TT_ASSERT_EQ(fl_fs_statfs(v.fs, &s2), FL_OK);
  fprintf(stderr, "libre : %llu au départ, %llu plein, %llu après suppression\n",
          (unsigned long long)s0.free_bytes, (unsigned long long)s1.free_bytes,
          (unsigned long long)s2.free_bytes);
  TT_ASSERT_EQ(s2.free_bytes, s0.free_bytes);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(fu_e2fsck_n(v.img, out, sizeof out), 0);
  free(b);
  unlink(v.img);
}

int main(int argc, char **argv) {
  if (!fu_tools_available()) {
    printf("SAUTÉ : e2fsck/debugfs/dumpe2fs absents ou plateforme Android (banc réservé au PC)\n");
    return 77; /* SKIP_RETURN_CODE de CTest */
  }
  fl_fs_set_clock(horloge);
  /* clairsemé.bin (5 Gio) : debugfs ne compare que sa taille ; tout le reste par son contenu */
  tm_set_debugfs_full_limit(64 * MIB);
  char tree[4096];
  snprintf(tree, sizeof tree, "%s", tt_fixture("tree"));
  g_base = tm_new();
  if (!g_base || tm_from_dir(g_base, tree) != 0) {
    fprintf(stderr, "modèle de tree/ impossible\n");
    return 1;
  }
  if (argc > 1 && !strcmp(argv[1], "lent")) {
    TT_RUN(test_batterie_longue);
    TT_RUN(test_batterie_gros_dense);
  } else {
    TT_RUN(test_batterie_disque_plein);
    TT_RUN(test_batterie_reel);
    TT_RUN(test_batterie_1k);
    TT_RUN(test_batterie_uninit);
    TT_RUN(test_batterie_seed);
    TT_RUN(test_batterie_luks2);
    TT_RUN(test_batterie_secteurs_4k);
    TT_RUN(test_batterie_gros_fichiers);
    TT_RUN(test_batterie_petits_fichiers);
  }
  tm_free(g_base);
  fl_fs_set_clock(NULL);
  TT_MAIN_END();
}
