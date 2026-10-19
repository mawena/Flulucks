#define _GNU_SOURCE
/* Batterie aléatoire FAT16 / FAT32 / exFAT (phase 4, T8) : opérations pseudo-aléatoires (graine affichée,
 * FL_SEED pour la fixer) appliquées au volume et au modèle ; toutes les 50 opérations, sync puis
 * comparaison par le moteur ; à la fin, démontage, fsck et fatjuge. FL_KEEP=1 garde l'image et le
 * manifeste (build/native/tmp/fat-batterie-<type>.{img,manifest}) pour tools/fixtures/verify_fat_with_kernel.sh.
 * « test_fat_batterie lent » : 5 000 opérations par image. Réservé au PC. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fluluks/probe.h"
#include "fsck_util.h"
#include "tree_model.h"
#include "tt.h"

#define MAXE 400
#define MAXLEN (128 * 1024)

typedef struct {
  char path[256];
  int dir;
  uint8_t *data;
  size_t len;
} ent;

typedef struct {
  ent e[MAXE];
  size_t n;
  uint64_t rng;
  int ecarts;
  unsigned long seq;
} etat;

static uint32_t alea(etat *s) {
  s->rng ^= s->rng << 13;
  s->rng ^= s->rng >> 7;
  s->rng ^= s->rng << 17;
  return (uint32_t)(s->rng >> 11);
}

static void ecart(etat *s, const char *op, const char *p, int rc, int attendu) {
  if (s->ecarts < 10) fprintf(stderr, "opération %lu, %s %s : %d, attendu %d\n", s->seq, op, p, rc, attendu);
  s->ecarts++;
}

/* Entrée de chemin p (comparaison insensible à la casse, comme FAT pour l'ASCII), ou -1. */
static int trouver(const etat *s, const char *p) {
  for (size_t i = 0; i < s->n; i++)
    if (!strcasecmp(s->e[i].path, p)) return (int)i;
  return -1;
}

static int au_hasard(etat *s, int dir) {
  int cand[MAXE], k = 0;
  for (size_t i = 0; i < s->n; i++)
    if (s->e[i].dir == dir) cand[k++] = (int)i;
  return k ? cand[alea(s) % (uint32_t)k] : -1;
}

/* Dossier parent tiré au hasard (racine comprise), de profondeur < 3. */
static const char *parent_au_hasard(etat *s) {
  for (int essai = 0; essai < 4; essai++) {
    int d = au_hasard(s, 1);
    if (d < 0) break;
    int prof = 0;
    for (const char *c = s->e[d].path; *c; c++) prof += *c == '/';
    if (prof < 3) return s->e[d].path;
  }
  return "";
}

static void nom_neuf(etat *s, const char *parent, int dir, char *out) {
  for (;;) {
    snprintf(out, 256, "%s/%s%u%s", parent, dir ? "Dossier " : "fichier ", alea(s) % 100000, dir ? "" : ".bin");
    if (trouver(s, out) < 0) return;
  }
}

static int ajouter(etat *s, const char *p, int dir) {
  if (s->n >= MAXE) return -1;
  ent *x = &s->e[s->n++];
  memset(x, 0, sizeof *x);
  snprintf(x->path, sizeof x->path, "%s", p);
  x->dir = dir;
  return (int)(s->n - 1);
}

static void retirer(etat *s, int i) {
  free(s->e[i].data);
  s->e[i] = s->e[--s->n];
}

static int enfants(const etat *s, const char *dir) {
  size_t k = strlen(dir);
  for (size_t i = 0; i < s->n; i++)
    if (!strncasecmp(s->e[i].path, dir, k) && s->e[i].path[k] == '/') return 1;
  return 0;
}

/* pwrite au modèle (zéros dans le trou). */
static void modele_ecrire(ent *x, const uint8_t *b, size_t len, size_t off) {
  if (off + len > x->len) {
    x->data = realloc(x->data, off + len);
    memset(x->data + x->len, 0, off + len - x->len);
    x->len = off + len;
  }
  memcpy(x->data + off, b, len);
}

static void modele_tronquer(ent *x, size_t len) {
  if (len > x->len) {
    x->data = realloc(x->data, len);
    memset(x->data + x->len, 0, len - x->len);
  }
  x->len = len;
}

static void remplir(etat *s, uint8_t *b, size_t n) {
  for (size_t i = 0; i < n; i++) b[i] = (uint8_t)alea(s);
}

static int montrer(void *ctx, const fl_dirent *e) {
  (void)ctx;
  fprintf(stderr, "  le moteur voit : « %s » (type %d, %llu octets)\n", e->name, e->st.type, (unsigned long long)e->st.size);
  return 0;
}

static void une_operation(etat *s, fl_fs *fs, tm *m, uint8_t *buf) {
  uint32_t r = alea(s) % 100;
  char p[256], q[300];
  int rc;
  if (s->n >= MAXE && r < 23) return; /* table pleine : pas de création (le volume la ferait sans suivi) */
  if (r < 15 || s->n < 3) { /* création de fichier */
    nom_neuf(s, parent_au_hasard(s), 0, p);
    size_t len = alea(s) % (64 * 1024);
    remplir(s, buf, len);
    fl_file *f = NULL;
    rc = fl_fs_open(fs, p, FL_O_RDWR | FL_O_CREAT | FL_O_TRUNC, &f);
    if (rc == FL_OK && len && fl_file_pwrite(f, buf, len, 0) != (int64_t)len) rc = FL_EIO;
    int c = f ? fl_file_close(f) : FL_OK;
    if (rc == FL_OK) rc = c;
    if (rc != FL_OK) return ecart(s, "création", p, rc, FL_OK);
    int i = ajouter(s, p, 0);
    if (i >= 0) modele_ecrire(&s->e[i], buf, len, 0);
    tm_apply_file(m, p, buf, len, 0, 0, 0644);
  } else if (r < 23) { /* mkdir */
    nom_neuf(s, parent_au_hasard(s), 1, p);
    if ((rc = fl_fs_mkdir(fs, p)) != FL_OK) return ecart(s, "mkdir", p, rc, FL_OK);
    ajouter(s, p, 1);
    tm_apply_mkdir(m, p, 0, 0, 0755);
  } else if (r < 58) { /* pwrite (r < 48) ou truncate */
    int i = au_hasard(s, 0);
    if (i < 0) return;
    ent *x = &s->e[i];
    fl_file *f = NULL;
    rc = fl_fs_open(fs, x->path, FL_O_RDWR, &f);
    size_t off = alea(s) % (x->len + 64 * 1024), len = 1 + alea(s) % (32 * 1024);
    if (off + len > MAXLEN) off = 0;
    if (r < 48) {
      remplir(s, buf, len);
      if (rc == FL_OK && fl_file_pwrite(f, buf, len, off) != (int64_t)len) rc = FL_EIO;
    } else if (rc == FL_OK) {
      rc = fl_file_truncate(f, off);
    }
    int c = f ? fl_file_close(f) : FL_OK;
    if (rc == FL_OK) rc = c;
    if (rc != FL_OK) return ecart(s, r < 48 ? "pwrite" : "truncate", x->path, rc, FL_OK);
    if (r < 48) modele_ecrire(x, buf, len, off);
    else modele_tronquer(x, off);
    tm_apply_file(m, x->path, x->data, x->len, 0, 0, 0644);
  } else if (r < 79) { /* renommage : même dossier, autre dossier, ou casse seule */
    int i = alea(s) % 3 ? au_hasard(s, 0) : au_hasard(s, 1);
    if (i < 0 || !strcmp(s->e[i].path, "")) return;
    ent *x = &s->e[i];
    const char *slash = strrchr(x->path, '/');
    if (r < 66) { /* casse seule */
      snprintf(p, sizeof p, "%s", x->path);
      for (char *c = p + (slash - x->path) + 1; *c; c++)
        if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')) *c ^= 0x20;
    } else {
      const char *par = r < 72 ? "" : parent_au_hasard(s);
      if (r < 72) {
        snprintf(q, sizeof q, "%.*s", (int)(slash - x->path), x->path);
        par = q;
      }
      size_t k = strlen(x->path);
      if (x->dir && !strncasecmp(par, x->path, k) && (par[k] == '/' || !par[k])) return; /* sous lui-même */
      char tmp[300];
      snprintf(tmp, sizeof tmp, "%s", par);
      nom_neuf(s, tmp, x->dir, p);
    }
    if ((rc = fl_fs_rename(fs, x->path, p)) != FL_OK) return ecart(s, "rename", x->path, rc, FL_OK);
    tm_apply_rename(m, x->path, p);
    size_t k = strlen(x->path);
    char ancien[256];
    snprintf(ancien, sizeof ancien, "%s", x->path);
    for (size_t j = 0; j < s->n; j++) /* le dossier et ses descendants */
      if (!strcmp(s->e[j].path, ancien) || (!strncmp(s->e[j].path, ancien, k) && s->e[j].path[k] == '/')) {
        char np[600];
        snprintf(np, sizeof np, "%s%s", p, s->e[j].path + k);
        if (strlen(np) < sizeof s->e[j].path) memcpy(s->e[j].path, np, strlen(np) + 1);
      }
  } else if (r < 87) { /* unlink */
    int i = au_hasard(s, 0);
    if (i < 0) return;
    snprintf(p, sizeof p, "%s", s->e[i].path);
    if ((rc = fl_fs_unlink(fs, p)) != FL_OK) return ecart(s, "unlink", p, rc, FL_OK);
    tm_apply_unlink(m, p);
    retirer(s, i);
  } else if (r < 92) { /* rmdir */
    int i = au_hasard(s, 1);
    if (i < 0 || !s->e[i].path[0]) return; /* pas la racine */
    snprintf(p, sizeof p, "%s", s->e[i].path);
    int plein = enfants(s, p);
    rc = fl_fs_rmdir(fs, p);
    if (rc != (plein ? FL_ENOTEMPTY : FL_OK)) {
      fl_fs_readdir(fs, p, montrer, NULL);
      return ecart(s, "rmdir", p, rc, plein ? FL_ENOTEMPTY : FL_OK);
    }
    if (!plein) {
      tm_apply_rmdir(m, p);
      retirer(s, i);
    }
  } else { /* même fichier ouvert deux fois : écrit par l'un, relu par l'autre */
    int i = au_hasard(s, 0);
    if (i < 0) return;
    ent *x = &s->e[i];
    fl_file *w = NULL, *rd = NULL;
    size_t len = 1 + alea(s) % 8192, off = x->len ? alea(s) % x->len : 0;
    remplir(s, buf, len);
    uint8_t back[8192];
    rc = fl_fs_open(fs, x->path, FL_O_RDWR, &w);
    if (rc == FL_OK) rc = fl_fs_open(fs, x->path, FL_O_RDONLY, &rd);
    if (rc == FL_OK && fl_file_pwrite(w, buf, len, off) != (int64_t)len) rc = FL_EIO;
    if (rc == FL_OK && (fl_file_pread(rd, back, len, off) != (int64_t)len || memcmp(back, buf, len))) rc = FL_EIO;
    if (rd) fl_file_close(rd);
    int c = w ? fl_file_close(w) : FL_OK;
    if (rc == FL_OK) rc = c;
    if (rc != FL_OK) return ecart(s, "double ouverture", x->path, rc, FL_OK);
    modele_ecrire(x, buf, len, off);
    tm_apply_file(m, x->path, x->data, x->len, 0, 0, 0644);
  }
}

static void batterie(const char *fixture, const char *arbre, const char *type, unsigned ops, uint64_t graine) {
  char copie[4096];
  TT_ASSERT(fu_copy_image(tt_fixture(fixture), copie, sizeof copie) == 0);
  etat *s = calloc(1, sizeof *s);
  uint8_t *buf = malloc(64 * 1024);
  tm *m = tm_new();
  TT_ASSERT(s && buf && m);
  s->rng = graine ? graine : 1;
  ajouter(s, "", 1); /* racine */
  if (arbre) {
    char src[4096];
    snprintf(src, sizeof src, "%s", tt_fixture(arbre));
    TT_ASSERT(tm_from_dir(m, src) == 0);
  }
  tm_set_fat_attrs(m);
  fl_bdev *d = NULL;
  fl_fs *fs = NULL;
  fl_probe_result pr;
  int rc = fl_file_bdev_open(copie, 512, false, &d);
  if (rc == FL_OK) rc = fl_probe(d, &pr);
  if (rc == FL_OK) rc = fl_fs_mount(d, pr.content, true, &fs);
  int ecarts_modele = 0;
  for (unsigned k = 0; rc == FL_OK && k < ops && s->ecarts == 0; k++) {
    s->seq = k;
    une_operation(s, fs, m, buf);
    if ((k + 1) % 50 == 0) {
      int sy = fl_fs_sync(fs);
      if (sy != FL_OK) ecart(s, "sync", "", sy, FL_OK);
      int e = tm_check_fl(m, fs);
      if (e) {
        fprintf(stderr, "%s : %d écart(s) moteur/modèle après %u opérations\n", type, e, k + 1);
        ecarts_modele += e;
        break;
      }
    }
  }
  int um = fs ? fl_fs_unmount(fs) : FL_OK;
  fl_bdev_close(d);
  char out[8192];
  int f = fu_fsck_fat_n(copie, out, sizeof out);
  if (f) fprintf(stderr, "%s : fsck = %d\n%s\n", type, f, out);
  int j = tm_check_fat_judge(m, copie);
  const char *keep = getenv("FL_KEEP");
  if (keep && !strcmp(keep, "1")) {
    char dir[4096], img[4200], man[4200];
    if (fu_tmpdir(dir, sizeof dir) == 0) {
      snprintf(img, sizeof img, "%s/fat-batterie-%s.img", dir, type);
      snprintf(man, sizeof man, "%s/fat-batterie-%s.manifest", dir, type);
      if (rename(copie, img) == 0 && tm_write_manifest(m, man) == 0) fprintf(stderr, "gardé : %s\n", img);
    }
  }
  unlink(copie);
  for (size_t i = 0; i < s->n; i++) free(s->e[i].data);
  int ecarts = s->ecarts;
  free(s);
  free(buf);
  tm_free(m);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(ecarts, 0);
  TT_ASSERT_EQ(ecarts_modele, 0);
  TT_ASSERT_EQ(um, FL_OK);
  TT_ASSERT_EQ(f, 0);
  TT_ASSERT_EQ(j, 0);
}

static unsigned g_ops = 400;
static uint64_t g_graine;

static void test_fat_batterie_fat16(void) { batterie("fat16-arbre.img", "tree-fat", "fat16", g_ops, g_graine); }
static void test_fat_batterie_fat32(void) { batterie("fat32-vide.img", NULL, "fat32", g_ops, g_graine + 1); }
static void test_fat_batterie_exfat(void) { batterie("exfat-vide.img", NULL, "exfat", g_ops, g_graine + 2); }

int main(int argc, char **argv) {
  if (!fu_fat_tools_available()) {
    printf("SAUTÉ : outils FAT/exFAT absents ou plateforme Android (banc réservé au PC)\n");
    return 77;
  }
  setenv("TZ", "UTC", 1);
  tzset();
  if (argc > 1 && !strcmp(argv[1], "lent")) g_ops = 5000;
  if (getenv("FL_OPS")) g_ops = (unsigned)strtoul(getenv("FL_OPS"), NULL, 10);
  const char *sd = getenv("FL_SEED");
  g_graine = sd && *sd ? strtoull(sd, NULL, 10) : 20261019;
  fprintf(stderr, "graine : %llu (FL_SEED pour la fixer)\n", (unsigned long long)g_graine);
  TT_RUN(test_fat_batterie_fat16);
  TT_RUN(test_fat_batterie_fat32);
  TT_RUN(test_fat_batterie_exfat);
  TT_MAIN_END();
}
