#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "crypto/fl_crypto.h"
#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/luks.h"
#include "fluluks/partition.h"
#include "luks/luks_internal.h"
#include "tt.h"

#define CHUNK (1u << 20)

static fl_bdev *ouvrir(const char *nom) {
  fl_bdev *d = NULL;
  if (fl_file_bdev_open(tt_fixture(nom), 512, true, &d) != FL_OK) return NULL;
  return d;
}

static size_t lire_pass(char *buf, size_t cap, const char *nom) {
  FILE *f = fopen(tt_fixture(nom), "rb");
  if (!f) return 0;
  size_t n = fread(buf, 1, cap, f);
  fclose(f);
  return n;
}

static void comparer_ext4(fl_bdev *dec) {
  fl_bdev *ref = ouvrir("ext4-plain.img");
  TT_ASSERT(ref != NULL);
  TT_ASSERT_EQ(fl_bdev_size_bytes(dec), 64u * 1024 * 1024);
  uint8_t *a = malloc(CHUNK), *b = malloc(CHUNK);
  TT_ASSERT(a && b);
  uint64_t size = fl_bdev_size_bytes(dec);
  for (uint64_t off = 0; off < size; off += CHUNK) {
    if (fl_bdev_read_bytes(dec, off, CHUNK, a) != FL_OK || fl_bdev_read_bytes(ref, off, CHUNK, b) != FL_OK ||
        memcmp(a, b, CHUNK) != 0) {
      fprintf(stderr, "difference au decalage %llu\n", (unsigned long long)off);
      tt_failures++;
      break;
    }
  }
  free(a);
  free(b);
  fl_bdev_close(ref);
}

/* Ouvre, déverrouille avec le fichier de phrase donné, compare à ext4-plain.img. */
static void deverrouiller(fl_bdev *dev, const char *passfile, const char *cipher, uint32_t sector_size) {
  char pass[256];
  size_t n = lire_pass(pass, sizeof pass, passfile);
  TT_ASSERT(n > 0);
  fl_luks *l = NULL;
  TT_ASSERT_EQ(fl_luks_open(dev, &l), FL_OK);
  TT_ASSERT_EQ(fl_luks_version(l), 2);
  TT_ASSERT_STR_EQ(fl_luks_cipher(l), cipher);
  fl_bdev *dec = NULL;
  TT_ASSERT_EQ(fl_luks_unlock(l, pass, n, &dec), FL_OK);
  TT_ASSERT_EQ(dec->sector_size, sector_size);
  comparer_ext4(dec);
  uint8_t z[512] = {0};
  TT_ASSERT_EQ(fl_bdev_write(dec, 0, 1, z), FL_EROFS);
  fl_bdev_close(dec);
  fl_luks_close(l);
}

static void test_luks2_gpt_argon2id(void) {
  fl_bdev *d = ouvrir("gpt-luks2.img");
  TT_ASSERT(d != NULL);
  fl_part_table t;
  TT_ASSERT_EQ(fl_part_scan(d, &t), FL_OK);
  TT_ASSERT(t.count >= 1);
  fl_bdev *s = NULL;
  TT_ASSERT_EQ(fl_slice_bdev_new(d, t.parts[0].first_lba, t.parts[0].sector_count, &s), FL_OK);
  deverrouiller(s, "pass.txt", "aes-xts-plain64", 512);
  fl_bdev_close(s);
  fl_bdev_close(d);
}

static void test_luks2_4k_argon2i(void) {
  fl_bdev *d = ouvrir("whole-luks2-4k.img");
  TT_ASSERT(d != NULL);
  deverrouiller(d, "pass.txt", "aes-xts-plain64", 4096);
  fl_bdev_close(d);
}

static void test_luks2_deuxieme_slot_utf8(void) {
  fl_bdev *d = ouvrir("whole-luks2-2slots.img");
  TT_ASSERT(d != NULL);
  deverrouiller(d, "pass2.txt", "aes-cbc-plain64", 512);
  deverrouiller(d, "pass.txt", "aes-cbc-plain64", 512);
  fl_bdev_close(d);
}

static void test_luks2_mauvaise_phrase(void) {
  fl_bdev *d = ouvrir("whole-luks2-2slots.img");
  TT_ASSERT(d != NULL);
  fl_luks *l = NULL;
  TT_ASSERT_EQ(fl_luks_open(d, &l), FL_OK);
  fl_bdev *dec = (fl_bdev *)1;
  TT_ASSERT_EQ(fl_luks_unlock(l, "faux", 4, &dec), FL_EBADPASS);
  TT_ASSERT(dec == NULL);
  fl_luks_close(l);
  fl_bdev_close(d);
}

static void test_luks2_entete_secondaire(void) {
  fl_bdev *d = ouvrir("luks2-entete-abime.img");
  TT_ASSERT(d != NULL);
  deverrouiller(d, "pass.txt", "aes-xts-plain64", 4096);
  fl_bdev_close(d);
}

static void test_luks2_tronque(void) {
  fl_bdev *d = ouvrir("luks-tronque.img");
  TT_ASSERT(d != NULL);
  fl_luks *l = (fl_luks *)1;
  TT_ASSERT_EQ(fl_luks_open(d, &l), FL_ECORRUPT);
  TT_ASSERT(l == NULL);
  fl_bdev_close(d);
}

static void test_luks2_taille_dynamique_impaire(void) {
  fl_bdev *a = ouvrir("whole-luks2-4k.img"), *b = ouvrir("whole-luks2-impair.img");
  TT_ASSERT(a && b);
  char pass[256];
  size_t n = lire_pass(pass, sizeof pass, "pass.txt");
  fl_luks *la = NULL, *lb = NULL;
  TT_ASSERT_EQ(fl_luks_open(a, &la), FL_OK);
  TT_ASSERT_EQ(fl_luks_open(b, &lb), FL_OK);
  fl_bdev *da = NULL, *db = NULL;
  TT_ASSERT_EQ(fl_luks_unlock(la, pass, n, &da), FL_OK);
  TT_ASSERT_EQ(fl_luks_unlock(lb, pass, n, &db), FL_OK);
  TT_ASSERT_EQ(da->sector_count, db->sector_count);
  TT_ASSERT_EQ(da->sector_size, db->sector_size);
  fl_bdev_close(da);
  fl_bdev_close(db);
  fl_luks_close(la);
  fl_luks_close(lb);
  fl_bdev_close(a);
  fl_bdev_close(b);
}

static void test_b64(void) {
  uint8_t o[16];
  size_t n = 99;
  TT_ASSERT_EQ(fl_b64_decode("", o, sizeof o, &n), FL_OK);
  TT_ASSERT_EQ(n, 0);
  TT_ASSERT_EQ(fl_b64_decode("Zm9vYg==", o, sizeof o, &n), FL_OK);
  TT_ASSERT_EQ(n, 4);
  TT_ASSERT_MEM_EQ(o, "foob", 4);
  TT_ASSERT_EQ(fl_b64_decode("Zm9vYmE=", o, sizeof o, &n), FL_OK);
  TT_ASSERT_EQ(n, 5);
  TT_ASSERT_EQ(fl_b64_decode("Zm9v", o, sizeof o, &n), FL_OK);
  TT_ASSERT_EQ(n, 3);
  TT_ASSERT_EQ(fl_b64_decode("Zm9", o, sizeof o, &n), FL_ECORRUPT);
  TT_ASSERT_EQ(fl_b64_decode("Zm9v!A==", o, sizeof o, &n), FL_ECORRUPT);
  TT_ASSERT_EQ(fl_b64_decode("Zm=v", o, sizeof o, &n), FL_ECORRUPT);
  TT_ASSERT_EQ(fl_b64_decode("Zg==Zg==", o, sizeof o, &n), FL_ECORRUPT);
  TT_ASSERT_EQ(fl_b64_decode("Zm9vYmFyZm9v", o, 8, &n), FL_ECORRUPT);
}

/* --- paramètres hostiles : on réécrit le JSON en mémoire (les deux en-têtes) et on recalcule la somme --- */
static uint8_t *g_mem;
#define MEMCAP (32u * 1024 * 1024)
static int mem_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  memcpy(buf, (uint8_t *)d->priv + lba * 512, (size_t)count * 512);
  return FL_OK;
}
static const fl_bdev_ops mem_ops = {mem_read, NULL, NULL, NULL};

static void resigner(uint8_t *h, size_t hdr_size) {
  memset(h + 448, 0, 64);
  uint8_t dg[32];
  fl_hash("sha256", h, hdr_size, dg);
  memcpy(h + 448, dg, 32);
}

/* remplace old par repl dans le JSON de l'en-tête h (taille hdr_size) */
static int patch(uint8_t *h, size_t hdr_size, const char *old, const char *repl) {
  char *j = (char *)h + 4096;
  size_t cap = hdr_size - 4096, ol = strlen(old), rl = strlen(repl);
  char *p = memmem(j, cap, old, ol);
  if (!p) return -1;
  size_t tail = cap - (size_t)(p - j) - ol;
  if (rl > ol) {
    if (tail < rl - ol) return -1;
    tail -= rl - ol; /* les octets perdus sont le remplissage NUL final */
  }
  memmove(p + rl, p + ol, tail);
  memcpy(p, repl, rl);
  if (rl < ol) memset(j + cap - (ol - rl), 0, ol - rl);
  resigner(h, hdr_size);
  return 0;
}

static int ouvrir_patche(const char *img, const char *old, const char *repl) {
  fl_bdev *src = ouvrir(img);
  if (!src) return -1000;
  g_mem = calloc(1, MEMCAP);
  fl_bdev_read_bytes(src, 0, 32768, g_mem);
  fl_bdev_close(src);
  if (patch(g_mem, 16384, old, repl) || patch(g_mem + 16384, 16384, old, repl)) {
    free(g_mem);
    return -1001;
  }
  fl_bdev dev = {&mem_ops, 512, MEMCAP / 512, true, g_mem};
  fl_luks *l = NULL;
  int rc = fl_luks_open(&dev, &l);
  fl_luks_close(l);
  free(g_mem);
  return rc;
}

static void test_luks2_hostile(void) {
  const char *img = "whole-luks2-4k.img";
  TT_ASSERT_EQ(ouvrir_patche(img, "\"stripes\":4000", "\"stripes\":4000"), FL_OK);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"stripes\":4000", "\"stripes\":0"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"stripes\":4000", "\"stripes\":65537"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"memory\":32768", "\"memory\":4194305"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"memory\":32768", "\"memory\":8"), FL_ECORRUPT); /* < 8*cpus */
  TT_ASSERT_EQ(ouvrir_patche(img, "\"cpus\":2", "\"cpus\":65"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"cpus\":2", "\"cpus\":0"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"time\":4", "\"time\":1001"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"time\":4", "\"time\":0"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"key_size\":64,\"af\"", "\"key_size\":48,\"af\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"offset\":\"32768\"", "\"offset\":\"-1\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"offset\":\"32768\"", "\"offset\":\"99999999999999999999\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"offset\":\"32768\"", "\"offset\":\"0x8000\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"offset\":\"32768\"", "\"offset\":\"33000\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"size\":\"258048\"", "\"size\":\"1024\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"size\":\"258048\"", "\"size\":\"999999999999\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"offset\":\"16777216\"", "\"offset\":\"999999999999\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"offset\":\"16777216\"", "\"offset\":\"16777217\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"sector_size\":4096", "\"sector_size\":4097"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"type\":\"luks2\"", "\"type\":\"autre\""), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"type\":\"argon2i\"", "\"type\":\"scrypt\""), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"type\":\"crypt\"", "\"type\":\"linear\""), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"type\":\"raw\"", "\"type\":\"journal\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"type\":\"luks1\"", "\"type\":\"luks2\""), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"encryption\":\"aes-xts-plain64\",\"sector", "\"encryption\":\"serpent-xts-plain64\",\"sector"), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"salt\":\"WDjoV0lHtFWBWwgvO3gR0hSj/Nxhk4hPYEfK8AQii1A=\"", "\"salt\":\"!!!\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"iterations\":1000", "\"iterations\":0"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"segments\":{\"0\"", "\"segments\":{\"5\":{\"type\":\"crypt\"},\"0\""), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"keyslots\":{\"0\":", "\"keyslots\":[],\"x\":"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "{\"keyslots\"", "[{\"keyslots\""), FL_ECORRUPT);
}

static void test_luks2_priorite_et_digest(void) {
  const char *img = "whole-luks2-2slots.img";
  char pass[256];
  size_t n = lire_pass(pass, sizeof pass, "pass.txt");
  /* priorité 0 sur les deux slots : rien à essayer -> mauvaise phrase */
  fl_bdev *src = ouvrir(img);
  TT_ASSERT(src != NULL);
  uint8_t *m = calloc(1, MEMCAP);
  TT_ASSERT(m != NULL);
  fl_bdev_read_bytes(src, 0, 32768, m);
  fl_bdev_close(src);
  TT_ASSERT_EQ(patch(m, 16384, "\"kdf\":{\"type\":\"pbkdf2\",\"hash\":\"sha256\",\"iterations\":1000,\"salt\":\"Eah", "\"priority\":0,\"kdf\":{\"type\":\"pbkdf2\",\"hash\":\"sha256\",\"iterations\":1000,\"salt\":\"Eah"), 0);
  TT_ASSERT_EQ(patch(m + 16384, 16384, "\"kdf\":{\"type\":\"pbkdf2\",\"hash\":\"sha256\",\"iterations\":1000,\"salt\":\"Eah", "\"priority\":0,\"kdf\":{\"type\":\"pbkdf2\",\"hash\":\"sha256\",\"iterations\":1000,\"salt\":\"Eah"), 0);
  fl_bdev dev = {&mem_ops, 512, MEMCAP / 512, true, m};
  fl_luks *l = NULL;
  TT_ASSERT_EQ(fl_luks_open(&dev, &l), FL_OK);
  fl_bdev *dec = NULL;
  /* slot 0 ignoré ; la phrase pass.txt ouvre le slot 0 seulement (pass2 -> slot 1) */
  TT_ASSERT_EQ(fl_luks_unlock(l, pass, n, &dec), FL_EBADPASS);
  fl_luks_close(l);
  /* le digest ne lie plus aucun slot : même la bonne phrase est refusée */
  free(m);
  TT_ASSERT_EQ(ouvrir_patche(img, "\"keyslots\":[\"0\",\"1\"]", "\"keyslots\":[\"7\"]"), FL_ECORRUPT);
}

int main(void) {
  TT_RUN(test_b64);
  TT_RUN(test_luks2_hostile);
  TT_RUN(test_luks2_priorite_et_digest);
  TT_RUN(test_luks2_gpt_argon2id);
  TT_RUN(test_luks2_4k_argon2i);
  TT_RUN(test_luks2_deuxieme_slot_utf8);
  TT_RUN(test_luks2_mauvaise_phrase);
  TT_RUN(test_luks2_entete_secondaire);
  TT_RUN(test_luks2_tronque);
  TT_RUN(test_luks2_taille_dynamique_impaire);
  TT_MAIN_END();
}
