#define _GNU_SOURCE
/* Disques FAT/exFAT hostiles (phase 4, T8) : images abîmées en mémoire (octets aléatoires à graine fixe dans
 * le secteur de démarrage, la FAT ou le bitmap, la racine ; cas fabriqués : chaînes en boucle, cluster hors
 * bornes, BPB incohérent, sommes exFAT fausses, taille démesurée). Chaque image est montée en lecture
 * seule ; si le montage réussit, l'arborescence est parcourue (bornée) et chaque fichier lu en entier.
 * Attendu : ni plantage ni rapport ASan/UBSan, ni boucle sans fin (délai CTest), seulement FL_OK ou un
 * fl_err. Tourne aussi sur Android. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fluluks/probe.h"
#include "tt.h"

/* --- périphérique en mémoire (lecture seule) ----------------------------------------------------- */

typedef struct {
  uint8_t *data;
} mem;

static int m_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  memcpy(buf, ((mem *)d->priv)->data + lba * d->sector_size, (size_t)count * d->sector_size);
  return FL_OK;
}
static int m_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf) {
  (void)d, (void)lba, (void)count, (void)buf;
  return FL_EROFS;
}
static int m_flush(fl_bdev *d) {
  (void)d;
  return FL_OK;
}
static void m_destroy(fl_bdev *d) { (void)d; }
static const fl_bdev_ops k_mem_ops = {m_read, m_write, m_flush, m_destroy};

/* --- image de départ et géométrie ---------------------------------------------------------------- */

typedef struct {
  uint8_t *data;
  size_t size;
  int exfat;
  size_t fat_off, fat_len; /* première FAT */
  size_t root_off;         /* premier cluster (ou région) de la racine */
  size_t clsz;
  uint32_t rootclus;       /* FAT32 et exFAT */
  fl_content kind;
} image;

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void st32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static int charger(const char *fixture, image *im) {
  memset(im, 0, sizeof *im);
  FILE *f = fopen(tt_fixture(fixture), "rb");
  if (!f) return -1;
  fseek(f, 0, SEEK_END);
  im->size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  im->data = malloc(im->size);
  size_t n = im->data ? fread(im->data, 1, im->size, f) : 0;
  fclose(f);
  if (n != im->size) return -1;
  const uint8_t *b = im->data;
  im->exfat = !memcmp(b + 3, "EXFAT   ", 8);
  if (im->exfat) {
    size_t ss = (size_t)1 << b[108];
    im->clsz = ss << b[109];
    im->fat_off = le32(b + 80) * ss;
    im->fat_len = le32(b + 84) * ss;
    im->rootclus = le32(b + 96);
    im->root_off = le32(b + 88) * ss + (im->rootclus - 2) * im->clsz;
    im->kind = FL_CONTENT_EXFAT;
  } else {
    size_t bps = le16(b + 11), rsvd = le16(b + 14), nf = b[16], re = le16(b + 17);
    size_t fsz = le16(b + 22) ? le16(b + 22) : le32(b + 36);
    im->clsz = bps * b[13];
    im->fat_off = rsvd * bps;
    im->fat_len = fsz * bps;
    size_t root = (rsvd + nf * fsz) * bps, data = root + (re * 32 + bps - 1) / bps * bps;
    int f32 = le16(b + 22) == 0;
    im->rootclus = f32 ? le32(b + 44) : 0;
    im->root_off = f32 ? data + (im->rootclus - 2) * im->clsz : root;
    im->kind = f32 ? FL_CONTENT_FAT32 : FL_CONTENT_FAT16;
  }
  return 0;
}

/* --- parcours borné -------------------------------------------------------------------------------- */

typedef struct {
  fl_fs *fs;
  size_t vus, limite_fichier;
  int prof;
  uint8_t *buf;
  int codes_ko; /* code rendu qui n'est ni FL_OK ni un fl_err */
} parcours;

static int code_ok(int64_t rc) { return rc >= FL_EFBIG && rc <= 0; }

static void lire_fichier(parcours *pc, const char *p, uint64_t taille) {
  fl_file *f = NULL;
  int rc = fl_fs_open(pc->fs, p, FL_O_RDONLY, &f);
  if (!code_ok(rc)) pc->codes_ko++;
  if (rc != FL_OK) return;
  if (taille > pc->limite_fichier) taille = pc->limite_fichier; /* plus grand que l'image : impossible */
  for (uint64_t o = 0; o < taille;) {
    int64_t n = fl_file_pread(f, pc->buf, 1 << 20, o);
    if (n < 0 && !code_ok(n)) pc->codes_ko++;
    if (n <= 0) break;
    o += (uint64_t)n;
  }
  fl_file_close(f);
}

typedef struct {
  char (*noms)[256];
  fl_stat *st;
  size_t n, cap;
} lot;

static int collecter(void *ctx, const fl_dirent *e) {
  lot *l = ctx;
  if (l->n == l->cap) {
    size_t c = l->cap ? l->cap * 2 : 64;
    void *a = realloc(l->noms, c * sizeof *l->noms), *b = a ? realloc(l->st, c * sizeof *l->st) : NULL;
    if (!a || !b) {
      if (a) l->noms = a;
      return FL_ENOMEM;
    }
    l->noms = a;
    l->st = b;
    l->cap = c;
  }
  memcpy(l->noms[l->n], e->name, sizeof l->noms[0]);
  l->st[l->n++] = e->st;
  return 0;
}

static void parcourir(parcours *pc, const char *dir) {
  if (pc->prof > 64 || pc->vus > 100000) return;
  lot l = {0};
  int rc = fl_fs_readdir(pc->fs, dir, collecter, &l);
  if (!code_ok(rc)) pc->codes_ko++;
  pc->prof++;
  for (size_t i = 0; i < l.n && pc->vus <= 100000; i++) {
    pc->vus++;
    char p[4096];
    int k = snprintf(p, sizeof p, "%s/%s", strcmp(dir, "/") ? dir : "", l.noms[i]);
    if (k <= 0 || (size_t)k >= sizeof p - 256) continue;
    fl_stat st;
    rc = fl_fs_stat(pc->fs, p, &st);
    if (!code_ok(rc)) pc->codes_ko++;
    if (rc != FL_OK) continue;
    if (st.type == FL_FT_DIR) parcourir(pc, p);
    else lire_fichier(pc, p, st.size);
  }
  pc->prof--;
  free(l.noms);
  free(l.st);
}

/* Monte (lecture seule) et parcourt ; rend le nombre de codes hors fl_err. */
static int essayer(const image *im, uint8_t *buf) {
  mem m = {im->data};
  fl_bdev dev = {&k_mem_ops, 512, im->size / 512, true, &m};
  fl_probe_result pr;
  fl_content kind = fl_probe(&dev, &pr) == FL_OK && pr.content != FL_CONTENT_UNKNOWN ? pr.content : im->kind;
  fl_fs *fs = NULL;
  int rc = fl_fs_mount(&dev, kind, false, &fs);
  if (!code_ok(rc)) return 1;
  if (rc != FL_OK) return 0;
  parcours pc = {fs, 0, im->size, 0, buf, 0};
  fl_statfs st;
  rc = fl_fs_statfs(fs, &st);
  if (!code_ok(rc)) pc.codes_ko++;
  parcourir(&pc, "/");
  if (fl_fs_unmount(fs) != FL_OK) pc.codes_ko++;
  return pc.codes_ko;
}

static uint64_t g_rng = 0x5eed2026;
static uint32_t alea(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 7;
  g_rng ^= g_rng << 17;
  return (uint32_t)(g_rng >> 11);
}

/* 100 images dérivées de `fixture` : 1 à 8 octets aléatoires dans une région de métadonnées. */
static void aleatoires(const char *fixture) {
  image im;
  TT_ASSERT_EQ(charger(fixture, &im), 0);
  uint8_t *buf = malloc(1 << 20);
  int ko = 0;
  for (int k = 0; k < 100; k++) {
    size_t off[8];
    uint8_t avant[8];
    int n = 1 + (int)(alea() % 8);
    for (int i = 0; i < n; i++) {
      uint32_t zone = alea() % 3;
      size_t base = zone == 0 ? 0 : zone == 1 ? im.fat_off : im.root_off;
      size_t len = zone == 0 ? 512 : zone == 1 ? (im.fat_len < 65536 ? im.fat_len : 65536) : 4096;
      off[i] = base + alea() % len;
      avant[i] = im.data[off[i]];
      im.data[off[i]] = (uint8_t)alea();
    }
    if (getenv("FL_HOSTILE_TRACE")) fprintf(stderr, "%s, mutation %d (%d octets)\n", fixture, k, n);
    int r = essayer(&im, buf);
    if (r) fprintf(stderr, "%s, mutation %d : %d code(s) hors fl_err\n", fixture, k, r);
    ko += r;
    for (int i = n - 1; i >= 0; i--) im.data[off[i]] = avant[i];
  }
  free(buf);
  free(im.data);
  TT_ASSERT_EQ(ko, 0);
}

static void test_fat_hostile_aleatoire(void) {
  aleatoires("fat16-arbre.img");
  aleatoires("fat32-arbre.img");
  aleatoires("exfat-arbre.img");
}

/* Entrée FAT n (FAT16, FAT32 ou exFAT : 32 bits sauf FAT16). */
static void poser_fat(image *im, uint32_t n, uint32_t v) {
  if (im->kind == FL_CONTENT_FAT16) {
    im->data[im->fat_off + n * 2] = (uint8_t)v;
    im->data[im->fat_off + n * 2 + 1] = (uint8_t)(v >> 8);
  } else {
    st32(im->data + im->fat_off + (size_t)n * 4, v);
  }
}

/* Somme d'un ensemble d'entrées exFAT (octets 2 et 3 de la première entrée exclus). */
static uint16_t somme_ensemble(const uint8_t *e, size_t n) {
  uint16_t s = 0;
  for (size_t i = 0; i < n; i++)
    if (i != 2 && i != 3) s = (uint16_t)(((s & 1) ? 0x8000 : 0) + (s >> 1) + e[i]);
  return s;
}

/* Premier ensemble d'entrées « fichier » (0x85) de la racine exFAT, ou NULL. */
static uint8_t *premier_ensemble(image *im) {
  for (size_t o = 0; o + 32 <= im->clsz; o += 32) {
    uint8_t *e = im->data + im->root_off + o;
    if (e[0] == 0x85) return e;
    if (e[0] == 0) break;
  }
  return NULL;
}

static void test_fat_hostile_fabriques(void) {
  uint8_t *buf = malloc(1 << 20);
  int ko = 0;
  image a, b, c;
  TT_ASSERT_EQ(charger("fat32-arbre.img", &a), 0);
  TT_ASSERT_EQ(charger("exfat-arbre.img", &b), 0);
  TT_ASSERT_EQ(charger("fat16-arbre.img", &c), 0);
  image *const fats[] = {&a, &b};
  for (int i = 0; i < 2; i++) {
    image *im = fats[i];
    uint32_t ancien = le32(im->data + im->fat_off + (size_t)im->rootclus * 4);
    poser_fat(im, im->rootclus, im->rootclus); /* racine en boucle sur elle-même */
    ko += essayer(im, buf);
    poser_fat(im, im->rootclus, 0x0FFFFFF0u - 100); /* cluster hors bornes */
    ko += essayer(im, buf);
    poser_fat(im, im->rootclus, ancien);
  }
  /* FAT32 : chaîne de gros.bin bouclée sur son premier cluster (table de recherche rapide, correctif 0001) */
  for (size_t o = 0; o + 32 <= a.clsz; o += 32) {
    uint8_t *e = a.data + a.root_off + o;
    if (memcmp(e, "GROS    BIN", 11)) continue;
    uint32_t cl = (uint32_t)le16(e + 20) << 16 | le16(e + 26);
    uint32_t ancien = le32(a.data + a.fat_off + (size_t)cl * 4);
    poser_fat(&a, cl, cl);
    ko += essayer(&a, buf);
    poser_fat(&a, cl, ancien);
    break;
  }
  /* exFAT : décalage de cluster démesuré (FatFs ferait 1 << 98) */
  uint8_t sh = b.data[109];
  b.data[109] = 98;
  ko += essayer(&b, buf);
  b.data[109] = sh;
  /* BPB incohérent (FAT32) */
  uint8_t spc = a.data[13], nf = a.data[16];
  a.data[13] = 0;
  ko += essayer(&a, buf);
  a.data[13] = 3;
  ko += essayer(&a, buf);
  a.data[13] = spc;
  a.data[16] = 0;
  ko += essayer(&a, buf);
  a.data[16] = nf;
  /* FAT16 : chaîne de mille/ en boucle (un cluster de dossier après l'autre pointe sur le premier) */
  for (uint32_t n = 2; n < 400; n++) {
    uint16_t v = le16(c.data + c.fat_off + n * 2);
    if (v >= 2 && v < 0xFFF0) poser_fat(&c, v, n); /* boucle de deux clusters */
  }
  ko += essayer(&c, buf);
  /* exFAT : somme d'ensemble fausse, puis taille démesurée avec une somme juste */
  uint8_t *e = premier_ensemble(&b);
  TT_ASSERT(e != NULL);
  size_t n = (size_t)(1 + e[1]) * 32;
  e[2] ^= 0xFF;
  ko += essayer(&b, buf);
  e[2] ^= 0xFF;
  uint8_t *flux = e + 32;
  for (int k = 0; k < 8; k++) flux[24 + k] = flux[8 + k] = 0x7F;
  uint16_t s = somme_ensemble(e, n);
  e[2] = (uint8_t)s;
  e[3] = (uint8_t)(s >> 8);
  ko += essayer(&b, buf);
  free(a.data);
  free(b.data);
  free(c.data);
  free(buf);
  TT_ASSERT_EQ(ko, 0);
}

int main(void) {
  time_t t0 = time(NULL);
  TT_RUN(test_fat_hostile_aleatoire);
  TT_RUN(test_fat_hostile_fabriques);
  long dt = (long)(time(NULL) - t0);
  fprintf(stderr, "durée : %ld s\n", dt);
  if (dt > 240) {
    fprintf(stderr, "ECHEC : plus de 240 s\n");
    tt_failures++;
  }
  TT_MAIN_END();
}
