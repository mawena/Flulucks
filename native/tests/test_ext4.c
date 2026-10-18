#define _DEFAULT_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fluluks/luks.h"
#include "fluluks/partition.h"
#include "ext4.h"
#include "ext4_blockdev.h"
#include "ext4_crc32.h"
#include "ext4_dir.h"
#include "ext4_dir_idx.h"
#include "ext4_errno.h"
#include "ext4_fs.h"
#include "ext4_types.h"
#include "tt.h"

#define CLAIRSEME_TAILLE 5368709120ull

static fl_bdev *ouvrir(const char *nom) {
  fl_bdev *d = NULL;
  if (fl_file_bdev_open(tt_fixture(nom), 512, true, &d) != FL_OK) return NULL;
  return d;
}

static uint8_t motif(uint64_t i) { return (uint8_t)((i * 31 + 7) & 0xFF); }

static bool suit_motif(const uint8_t *buf, size_t n, uint64_t off) {
  for (size_t i = 0; i < n; i++)
    if (buf[i] != motif(off + i)) return false;
  return true;
}

static bool tout_zero(const uint8_t *buf, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (buf[i]) return false;
  return true;
}

/* Monte ext4-plain.img ; *dev à fermer après démontage. */
static fl_fs *monter_plain(fl_bdev **dev) {
  *dev = ouvrir("ext4-plain.img");
  if (!*dev) return NULL;
  fl_fs *fs = NULL;
  if (fl_fs_mount(*dev, FL_CONTENT_EXT4, false, &fs) != FL_OK) {
    fl_bdev_close(*dev);
    *dev = NULL;
    return NULL;
  }
  return fs;
}

/* LUKS2 déverrouillé : tout ce qu'il faut fermer, dans l'ordre. */
typedef struct {
  fl_bdev *img, *part, *dec;
  fl_luks *l;
} luks_vol;

static size_t lire_pass(char *buf, size_t cap) {
  FILE *f = fopen(tt_fixture("pass.txt"), "rb");
  if (!f) return 0;
  size_t n = fread(buf, 1, cap, f);
  fclose(f);
  return n;
}

/* gpt = true : gpt-luks2.img p1 ; sinon whole-luks2-4k.img entier. */
static int ouvrir_luks(luks_vol *v, bool gpt) {
  memset(v, 0, sizeof *v);
  v->img = ouvrir(gpt ? "gpt-luks2.img" : "whole-luks2-4k.img");
  if (!v->img) return FL_EIO;
  fl_bdev *base = v->img;
  if (gpt) {
    fl_part_table t;
    if (fl_part_scan(v->img, &t) != FL_OK || t.count < 1) return FL_EIO;
    if (fl_slice_bdev_new(v->img, t.parts[0].first_lba, t.parts[0].sector_count, &v->part) != FL_OK)
      return FL_EIO;
    base = v->part;
  }
  char pass[256];
  size_t n = lire_pass(pass, sizeof pass);
  if (!n) return FL_EIO;
  int rc = fl_luks_open(base, &v->l);
  if (rc != FL_OK) return rc;
  return fl_luks_unlock(v->l, pass, n, &v->dec);
}

static void fermer_luks(luks_vol *v) {
  fl_bdev_close(v->dec);
  fl_luks_close(v->l);
  fl_bdev_close(v->part);
  fl_bdev_close(v->img);
}

/* ---------- collecte de readdir ---------- */

typedef struct {
  int n;
  char noms[16][256];
  fl_dirent e[16];
} collecte;

static int cb_collecte(void *ctx, const fl_dirent *e) {
  collecte *c = ctx;
  if (c->n < 16) {
    snprintf(c->noms[c->n], sizeof c->noms[0], "%s", e->name);
    c->e[c->n] = *e;
  }
  c->n++;
  return 0;
}

static int trouver(const collecte *c, const char *nom) {
  for (int i = 0; i < c->n && i < 16; i++)
    if (strcmp(c->noms[i], nom) == 0) return i;
  return -1;
}

static void test_ext4_racine(void) {
  fl_bdev *d;
  fl_fs *fs = monter_plain(&d);
  TT_ASSERT(fs != NULL);
  collecte c = {0};
  TT_ASSERT_EQ(fl_fs_readdir(fs, "/", cb_collecte, &c), FL_OK);
  const char *attendus[] = {"hello.txt", "vide.txt", "accentué é ☀.txt", "sous-dossier",
                            "gros.bin", "beaucoup", "clairsemé.bin", "lost+found"};
  TT_ASSERT_EQ(c.n, 8);
  for (int i = 0; i < 8; i++) TT_ASSERT(trouver(&c, attendus[i]) >= 0);
  int h = trouver(&c, "hello.txt");
  TT_ASSERT_EQ(c.e[h].st.type, FL_FT_FILE);
  TT_ASSERT_EQ(c.e[h].st.size, 16);
  TT_ASSERT_EQ(c.e[trouver(&c, "sous-dossier")].st.type, FL_FT_DIR);
  TT_ASSERT_EQ(c.e[trouver(&c, "clairsemé.bin")].st.size, CLAIRSEME_TAILLE);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
}

static void test_ext4_stat(void) {
  fl_bdev *d;
  fl_fs *fs = monter_plain(&d);
  TT_ASSERT(fs != NULL);
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_stat(fs, "/hello.txt", &st), FL_OK);
  TT_ASSERT_EQ(st.type, FL_FT_FILE);
  TT_ASSERT_EQ(st.size, 16);
  TT_ASSERT_EQ(st.mode & 0170000, 0100000);
  TT_ASSERT(st.mtime > 1700000000);
  TT_ASSERT_EQ(fl_fs_stat(fs, "/sous-dossier", &st), FL_OK);
  TT_ASSERT_EQ(st.type, FL_FT_DIR);
  TT_ASSERT_EQ(st.mode & 0170000, 0040000);
  TT_ASSERT_EQ(fl_fs_stat(fs, "/", &st), FL_OK);
  TT_ASSERT_EQ(st.type, FL_FT_DIR);
  TT_ASSERT_EQ(fl_fs_stat(fs, "/sous-dossier/données.bin", &st), FL_OK);
  TT_ASSERT_EQ(st.size, 1u << 20);
  TT_ASSERT_EQ(fl_fs_stat(fs, "/absent", &st), FL_ENOENT);
  TT_ASSERT_EQ(fl_fs_stat(fs, "/sous-dossier/absent", &st), FL_ENOENT);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
}

static void test_ext4_lecture(void) {
  fl_bdev *d;
  fl_fs *fs = monter_plain(&d);
  TT_ASSERT(fs != NULL);
  fl_file *f = NULL;
  char buf[64];
  TT_ASSERT_EQ(fl_fs_open(fs, "/hello.txt", FL_O_RDONLY, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pread(f, buf, sizeof buf, 0), 16);
  TT_ASSERT_MEM_EQ(buf, "Bonjour FluLuks\n", 16);
  TT_ASSERT_EQ(fl_file_pread(f, buf, sizeof buf, 16), 0);
  TT_ASSERT_EQ(fl_file_pread(f, buf, sizeof buf, 1000), 0);
  TT_ASSERT_EQ(fl_file_pread(f, buf, 0, 0), 0);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);

  TT_ASSERT_EQ(fl_fs_open(fs, "/vide.txt", FL_O_RDONLY, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pread(f, buf, sizeof buf, 0), 0);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);

  TT_ASSERT_EQ(fl_fs_open(fs, "/accentué é ☀.txt", FL_O_RDONLY, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pread(f, buf, sizeof buf, 0), 5);
  TT_ASSERT_MEM_EQ(buf, "utf8\n", 5);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);

  uint8_t *gros = malloc(1u << 20);
  TT_ASSERT(gros != NULL);
  TT_ASSERT_EQ(fl_fs_open(fs, "/sous-dossier/données.bin", FL_O_RDONLY, &f), FL_OK);
  int64_t n = fl_file_pread(f, gros, 1u << 20, 0);
  bool ok = n == (1 << 20) && suit_motif(gros, 1u << 20, 0);
  free(gros);
  TT_ASSERT(ok);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_EQ(fl_file_close(NULL), FL_OK);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
}

static void verifier_motif_gros(fl_fs *fs) {
  fl_file *f = NULL;
  uint8_t buf[4093];
  TT_ASSERT_EQ(fl_fs_open(fs, "/gros.bin", FL_O_RDONLY, &f), FL_OK);
  const uint64_t offs[] = {0, 4095, 1048577, 5242879};
  for (int i = 0; i < 4; i++) {
    int64_t n = fl_file_pread(f, buf, sizeof buf, offs[i]);
    int64_t attendu = i == 3 ? 1 : (int64_t)sizeof buf;
    if (n != attendu || !suit_motif(buf, (size_t)n, offs[i])) {
      fprintf(stderr, "decalage %llu : %lld octets\n", (unsigned long long)offs[i], (long long)n);
      fl_file_close(f);
      TT_ASSERT(0);
    }
  }
  TT_ASSERT_EQ(fl_file_pread(f, buf, sizeof buf, 5242880), 0);
  TT_ASSERT_EQ(fl_file_pread(f, buf, sizeof buf, 1ull << 40), 0);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
}

static void test_ext4_motif_decalages(void) {
  fl_bdev *d;
  fl_fs *fs = monter_plain(&d);
  TT_ASSERT(fs != NULL);
  verifier_motif_gros(fs);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
}

/* Lecture complète de gros.bin par morceaux de taille impaire (blocs contigus et bords). */
static void test_ext4_lecture_complete(void) {
  fl_bdev *d;
  fl_fs *fs = monter_plain(&d);
  TT_ASSERT(fs != NULL);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(fs, "/gros.bin", FL_O_RDONLY, &f), FL_OK);
  size_t cap = 300001;
  uint8_t *buf = malloc(cap);
  TT_ASSERT(buf != NULL);
  uint64_t off = 0;
  bool ok = true;
  for (;;) {
    int64_t n = fl_file_pread(f, buf, cap, off);
    if (n < 0) { ok = false; break; }
    if (n == 0) break;
    if (!suit_motif(buf, (size_t)n, off)) { ok = false; break; }
    off += (uint64_t)n;
  }
  free(buf);
  fl_file_close(f);
  TT_ASSERT(ok);
  TT_ASSERT_EQ(off, 5u << 20);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
}

typedef struct {
  int n, fichiers;
  uint8_t vus[300];
  bool intrus;
} compte;

static int cb_compte(void *ctx, const fl_dirent *e) {
  compte *c = ctx;
  c->n++;
  unsigned k;
  char fin;
  if (sscanf(e->name, "f%3u.txt%c", &k, &fin) == 1 && k < 300 && strlen(e->name) == 8)
    c->vus[k]++;
  else
    c->intrus = true;
  if (e->st.type == FL_FT_FILE && e->st.size == 9) c->fichiers++;
  return 0;
}

static void test_ext4_beaucoup(void) {
  fl_bdev *d;
  fl_fs *fs = monter_plain(&d);
  TT_ASSERT(fs != NULL);
  compte c = {0};
  TT_ASSERT_EQ(fl_fs_readdir(fs, "/beaucoup", cb_compte, &c), FL_OK);
  TT_ASSERT_EQ(c.n, 300);
  TT_ASSERT_EQ(c.fichiers, 300);
  TT_ASSERT(!c.intrus);
  for (int i = 0; i < 300; i++) TT_ASSERT_EQ(c.vus[i], 1);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
}

static void test_ext4_lecture_au_dela_de_4gio(void) {
  fl_bdev *d;
  fl_fs *fs = monter_plain(&d);
  TT_ASSERT(fs != NULL);
  fl_stat st;
  TT_ASSERT_EQ(fl_fs_stat(fs, "/clairsemé.bin", &st), FL_OK);
  TT_ASSERT_EQ(st.size, CLAIRSEME_TAILLE);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(fs, "/clairsemé.bin", FL_O_RDONLY, &f), FL_OK);
  char fin[16];
  TT_ASSERT_EQ(fl_file_pread(f, fin, 11, 5368709109ull), 11);
  TT_ASSERT_MEM_EQ(fin, "FIN-FLULUKS", 11);
  /* lecture courte en fin, avec la fin du trou devant */
  TT_ASSERT_EQ(fl_file_pread(f, fin, 16, CLAIRSEME_TAILLE - 16), 16);
  TT_ASSERT_MEM_EQ(fin, "\0\0\0\0\0FIN-FLULUKS", 16);
  TT_ASSERT_EQ(fl_file_pread(f, fin, 16, CLAIRSEME_TAILLE - 11), 11);
  TT_ASSERT_EQ(fl_file_pread(f, fin, 16, CLAIRSEME_TAILLE), 0);
  uint8_t z[11];
  memset(z, 0xAA, sizeof z);
  TT_ASSERT_EQ(fl_file_pread(f, z, 11, 4294967296ull), 11);
  TT_ASSERT(tout_zero(z, 11));
  /* trou : blocs entiers et fin partielle de bloc, jamais le contenu du bloc 0 */
  size_t cap = 3 * 4096 + 1500;
  uint8_t *buf = malloc(cap);
  TT_ASSERT(buf != NULL);
  memset(buf, 0xAA, cap);
  int64_t n = fl_file_pread(f, buf, cap, 4294967296ull);
  bool zero = tout_zero(buf, cap);
  memset(buf, 0xAA, cap);
  int64_t n2 = fl_file_pread(f, buf, cap, 4294967296ull + 1000);
  bool zero2 = tout_zero(buf, cap);
  free(buf);
  TT_ASSERT_EQ(n, (int64_t)cap);
  TT_ASSERT(zero);
  TT_ASSERT_EQ(n2, (int64_t)cap);
  TT_ASSERT(zero2);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
}

static void test_ext4_lecture_seule(void) {
  fl_bdev *d = ouvrir("ext4-plain.img");
  TT_ASSERT(d != NULL);
  fl_fs *fs = NULL;
  TT_ASSERT_EQ(fl_fs_mount(d, FL_CONTENT_EXT4, true, &fs), FL_OK); /* want_rw ignoré */
  fl_file *f = (fl_file *)0x1;
  TT_ASSERT_EQ(fl_fs_open(fs, "/hello.txt", FL_O_RDWR, &f), FL_EROFS);
  TT_ASSERT(f == NULL);
  TT_ASSERT_EQ(fl_fs_open(fs, "/hello.txt", FL_O_TRUNC, &f), FL_EROFS);
  TT_ASSERT_EQ(fl_fs_open(fs, "/nouveau.txt", FL_O_CREAT, &f), FL_EROFS);
  TT_ASSERT_EQ(fl_fs_mkdir(fs, "/nouveau"), FL_EROFS);
  TT_ASSERT_EQ(fl_fs_unlink(fs, "/hello.txt"), FL_EROFS);
  TT_ASSERT_EQ(fl_fs_rmdir(fs, "/sous-dossier"), FL_EROFS);
  TT_ASSERT_EQ(fl_fs_rename(fs, "/hello.txt", "/h.txt"), FL_EROFS);
  TT_ASSERT_EQ(fl_fs_sync(fs), FL_OK);
  TT_ASSERT_EQ(fl_fs_open(fs, "/hello.txt", FL_O_RDONLY, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pwrite(f, "x", 1, 0), FL_EROFS);
  TT_ASSERT_EQ(fl_file_truncate(f, 0), FL_EROFS);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  fl_statfs s;
  memset(&s, 0x55, sizeof s);
  TT_ASSERT_EQ(fl_fs_statfs(fs, &s), FL_OK);
  TT_ASSERT(s.read_only);
  TT_ASSERT_STR_EQ(s.label, "FLUTEST");
  TT_ASSERT_STR_EQ(s.ro_reason, "écriture pas encore disponible");
  TT_ASSERT(!s.needs_recovery);
  TT_ASSERT_EQ(s.total_bytes, 64u << 20);
  TT_ASSERT(s.free_bytes > 0 && s.free_bytes < s.total_bytes);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
}

static void lire_hello(fl_fs *fs) {
  fl_file *f = NULL;
  char buf[32];
  TT_ASSERT_EQ(fl_fs_open(fs, "/hello.txt", FL_O_RDONLY, &f), FL_OK);
  int64_t n = fl_file_pread(f, buf, sizeof buf, 0);
  fl_file_close(f);
  TT_ASSERT_EQ(n, 16);
  TT_ASSERT_MEM_EQ(buf, "Bonjour FluLuks\n", 16);
}

static void test_ext4_sur_luks2(void) {
  luks_vol v;
  int rc = ouvrir_luks(&v, true);
  if (rc != FL_OK) fermer_luks(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  fl_fs *fs = NULL;
  rc = fl_fs_mount(v.dec, FL_CONTENT_EXT4, false, &fs);
  if (rc == FL_OK) {
    lire_hello(fs);
    rc = fl_fs_unmount(fs);
  }
  fermer_luks(&v);
  TT_ASSERT_EQ(rc, FL_OK);
}

/* Périphérique déchiffré à secteurs de 4096 octets. */
static void test_ext4_sur_luks2_4k(void) {
  luks_vol v;
  int rc = ouvrir_luks(&v, false);
  if (rc != FL_OK) fermer_luks(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  fl_fs *fs = NULL;
  uint32_t ss = v.dec->sector_size;
  rc = fl_fs_mount(v.dec, FL_CONTENT_EXT4, false, &fs);
  if (rc == FL_OK) {
    lire_hello(fs);
    verifier_motif_gros(fs);
    rc = fl_fs_unmount(fs);
  }
  fermer_luks(&v);
  TT_ASSERT_EQ(ss, 4096);
  TT_ASSERT_EQ(rc, FL_OK);
}

static void test_ext4_deux_montages(void) {
  luks_vol v;
  int rc = ouvrir_luks(&v, true);
  if (rc != FL_OK) fermer_luks(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  fl_bdev *d;
  fl_fs *a = monter_plain(&d);
  fl_fs *b = NULL;
  rc = fl_fs_mount(v.dec, FL_CONTENT_EXT4, false, &b);
  bool ok = a && rc == FL_OK;
  if (ok) {
    fl_file *fa = NULL, *fb = NULL;
    ok = fl_fs_open(a, "/gros.bin", 0, &fa) == FL_OK && fl_fs_open(b, "/gros.bin", 0, &fb) == FL_OK;
    uint8_t x[777], y[777];
    for (uint64_t off = 0; ok && off < (5u << 20); off += 65537) {
      int64_t na = fl_file_pread(fa, x, sizeof x, off), nb = fl_file_pread(fb, y, sizeof y, off);
      ok = na > 0 && na == nb && memcmp(x, y, (size_t)na) == 0 && suit_motif(x, (size_t)na, off);
    }
    fl_file_close(fa);
    fl_file_close(fb);
    collecte ca = {0}, cb = {0};
    ok = ok && fl_fs_readdir(a, "/", cb_collecte, &ca) == FL_OK &&
         fl_fs_readdir(b, "/", cb_collecte, &cb) == FL_OK && ca.n == 8 && cb.n == 8;
  }
  int ua = fl_fs_unmount(a), ub = fl_fs_unmount(b);
  fl_bdev_close(d);
  fermer_luks(&v);
  TT_ASSERT(ok);
  TT_ASSERT_EQ(ua, FL_OK);
  TT_ASSERT_EQ(ub, FL_OK);
}

/* Aucun emplacement de lwext4 (8 périphériques, 8 points de montage) ne fuit. */
static void test_ext4_montages_repetes(void) {
  fl_bdev *d = ouvrir("ext4-plain.img");
  TT_ASSERT(d != NULL);
  for (int i = 0; i < 20; i++) {
    fl_fs *fs = NULL;
    int rc = fl_fs_mount(d, FL_CONTENT_EXT4, false, &fs);
    if (rc == FL_OK) {
      lire_hello(fs);
      rc = fl_fs_unmount(fs);
    }
    if (rc != FL_OK) fl_bdev_close(d);
    TT_ASSERT_EQ(rc, FL_OK);
  }
  /* 8 montages simultanés au plus, le 9e échoue proprement */
  fl_fs *fs[9] = {0};
  int ok = 0;
  for (int i = 0; i < 8; i++) ok += fl_fs_mount(d, FL_CONTENT_EXT4, false, &fs[i]) == FL_OK;
  int rc9 = fl_fs_mount(d, FL_CONTENT_EXT4, false, &fs[8]);
  bool null9 = fs[8] == NULL;
  for (int i = 0; i < 9; i++) fl_fs_unmount(fs[i]);
  fl_fs *g = NULL;
  int rc = fl_fs_mount(d, FL_CONTENT_EXT4, false, &g);
  if (rc == FL_OK) lire_hello(g);
  fl_fs_unmount(g);
  fl_bdev_close(d);
  TT_ASSERT_EQ(ok, 8);
  TT_ASSERT(rc9 != FL_OK);
  TT_ASSERT(null9);
  TT_ASSERT_EQ(rc, FL_OK);
}

static void test_ext4_chemins(void) {
  fl_bdev *d;
  fl_fs *fs = monter_plain(&d);
  TT_ASSERT(fs != NULL);
  fl_stat st;
  char longue[4200];
  memset(longue, 'a', sizeof longue);
  longue[0] = '/';
  longue[4096] = 0; /* 4096 octets */
  char comp[300];
  memset(comp, 'b', sizeof comp);
  comp[0] = '/';
  comp[257] = 0; /* composant de 256 octets */
  const char *mauvais[] = {"", "hello.txt", "//hello.txt", "/hello.txt/", "/sous-dossier//données.bin",
                           "/./hello.txt", "/sous-dossier/../hello.txt", "/..", longue, comp};
  for (size_t i = 0; i < sizeof mauvais / sizeof mauvais[0]; i++) {
    if (fl_fs_stat(fs, mauvais[i], &st) != FL_EINVAL) {
      fprintf(stderr, "chemin accepte : \"%.40s\"\n", mauvais[i]);
      TT_ASSERT(0);
    }
  }
  TT_ASSERT_EQ(fl_fs_stat(fs, NULL, &st), FL_EINVAL);
  TT_ASSERT_EQ(fl_fs_stat(NULL, "/", &st), FL_EINVAL);
  TT_ASSERT_EQ(fl_fs_stat(fs, "/", NULL), FL_EINVAL);
  comp[256] = 0; /* 255 octets : valide mais absent */
  TT_ASSERT_EQ(fl_fs_stat(fs, comp, &st), FL_ENOENT);
  /* 4095 octets en composants de 199 octets : valide mais absent ; 4096 : refusé */
  for (int i = 0; i < 4096; i++) longue[i] = i % 200 == 0 ? '/' : 'a';
  longue[4095] = 0;
  TT_ASSERT_EQ(fl_fs_stat(fs, longue, &st), FL_ENOENT);
  longue[4095] = 'a';
  longue[4096] = 0;
  TT_ASSERT_EQ(fl_fs_stat(fs, longue, &st), FL_EINVAL);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
}

static void test_ext4_erreurs_types(void) {
  fl_bdev *d;
  fl_fs *fs = monter_plain(&d);
  TT_ASSERT(fs != NULL);
  fl_file *f = NULL;
  collecte c = {0};
  TT_ASSERT_EQ(fl_fs_open(fs, "/sous-dossier", FL_O_RDONLY, &f), FL_EISDIR);
  TT_ASSERT_EQ(fl_fs_open(fs, "/", FL_O_RDONLY, &f), FL_EISDIR);
  TT_ASSERT_EQ(fl_fs_open(fs, "/absent", FL_O_RDONLY, &f), FL_ENOENT);
  TT_ASSERT_EQ(fl_fs_open(fs, "/hello.txt", 8, &f), FL_EINVAL);
  TT_ASSERT_EQ(fl_fs_open(fs, "/hello.txt", FL_O_RDONLY, NULL), FL_EINVAL);
  TT_ASSERT_EQ(fl_fs_readdir(fs, "/hello.txt", cb_collecte, &c), FL_ENOTDIR);
  TT_ASSERT_EQ(fl_fs_readdir(fs, "/absent", cb_collecte, &c), FL_ENOENT);
  TT_ASSERT_EQ(fl_fs_readdir(fs, "/", NULL, &c), FL_EINVAL);
  TT_ASSERT_EQ(c.n, 0);
  TT_ASSERT_EQ(fl_fs_statfs(fs, NULL), FL_EINVAL);
  TT_ASSERT_EQ(fl_file_pread(NULL, &c, 1, 0), FL_EINVAL);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  TT_ASSERT_EQ(fl_fs_unmount(NULL), FL_OK);
  fl_fs *g = (fl_fs *)0x1;
  TT_ASSERT_EQ(fl_fs_mount(d, FL_CONTENT_FAT32, false, &g), FL_ENOTSUP);
  TT_ASSERT(g == NULL);
  TT_ASSERT_EQ(fl_fs_mount(d, FL_CONTENT_LUKS2, false, &g), FL_ENOTSUP);
  TT_ASSERT_EQ(fl_fs_mount(NULL, FL_CONTENT_EXT4, false, &g), FL_EINVAL);
  TT_ASSERT_EQ(fl_fs_mount(d, FL_CONTENT_EXT4, false, NULL), FL_EINVAL);
  fl_bdev_close(d);
}

typedef struct {
  int appels;
} arret;

static int cb_arret(void *ctx, const fl_dirent *e) {
  (void)e;
  arret *a = ctx;
  return ++a->appels == 3 ? 42 : 0;
}

/* Le callback peut rappeler l'API (pas de verrou tenu pendant l'appel). */
typedef struct {
  fl_fs *fs;
  int lus;
} reentrant;

static int cb_reentrant(void *ctx, const fl_dirent *e) {
  reentrant *r = ctx;
  char p[300];
  snprintf(p, sizeof p, "/beaucoup/%s", e->name);
  fl_file *f = NULL;
  if (fl_fs_open(r->fs, p, FL_O_RDONLY, &f) != FL_OK) return -100;
  char buf[16];
  int64_t n = fl_file_pread(f, buf, sizeof buf, 0);
  fl_file_close(f);
  if (n != 9 || memcmp(buf, e->name, 8) != 0) return -101;
  r->lus++;
  return 0;
}

static void test_ext4_readdir_callback(void) {
  fl_bdev *d;
  fl_fs *fs = monter_plain(&d);
  TT_ASSERT(fs != NULL);
  arret a = {0};
  TT_ASSERT_EQ(fl_fs_readdir(fs, "/beaucoup", cb_arret, &a), 42);
  TT_ASSERT_EQ(a.appels, 3);
  reentrant r = {fs, 0};
  TT_ASSERT_EQ(fl_fs_readdir(fs, "/beaucoup", cb_reentrant, &r), FL_OK);
  TT_ASSERT_EQ(r.lus, 300);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
}

static void test_ext4_pread_entrelaces(void) {
  fl_bdev *d;
  fl_fs *fs = monter_plain(&d);
  TT_ASSERT(fs != NULL);
  fl_file *f = NULL, *g = NULL;
  TT_ASSERT_EQ(fl_fs_open(fs, "/gros.bin", FL_O_RDONLY, &f), FL_OK);
  TT_ASSERT_EQ(fl_fs_open(fs, "/gros.bin", FL_O_RDONLY, &g), FL_OK);
  uint8_t a[100], b[100];
  bool ok = true;
  for (uint64_t i = 0; ok && i < 50; i++) {
    uint64_t oa = i * 104729, ob = (5u << 20) - 200 - i * 7919;
    ok = fl_file_pread(f, a, sizeof a, oa) == 100 && fl_file_pread(f, b, sizeof b, ob) == 100 &&
         suit_motif(a, 100, oa) && suit_motif(b, 100, ob) &&
         fl_file_pread(g, a, sizeof a, ob) == 100 && suit_motif(a, 100, ob);
  }
  /* démontage refusé tant qu'un fichier est ouvert */
  int busy = fl_fs_unmount(fs);
  fl_file_close(f);
  int busy2 = fl_fs_unmount(fs);
  fl_file_close(g);
  TT_ASSERT(ok);
  TT_ASSERT_EQ(busy, FL_EINVAL);
  TT_ASSERT_EQ(busy2, FL_EINVAL);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
}

typedef struct {
  fl_fs *fs;
  int graine;
  bool ok;
} fil_arg;

static void *fil_lecteur(void *p) {
  fil_arg *a = p;
  a->ok = true;
  fl_file *f = NULL;
  if (fl_fs_open(a->fs, "/gros.bin", FL_O_RDONLY, &f) != FL_OK) {
    a->ok = false;
    return NULL;
  }
  uint8_t buf[5000];
  uint64_t x = (uint64_t)a->graine * 2654435761u;
  for (int i = 0; i < 200 && a->ok; i++) {
    x = x * 6364136223846793005ull + 1442695040888963407ull;
    uint64_t off = (x >> 20) % (5u << 20);
    int64_t n = fl_file_pread(f, buf, sizeof buf, off);
    uint64_t attendu = (5u << 20) - off < sizeof buf ? (5u << 20) - off : sizeof buf;
    if (n != (int64_t)attendu || !suit_motif(buf, (size_t)n, off)) a->ok = false;
    collecte c = {0};
    if (i % 50 == 0 && (fl_fs_readdir(a->fs, "/", cb_collecte, &c) != FL_OK || c.n != 8)) a->ok = false;
  }
  fl_file_close(f);
  return NULL;
}

static void test_ext4_fils_concurrents(void) {
  fl_bdev *d1, *d2;
  fl_fs *a = monter_plain(&d1), *b = monter_plain(&d2);
  bool montes = a && b;
  pthread_t t[4];
  fil_arg args[4];
  int lances = 0;
  for (int i = 0; montes && i < 4; i++) {
    args[i] = (fil_arg){i % 2 ? a : b, i + 1, false};
    if (pthread_create(&t[i], NULL, fil_lecteur, &args[i]) == 0) lances++;
  }
  bool ok = montes && lances == 4;
  for (int i = 0; i < lances; i++) {
    pthread_join(t[i], NULL);
    ok = ok && args[i].ok;
  }
  fl_fs_unmount(a);
  fl_fs_unmount(b);
  fl_bdev_close(d1);
  fl_bdev_close(d2);
  TT_ASSERT(ok);
}

/* ---------- entrées hostiles : superbloc modifié par-dessus ext4-plain.img ---------- */

typedef struct {
  fl_bdev *parent;
  uint8_t sb[1024];      /* remplace les octets [1024, 2048) */
  const uint8_t *apres;  /* si non NULL : superbloc servi à partir de la 2e lecture */
  int lectures;
} recouvrement;

static int rec_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  recouvrement *r = d->priv;
  int rc = fl_bdev_read(r->parent, lba, count, buf);
  if (rc != FL_OK) return rc;
  uint64_t debut = lba * d->sector_size, fin = debut + (uint64_t)count * d->sector_size;
  if (debut >= 2048 || fin <= 1024) return FL_OK;
  const uint8_t *sb = r->apres && r->lectures++ > 0 ? r->apres : r->sb;
  for (uint64_t o = debut < 1024 ? 1024 : debut; o < fin && o < 2048; o++)
    ((uint8_t *)buf)[o - debut] = sb[o - 1024];
  return FL_OK;
}

static int rec_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf) {
  (void)d, (void)lba, (void)count, (void)buf;
  return FL_EROFS;
}

static void rec_destroy(fl_bdev *d) { (void)d; }

static const fl_bdev_ops rec_ops = {rec_read, rec_write, NULL, rec_destroy};

static void put32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void sb_csum(uint8_t *sb) { put32(sb + 0x3FC, ext4_crc32c(0xFFFFFFFFu, sb, 0x3FC)); }

typedef struct {
  const char *nom;
  uint32_t off, taille, valeur;
} alteration;

static void test_ext4_superbloc_hostile(void) {
  fl_bdev *d = ouvrir("ext4-plain.img");
  TT_ASSERT(d != NULL);
  recouvrement r = {.parent = d};
  if (fl_bdev_read_bytes(d, 1024, 1024, r.sb) != FL_OK) fl_bdev_close(d);
  TT_ASSERT(r.sb[0x38] == 0x53 && r.sb[0x39] == 0xEF);
  uint8_t orig[1024];
  memcpy(orig, r.sb, sizeof orig);
  sb_csum(r.sb);
  TT_ASSERT_MEM_EQ(r.sb, orig, sizeof orig); /* notre somme de contrôle est la bonne */
  fl_bdev ov = {&rec_ops, d->sector_size, d->sector_count, true, &r};
  const alteration cas[] = {
      {"magie", 0x38, 2, 0x1234},
      {"taille de bloc", 0x18, 4, 30},
      {"taille de bloc 64 Kio sur 64 Mio", 0x18, 4, 6},
      {"inodes par groupe nul", 0x28, 4, 0},
      {"inodes par groupe enorme", 0x28, 4, 0xFFFFFFF0u},
      {"blocs par groupe nul", 0x20, 4, 0},
      {"nombre d'inodes enorme", 0x00, 4, 0xFFFFFFFFu},
      {"taille d'inode 100", 0x58, 2, 100},
      {"taille d'inode 8192", 0x58, 2, 8192},
      {"taille d'inode 384", 0x58, 2, 384},
      {"blocs au-dela du disque", 0x04, 4, 1u << 30},
      {"premier bloc de donnees", 0x14, 4, 20000},
      {"taille de descripteur 48", 0xFE, 2, 48},
      {"inline_data", 0x60, 4, 0},  /* valeur calculée plus bas */
      {"chiffrement", 0x60, 4, 0},
  };
  int ncas = (int)(sizeof cas / sizeof cas[0]);
  for (int i = 0; i < ncas; i++) {
    memcpy(r.sb, orig, sizeof orig);
    uint32_t v = cas[i].valeur;
    if (i == ncas - 2) v = get32(orig + 0x60) | 0x8000;
    if (i == ncas - 1) v = get32(orig + 0x60) | 0x10000;
    for (uint32_t k = 0; k < cas[i].taille; k++) r.sb[cas[i].off + k] = (uint8_t)(v >> (8 * k));
    sb_csum(r.sb);
    fl_fs *fs = (fl_fs *)0x1;
    int rc = fl_fs_mount(&ov, FL_CONTENT_EXT4, false, &fs);
    if (rc == FL_OK || fs != NULL) {
      fprintf(stderr, "superbloc accepte : %s\n", cas[i].nom);
      fl_fs_unmount(rc == FL_OK ? fs : NULL);
      fl_bdev_close(d);
      TT_ASSERT(0);
    }
    if (i >= ncas - 2 && rc != FL_ENOTSUP) {
      fprintf(stderr, "%s : %d\n", cas[i].nom, rc);
      fl_bdev_close(d);
      TT_ASSERT(0);
    }
  }
  /* superbloc d'origine via le recouvrement : montable */
  memcpy(r.sb, orig, sizeof orig);
  fl_fs *fs = NULL;
  int rc = fl_fs_mount(&ov, FL_CONTENT_EXT4, false, &fs);
  if (rc == FL_OK) lire_hello(fs);
  fl_fs_unmount(fs);
  /* périphériques trop petits */
  fl_bdev *petit = NULL;
  int rcp = fl_slice_bdev_new(d, 0, 3, &petit);
  fl_fs *fp = (fl_fs *)0x1;
  int rcm = rcp == FL_OK ? fl_fs_mount(petit, FL_CONTENT_EXT4, false, &fp) : FL_OK;
  fl_bdev_close(petit);
  fl_bdev *moitie = NULL;
  fl_fs *fm = (fl_fs *)0x1;
  int rcm2 = fl_slice_bdev_new(d, 0, d->sector_count / 2, &moitie) == FL_OK
                 ? fl_fs_mount(moitie, FL_CONTENT_EXT4, false, &fm) : FL_OK;
  fl_bdev_close(moitie);
  fl_bdev_close(d);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT(rcm != FL_OK && fp == NULL);
  TT_ASSERT_EQ(rcm2, FL_ECORRUPT);
  TT_ASSERT(fm == NULL);
}

static void test_ext4_needs_recovery(void) {
  fl_bdev *d = ouvrir("ext4-plain.img");
  TT_ASSERT(d != NULL);
  recouvrement r = {.parent = d};
  if (fl_bdev_read_bytes(d, 1024, 1024, r.sb) != FL_OK) fl_bdev_close(d);
  put32(r.sb + 0x60, get32(r.sb + 0x60) | 0x4); /* RECOVER */
  sb_csum(r.sb);
  fl_bdev ov = {&rec_ops, d->sector_size, d->sector_count, true, &r};
  fl_fs *fs = NULL;
  int rc = fl_fs_mount(&ov, FL_CONTENT_EXT4, false, &fs);
  fl_statfs s = {0};
  int rs = rc == FL_OK ? fl_fs_statfs(fs, &s) : rc;
  if (rc == FL_OK) lire_hello(fs);
  fl_fs_unmount(fs);
  fl_bdev_close(d);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(rs, FL_OK);
  TT_ASSERT(s.needs_recovery);
  TT_ASSERT(s.read_only);
}

/* Périphérique malveillant : superbloc valide à la validation, hostile ensuite. */
static void test_ext4_superbloc_change(void) {
  fl_bdev *d = ouvrir("ext4-plain.img");
  TT_ASSERT(d != NULL);
  recouvrement r = {.parent = d};
  uint8_t mauvais[1024];
  if (fl_bdev_read_bytes(d, 1024, 1024, r.sb) != FL_OK) fl_bdev_close(d);
  memcpy(mauvais, r.sb, sizeof mauvais);
  put32(mauvais + 0x18, 30); /* taille de bloc absurde */
  put32(mauvais + 0x28, 0);  /* inodes par groupe nul */
  sb_csum(mauvais);
  r.apres = mauvais;
  fl_bdev ov = {&rec_ops, d->sector_size, d->sector_count, true, &r};
  fl_fs *fs = NULL;
  int rc = fl_fs_mount(&ov, FL_CONTENT_EXT4, false, &fs);
  if (rc == FL_OK) lire_hello(fs);
  fl_fs_unmount(fs);
  int lectures = r.lectures;
  fl_bdev_close(d);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT(lectures >= 2); /* lwext4 a bien relu le superbloc */
}

/* ---------- dettes de lecture de lwext4 (correctif 0002) ---------- */

/* 255 octets : « é » × 100 + « n » × 51 + « .txt » (voir make_fixtures.sh). */
static void nom_255(char *buf) {
  char *p = buf;
  for (int i = 0; i < 100; i++) *p++ = (char)0xC3, *p++ = (char)0xA9;
  memset(p, 'n', 51);
  memcpy(p + 51, ".txt", 5);
}

static void test_ext4_nom_255(void) {
  char n255[256], d255[256], chemin[600];
  nom_255(n255);
  TT_ASSERT_EQ(strlen(n255), 255);
  memset(d255, 'd', 255);
  d255[255] = 0;
  fl_bdev *d = ouvrir("ext4-nom255.img");
  TT_ASSERT(d != NULL);
  fl_fs *fs = NULL;
  if (fl_fs_mount(d, FL_CONTENT_EXT4, false, &fs) != FL_OK) fl_bdev_close(d);
  TT_ASSERT(fs != NULL);
  fl_stat st;
  snprintf(chemin, sizeof chemin, "/%s", n255);
  int rs = fl_fs_stat(fs, chemin, &st);
  fl_file *f = NULL;
  char buf[64] = {0};
  int ro = fl_fs_open(fs, chemin, FL_O_RDONLY, &f);
  int64_t n = ro == FL_OK ? fl_file_pread(f, buf, sizeof buf, 0) : -1;
  fl_file_close(f);
  /* composant intermédiaire de 255 octets */
  snprintf(chemin, sizeof chemin, "/%s/f.txt", d255);
  fl_stat st2;
  int rs2 = fl_fs_stat(fs, chemin, &st2);
  f = NULL;
  char buf2[16] = {0};
  int ro2 = fl_fs_open(fs, chemin, FL_O_RDONLY, &f);
  int64_t n2 = ro2 == FL_OK ? fl_file_pread(f, buf2, sizeof buf2, 0) : -1;
  fl_file_close(f);
  collecte c = {0};
  int rd = fl_fs_readdir(fs, "/", cb_collecte, &c);
  /* 256 octets : toujours refusé par fs.c */
  char n256[300];
  snprintf(n256, sizeof n256, "/%sx", n255);
  int r256 = fl_fs_stat(fs, n256, &st2);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
  TT_ASSERT_EQ(rs, FL_OK);
  TT_ASSERT_EQ(st.type, FL_FT_FILE);
  TT_ASSERT_EQ(st.size, 18);
  TT_ASSERT_EQ(ro, FL_OK);
  TT_ASSERT_EQ(n, 18);
  TT_ASSERT_MEM_EQ(buf, "nom de 255 octets\n", 18);
  TT_ASSERT_EQ(rs2, FL_OK);
  TT_ASSERT_EQ(st2.size, 8);
  TT_ASSERT_EQ(ro2, FL_OK);
  TT_ASSERT_EQ(n2, 8);
  TT_ASSERT_MEM_EQ(buf2, "profond\n", 8);
  TT_ASSERT_EQ(rd, FL_OK);
  TT_ASSERT_EQ(c.n, 3); /* N255, D255, lost+found */
  TT_ASSERT(trouver(&c, n255) >= 0);
  TT_ASSERT(trouver(&c, d255) >= 0);
  TT_ASSERT_EQ(c.e[trouver(&c, n255)].st.size, 18);
  TT_ASSERT_EQ(c.e[trouver(&c, d255)].st.type, FL_FT_DIR);
  TT_ASSERT_EQ(r256, FL_EINVAL);
}

/* Périphérique qui compte les lectures touchant les octets [0, 4096) (bloc 0 du système) et fait
 * échouer celles qui touchent le bloc ext4 de 4 Kio `panne` (0 : aucun). */
typedef struct {
  fl_bdev *parent;
  int bloc0;
  uint64_t panne;
} compteur;

static int cpt_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  compteur *c = d->priv;
  uint64_t debut = lba * d->sector_size, fin = debut + (uint64_t)count * d->sector_size;
  if (count && debut < 4096) c->bloc0++;
  if (c->panne && debut < (c->panne + 1) * 4096 && fin > c->panne * 4096) return FL_EIO;
  return fl_bdev_read(c->parent, lba, count, buf);
}

static const fl_bdev_ops cpt_ops = {cpt_read, rec_write, NULL, rec_destroy};

static void nom_trou(char *buf, size_t cap, const char *dossier, int i) {
  snprintf(buf, cap, "/%s/fichier-%03d-abcdefghijklmnopqrstuvwxyz", dossier, i);
}

/* /d (linéaire) et /h (htree) ont chacun un bloc logique sans bloc physique. */
static void test_ext4_trou_de_dossier(void) {
  fl_bdev *d = ouvrir("ext4-trou-dossier.img");
  TT_ASSERT(d != NULL);
  compteur c = {d, 0, 0};
  fl_bdev cd = {&cpt_ops, d->sector_size, d->sector_count, true, &c};
  fl_fs *fs = NULL;
  if (fl_fs_mount(&cd, FL_CONTENT_EXT4, false, &fs) != FL_OK) fl_bdev_close(d);
  TT_ASSERT(fs != NULL);
  c.bloc0 = 0; /* le montage lit le superbloc ; plus rien ensuite */
  fl_stat st;
  collecte cl = {0};
  int rd = fl_fs_readdir(fs, "/d", cb_collecte, &cl);
  int rs = fl_fs_stat(fs, "/d/x", &st);
  char p[128];
  nom_trou(p, sizeof p, "d", 1); /* dans le bloc 0 de /d, avant le trou */
  int rs1 = fl_fs_stat(fs, p, &st);
  int rsd = fl_fs_stat(fs, "/d", &st);
  collecte ch = {0};
  int rh = fl_fs_readdir(fs, "/h", cb_collecte, &ch);
  /* recherche par hachage : les noms de la feuille trouée → FL_ECORRUPT, les autres → FL_OK */
  int ok = 0, corrompus = 0, autres = 0;
  for (int i = 1; i <= 300; i++) {
    nom_trou(p, sizeof p, "h", i);
    int r = fl_fs_stat(fs, p, &st);
    if (r == FL_OK) ok++;
    else if (r == FL_ECORRUPT) corrompus++;
    else autres++;
  }
  int rsh = fl_fs_stat(fs, "/h/x", &st);
  int bloc0 = c.bloc0;
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
  TT_ASSERT_EQ(rd, FL_ECORRUPT);
  TT_ASSERT_EQ(rs, FL_ECORRUPT);
  TT_ASSERT_EQ(rs1, FL_OK);
  TT_ASSERT_EQ(rsd, FL_OK);
  TT_ASSERT_EQ(rh, FL_ECORRUPT);
  TT_ASSERT(corrompus > 0);
  TT_ASSERT(ok > 0);
  TT_ASSERT_EQ(autres, 0);
  TT_ASSERT(rsh == FL_ENOENT || rsh == FL_ECORRUPT);
  TT_ASSERT_EQ(bloc0, 0);
}

/* Blocs physiques notés par make_fixtures.sh dans ext4-trou-dossier.blocs. */
typedef struct {
  uint64_t h_feuille, h_etb, d_bloc0;
} blocs_trou;

static bool lire_blocs(blocs_trou *b) {
  FILE *f = fopen(tt_fixture("ext4-trou-dossier.blocs"), "r");
  if (!f) return false;
  unsigned long long x, y, z;
  int n = fscanf(f, "h_feuille %llu h_etb %llu d_bloc0 %llu", &x, &y, &z);
  fclose(f);
  *b = (blocs_trou){x, y, z};
  return n == 3 && x && y && z;
}

typedef struct {
  int chauffe;  /* stat /h et stat /d avant la panne */
  int readdir;  /* readdir du dossier, panne active */
  int stat;     /* stat d'un nom du dossier, panne active */
  int eio, autres; /* /h : stat des 300 noms (FL_OK, FL_ECORRUPT pour la feuille trouée, FL_EIO) */
} essai_es;

/* Montage neuf, chemin chauffé, puis seul le bloc `panne` échoue en lecture. */
static essai_es essai_panne(fl_bdev *d, uint64_t panne, const char *dossier) {
  essai_es r = {FL_EIO, -1, -1, 0, 0};
  compteur c = {d, 0, 0};
  fl_bdev cd = {&cpt_ops, d->sector_size, d->sector_count, true, &c};
  fl_fs *fs = NULL;
  if (fl_fs_mount(&cd, FL_CONTENT_EXT4, false, &fs) != FL_OK) return r;
  fl_stat st;
  r.chauffe = fl_fs_stat(fs, "/h", &st);
  if (r.chauffe == FL_OK) r.chauffe = fl_fs_stat(fs, "/d", &st);
  c.panne = panne;
  collecte cl = {0};
  char p[128], dir[8];
  snprintf(dir, sizeof dir, "/%s", dossier);
  r.readdir = fl_fs_readdir(fs, dir, cb_collecte, &cl);
  nom_trou(p, sizeof p, dossier, 1);
  r.stat = fl_fs_stat(fs, p, &st);
  for (int i = 1; dossier[0] == 'h' && i <= 300; i++) {
    nom_trou(p, sizeof p, "h", i);
    int x = fl_fs_stat(fs, p, &st);
    if (x == FL_EIO) r.eio++;
    else if (x != FL_OK && x != FL_ECORRUPT) r.autres++;
  }
  c.panne = 0;
  fl_fs_unmount(fs);
  return r;
}

/* Une vraie erreur de lecture du périphérique dans un bloc de dossier reste FL_EIO (pas
 * FL_ECORRUPT, pas FL_ENOENT), en parcours comme en recherche : feuille htree de /h, bloc d'index
 * d'extents de /h (erreur rendue par ext4_dir_get_dblk elle-même), bloc 0 du dossier linéaire /d. */
static void test_ext4_erreur_es_dossier(void) {
  blocs_trou b;
  TT_ASSERT(lire_blocs(&b));
  fl_bdev *d = ouvrir("ext4-trou-dossier.img");
  TT_ASSERT(d != NULL);
  essai_es feuille = essai_panne(d, b.h_feuille, "h");
  essai_es etb = essai_panne(d, b.h_etb, "h");
  essai_es lin = essai_panne(d, b.d_bloc0, "d");
  fl_stat st;
  compteur c = {d, 0, b.d_bloc0};
  fl_bdev cd = {&cpt_ops, d->sector_size, d->sector_count, true, &c};
  fl_fs *fs = NULL;
  int rx = fl_fs_mount(&cd, FL_CONTENT_EXT4, false, &fs);
  if (rx == FL_OK) rx = fl_fs_stat(fs, "/d/x", &st); /* absent : la recherche lit le bloc 0 */
  fl_fs_unmount(fs);
  fl_bdev_close(d);
  TT_ASSERT_EQ(feuille.chauffe, FL_OK);
  TT_ASSERT_EQ(feuille.readdir, FL_EIO);
  TT_ASSERT(feuille.eio > 0); /* les noms de la feuille en panne */
  TT_ASSERT_EQ(feuille.autres, 0);
  TT_ASSERT_EQ(etb.chauffe, FL_OK);
  TT_ASSERT_EQ(etb.readdir, FL_EIO);
  TT_ASSERT_EQ(etb.stat, FL_EIO);
  TT_ASSERT_EQ(etb.eio, 300);
  TT_ASSERT_EQ(lin.chauffe, FL_OK);
  TT_ASSERT_EQ(lin.readdir, FL_EIO);
  TT_ASSERT_EQ(lin.stat, FL_EIO);
  TT_ASSERT_EQ(rx, FL_EIO);
}

/* lwext4 monté en écriture sur une copie en mémoire d'une image (chemin d'écriture, pas encore
 * branché sur l'API) ; le fichier n'est jamais écrit. */
#ifndef EFSCORRUPTED
#define EFSCORRUPTED EUCLEAN /* défini par le correctif 0002 */
#endif

typedef struct {
  uint8_t *img;
  uint64_t taille;
  int bas; /* accès touchant [0, 4096) (bloc 0 du système), hors superbloc exact [1024, 2048) */
  int sb;  /* accès au superbloc exact [1024, 2048) */
} memoire;

static memoire g_mem;

static int mem_open(struct ext4_blockdev *bd) { (void)bd; return EOK; }
static int mem_close(struct ext4_blockdev *bd) { (void)bd; return EOK; }

static int mem_acces(uint64_t blk, uint32_t cnt, uint64_t *off, size_t *len) {
  *off = blk * 512;
  *len = (size_t)cnt * 512;
  if (blk > g_mem.taille / 512 || cnt > g_mem.taille / 512 - blk) return EIO;
  if (cnt && *off == 1024 && *len == 1024) g_mem.sb++;
  else if (cnt && *off < 4096) g_mem.bas++;
  return EOK;
}

static int mem_bread(struct ext4_blockdev *bd, void *buf, uint64_t blk, uint32_t cnt) {
  (void)bd;
  uint64_t off;
  size_t len;
  int r = mem_acces(blk, cnt, &off, &len);
  if (r == EOK) memcpy(buf, g_mem.img + off, len);
  return r;
}

static int mem_bwrite(struct ext4_blockdev *bd, const void *buf, uint64_t blk, uint32_t cnt) {
  (void)bd;
  uint64_t off;
  size_t len;
  int r = mem_acces(blk, cnt, &off, &len);
  if (r == EOK) memcpy(g_mem.img + off, buf, len);
  return r;
}

static uint8_t g_mem_ph[512];
static struct ext4_blockdev_iface g_mem_if = {
    .open = mem_open, .bread = mem_bread, .bwrite = mem_bwrite, .close = mem_close,
    .ph_bsize = 512, .ph_bbuf = g_mem_ph};
static struct ext4_blockdev g_mem_bd = {.bdif = &g_mem_if};

static bool charger(const char *nom) {
  FILE *f = fopen(tt_fixture(nom), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long t = ftell(f);
  fseek(f, 0, SEEK_SET);
  g_mem.img = t > 0 ? malloc((size_t)t) : NULL;
  bool ok = g_mem.img && fread(g_mem.img, 1, (size_t)t, f) == (size_t)t;
  fclose(f);
  if (!ok) {
    free(g_mem.img);
    g_mem.img = NULL;
  }
  g_mem.taille = ok ? (uint64_t)t : 0;
  return ok;
}

/* Charge `nom` en mémoire, l'enregistre ("tt-mem") et le monte en écriture sur "/tt/".
 * *monte : à passer à demonter_mem, quel que soit le résultat. */
static int monter_mem(const char *nom, struct ext4_fs **fs, bool *monte) {
  *monte = false;
  *fs = NULL;
  if (!charger(nom)) return ENOENT;
  g_mem.bas = g_mem.sb = 0;
  g_mem_if.ph_bcnt = g_mem.taille / 512;
  g_mem_bd.part_offset = 0;
  g_mem_bd.part_size = g_mem.taille;
  int r = ext4_device_register(&g_mem_bd, "tt-mem");
  if (r != EOK) return r;
  r = ext4_mount("tt-mem", "/tt/", false);
  if (r != EOK) {
    ext4_device_unregister("tt-mem");
    return r;
  }
  *monte = true;
  struct ext4_sblock *sb = NULL;
  r = ext4_get_sblock("/tt/", &sb);
  if (r == EOK) *fs = (struct ext4_fs *)((char *)sb - offsetof(struct ext4_fs, sb));
  g_mem.bas = g_mem.sb = 0; /* le montage lit et écrit le superbloc */
  return r;
}

static void demonter_mem(bool monte) {
  if (monte) {
    ext4_umount("/tt/");
    ext4_device_unregister("tt-mem");
  }
  free(g_mem.img);
  g_mem.img = NULL;
}

/* Références d'inode du dossier `chemin` et de la racine (à rendre par ext4_fs_put_inode_ref). */
static int refs(struct ext4_fs *fs, const char *chemin, struct ext4_inode_ref *dir,
                struct ext4_inode_ref *racine) {
  uint32_t ino = 0;
  struct ext4_inode in;
  int r = ext4_raw_inode_fill(chemin, &ino, &in);
  if (r != EOK) return r;
  if ((r = ext4_fs_get_inode_ref(fs, ino, dir)) != EOK) return r;
  if ((r = ext4_fs_get_inode_ref(fs, EXT4_INODE_ROOT_INDEX, racine)) != EOK)
    ext4_fs_put_inode_ref(dir);
  return r;
}

/* ext4_dir_add_entry dans le dossier `chemin` : rend le premier code différent de EOK, après au
 * plus `essais` noms (nom_fmt avec le numéro d'essai). */
static int ajouter(struct ext4_fs *fs, const char *chemin, const char *nom_fmt, int essais) {
  struct ext4_inode_ref parent, enfant;
  int r = refs(fs, chemin, &parent, &enfant);
  if (r != EOK) return r;
  for (int i = 0; i < essais && r == EOK; i++) {
    char nom[256];
    snprintf(nom, sizeof nom, nom_fmt, i);
    r = ext4_dir_add_entry(&parent, nom, (uint32_t)strlen(nom), &enfant);
  }
  ext4_fs_put_inode_ref(&enfant);
  ext4_fs_put_inode_ref(&parent);
  return r;
}

static void test_ext4_trou_de_dossier_ajout(void) {
  struct ext4_fs *fs;
  bool monte;
  int rm = monter_mem("ext4-trou-dossier.img", &fs, &monte);
  int rd = -1, rh = -1, bas = -1, sb = -1;
  if (rm == EOK) {
    /* nom de 200 octets : ne tient pas dans la place libre du bloc 0 de /d, l'ajout va au bloc 1 */
    char longs[256];
    memset(longs, 'L', 200);
    memcpy(longs + 200, "%d", 3);
    rd = ajouter(fs, "/tt/d", longs, 1);
    /* htree : le hachage choisit la feuille ; on essaie des noms jusqu'à tomber sur le trou */
    rh = ajouter(fs, "/tt/h", "nouveau-%d", 200);
    bas = g_mem.bas;
    sb = g_mem.sb;
  }
  demonter_mem(monte);
  TT_ASSERT_EQ(rm, EOK);
  TT_ASSERT_EQ(rd, EFSCORRUPTED);
  TT_ASSERT_EQ(rh, EFSCORRUPTED);
  TT_ASSERT_EQ(bas, 0);
  TT_ASSERT_EQ(sb, 0);
}

/* Dossiers adressés par blocs (sans extents) : ext4_dir_dx_init (appelé par ext4_dir_rm via
 * ext4_trunc_dir) ne doit jamais prendre le bloc 0 du disque pour un bloc de dossier. */
static void test_ext4_dossier_indirect_bloc0(void) {
  struct ext4_fs *fs;
  bool monte;
  int rm = monter_mem("ext4-indirect.img", &fs, &monte);
  int rx = -1, rr = -1, rv = -1, bas = -1, sb = -1;
  if (rm == EOK) {
    struct ext4_inode_ref dir, racine;
    rx = refs(fs, "/tt/r", &dir, &racine);
    if (rx == EOK) {
      rx = ext4_dir_dx_init(&dir, &racine); /* trou au bloc logique 1 de /r */
      ext4_fs_put_inode_ref(&racine);
      ext4_fs_put_inode_ref(&dir);
    }
    rr = ext4_dir_rm("/tt/r");    /* le parcours de /r bute sur le trou */
    rv = ext4_dir_rm("/tt/vide"); /* un seul bloc : le bloc logique 1 serait à allouer */
    bas = g_mem.bas;
    sb = g_mem.sb;
  }
  demonter_mem(monte);
  TT_ASSERT_EQ(rm, EOK);
  TT_ASSERT_EQ(rx, EFSCORRUPTED);
  TT_ASSERT_EQ(rr, EFSCORRUPTED);
  TT_ASSERT_EQ(rv, ENOTSUP);
  TT_ASSERT_EQ(bas, 0);
  TT_ASSERT_EQ(sb, 0);
}

/* /hostile : inode hors bornes, « / », NUL, nom vide, puis e-valide.txt. */
static void test_ext4_entree_hostile_ignoree(void) {
  fl_bdev *d = ouvrir("ext4-entrees-hostiles.img");
  TT_ASSERT(d != NULL);
  fl_fs *fs = NULL;
  if (fl_fs_mount(d, FL_CONTENT_EXT4, false, &fs) != FL_OK) fl_bdev_close(d);
  TT_ASSERT(fs != NULL);
  collecte c = {0};
  int rd = fl_fs_readdir(fs, "/hostile", cb_collecte, &c);
  fl_stat st;
  int rh = fl_fs_stat(fs, "/hostile/a-hors-bornes", &st);
  int rv = fl_fs_stat(fs, "/hostile/e-valide.txt", &st);
  fl_file *f = NULL;
  char buf[32] = {0};
  int ro = fl_fs_open(fs, "/hostile/a-hors-bornes", FL_O_RDONLY, &f);
  if (ro == FL_OK) fl_file_close(f);
  f = NULL;
  int rov = fl_fs_open(fs, "/hostile/e-valide.txt", FL_O_RDONLY, &f);
  int64_t n = rov == FL_OK ? fl_file_pread(f, buf, sizeof buf, 0) : -1;
  fl_file_close(f);
  int rvide = fl_fs_stat(fs, "/hostile/d-vide", &st);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
  TT_ASSERT_EQ(rd, FL_OK);
  TT_ASSERT_EQ(c.n, 1);
  TT_ASSERT_STR_EQ(c.noms[0], "e-valide.txt");
  TT_ASSERT_EQ(c.e[0].st.size, 13);
  TT_ASSERT_EQ(rh, FL_ENOENT);
  TT_ASSERT_EQ(ro, FL_ENOENT);
  TT_ASSERT_EQ(rv, FL_OK);
  TT_ASSERT_EQ(rov, FL_OK);
  TT_ASSERT_EQ(n, 13);
  TT_ASSERT_MEM_EQ(buf, "e-valide.txt\n", 13);
  TT_ASSERT_EQ(rvide, FL_ENOENT);
}

int main(void) {
  TT_RUN(test_ext4_racine);
  TT_RUN(test_ext4_stat);
  TT_RUN(test_ext4_lecture);
  TT_RUN(test_ext4_motif_decalages);
  TT_RUN(test_ext4_lecture_complete);
  TT_RUN(test_ext4_beaucoup);
  TT_RUN(test_ext4_lecture_au_dela_de_4gio);
  TT_RUN(test_ext4_lecture_seule);
  TT_RUN(test_ext4_sur_luks2);
  TT_RUN(test_ext4_sur_luks2_4k);
  TT_RUN(test_ext4_deux_montages);
  TT_RUN(test_ext4_montages_repetes);
  TT_RUN(test_ext4_chemins);
  TT_RUN(test_ext4_erreurs_types);
  TT_RUN(test_ext4_readdir_callback);
  TT_RUN(test_ext4_pread_entrelaces);
  TT_RUN(test_ext4_fils_concurrents);
  TT_RUN(test_ext4_superbloc_hostile);
  TT_RUN(test_ext4_needs_recovery);
  TT_RUN(test_ext4_superbloc_change);
  TT_RUN(test_ext4_nom_255);
  TT_RUN(test_ext4_trou_de_dossier);
  TT_RUN(test_ext4_trou_de_dossier_ajout);
  TT_RUN(test_ext4_erreur_es_dossier);
  TT_RUN(test_ext4_dossier_indirect_bloc0);
  TT_RUN(test_ext4_entree_hostile_ignoree);
  TT_MAIN_END();
}
