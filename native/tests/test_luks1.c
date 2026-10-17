#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crypto/fl_crypto.h"
#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/luks.h"
#include "fluluks/partition.h"
#include "fluluks/probe.h"
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

/* Compare tout le bdev déchiffré avec ext4-plain.img, par morceaux. */
static void comparer_ext4(fl_bdev *dec) {
  fl_bdev *ref = ouvrir("ext4-plain.img");
  TT_ASSERT(ref != NULL);
  TT_ASSERT_EQ(fl_bdev_size_bytes(dec), 64u * 1024 * 1024);
  TT_ASSERT_EQ(fl_bdev_size_bytes(ref), 64u * 1024 * 1024);
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

static void deverrouiller(fl_bdev *dev, const char *cipher) {
  char pass[256];
  size_t n = lire_pass(pass, sizeof pass, "pass.txt");
  TT_ASSERT(n > 0);
  fl_luks *l = NULL;
  TT_ASSERT_EQ(fl_luks_open(dev, &l), FL_OK);
  TT_ASSERT_EQ(fl_luks_version(l), 1);
  TT_ASSERT_STR_EQ(fl_luks_cipher(l), cipher);
  fl_bdev *dec = NULL;
  TT_ASSERT_EQ(fl_luks_unlock(l, pass, n, &dec), FL_OK);
  comparer_ext4(dec);
  fl_probe_result r;
  TT_ASSERT_EQ(fl_probe(dec, &r), FL_OK);
  TT_ASSERT_EQ(r.content, FL_CONTENT_EXT4);
  /* lecture seule héritée : l'écriture est refusée */
  uint8_t z[512] = {0};
  TT_ASSERT_EQ(fl_bdev_write(dec, 0, 1, z), FL_EROFS);
  fl_bdev_close(dec);
  fl_luks_close(l);
}

static void test_luks1_essiv(void) {
  fl_bdev *d = ouvrir("mbr-luks1-essiv.img");
  TT_ASSERT(d != NULL);
  fl_part_table t;
  TT_ASSERT_EQ(fl_part_scan(d, &t), FL_OK);
  TT_ASSERT(t.count >= 1);
  fl_bdev *s = NULL;
  TT_ASSERT_EQ(fl_slice_bdev_new(d, t.parts[0].first_lba, t.parts[0].sector_count, &s), FL_OK);
  deverrouiller(s, "aes-cbc-essiv:sha256");
  fl_bdev_close(s);
  fl_bdev_close(d);
}

static void test_luks1_xts_sha1(void) {
  fl_bdev *d = ouvrir("whole-luks1-xts.img");
  TT_ASSERT(d != NULL);
  deverrouiller(d, "aes-xts-plain64");
  fl_bdev_close(d);
}

static void test_luks1_mauvaise_phrase(void) {
  fl_bdev *d = ouvrir("whole-luks1-xts.img");
  TT_ASSERT(d != NULL);
  fl_luks *l = NULL;
  TT_ASSERT_EQ(fl_luks_open(d, &l), FL_OK);
  fl_bdev *dec = (fl_bdev *)1;
  TT_ASSERT_EQ(fl_luks_unlock(l, "faux", 4, &dec), FL_EBADPASS);
  TT_ASSERT(dec == NULL);
  fl_luks_close(l);
  fl_luks_close(NULL);
  fl_bdev_close(d);
}

static void test_luks1_pas_luks(void) {
  fl_bdev *d = ouvrir("ext4-plain.img");
  TT_ASSERT(d != NULL);
  fl_luks *l = NULL;
  TT_ASSERT_EQ(fl_luks_open(d, &l), FL_ENOTSUP);
  TT_ASSERT(l == NULL);
  fl_bdev_close(d);
}

/* Copie l'en-tête LUKS1 en mémoire, la corrompt, et vérifie le rejet. */
typedef struct { uint8_t *buf; } membdev;
static int mem_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  memcpy(buf, (uint8_t *)d->priv + lba * 512, (size_t)count * 512);
  return FL_OK;
}
static const fl_bdev_ops mem_ops = {mem_read, NULL, NULL, NULL};

static void corrompu(void (*mut)(uint8_t *h), int attendu) {
  fl_bdev *src = ouvrir("whole-luks1-xts.img");
  TT_ASSERT(src != NULL);
  size_t cap = 4u * 1024 * 1024; /* en-tête + zones de clés, payload à 2 Mio */
  uint8_t *m = calloc(1, cap);
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(fl_bdev_read_bytes(src, 0, 4096, m), FL_OK);
  fl_bdev_close(src);
  mut(m);
  fl_bdev dev = {&mem_ops, 512, cap / 512, true, m};
  fl_luks *l = NULL;
  int rc = fl_luks_open(&dev, &l);
  if (rc == FL_OK) fl_luks_close(l);
  free(m);
  TT_ASSERT_EQ(rc, attendu);
}
static void mut_keybytes(uint8_t *h) { h[108 + 3] = 17; }
static void mut_stripes(uint8_t *h) { h[208 + 44] = 0x7f; }
static void mut_payload(uint8_t *h) { h[104] = 0x7f; }
static void mut_cipher(uint8_t *h) { memset(h + 8, 'a', 32); }
static void mut_iter(uint8_t *h) { memset(h + 208 + 4, 0, 4); }
static void mut_digest_iter(uint8_t *h) { h[164] = 0x7f; }
static void mut_kmo(uint8_t *h) { h[208 + 40] = 0x7f; }

static void test_luks1_corrompu(void) {
  corrompu(mut_keybytes, FL_ECORRUPT);
  corrompu(mut_stripes, FL_ECORRUPT);
  corrompu(mut_payload, FL_ECORRUPT);
  corrompu(mut_cipher, FL_ECORRUPT);
  corrompu(mut_iter, FL_ECORRUPT);
  corrompu(mut_kmo, FL_ECORRUPT);
  corrompu(mut_digest_iter, FL_ENOTSUP);
}

/* Lecture mémoire avec zone en échec (EIO) pour tester la poursuite après erreur de slot. */
static uint64_t fail_lo, fail_hi;
static int mem_read_fail(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  if (lba < fail_hi && lba + count > fail_lo) return FL_EIO;
  return mem_read(d, lba, count, buf);
}
static const fl_bdev_ops mem_fail_ops = {mem_read_fail, NULL, NULL, NULL};

/* mutation de l'en-tête : modifie le slot 0 (kmo / itérations) et active un slot 1 copie de l'original. */
static void unlock_mute(int variante, const char *pass, size_t pl, int attendu) {
  fl_bdev *src = ouvrir("whole-luks1-xts.img");
  TT_ASSERT(src != NULL);
  size_t cap = 4u * 1024 * 1024;
  uint8_t *m = calloc(1, cap);
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(fl_bdev_read_bytes(src, 0, 2u * 1024 * 1024, m), FL_OK);
  fl_bdev_close(src);
  uint8_t *s0 = m + 208, *s1 = m + 208 + 48;
  int deuxslots = variante != 0;
  if (deuxslots) memcpy(s1, s0, 48); /* slot 1 = bon slot d'origine */
  if (variante == 0 || variante == 1) {
    /* slot 0 : AF lue dans une zone en échec EIO */
    uint32_t kmo = 3000;
    s0[40] = (uint8_t)(kmo >> 24); s0[41] = (uint8_t)(kmo >> 16); s0[42] = (uint8_t)(kmo >> 8); s0[43] = (uint8_t)kmo;
    fail_lo = 3000; fail_hi = 3500;
  } else {
    /* slot 0 : itérations au-dessus du plafond */
    s0[4] = 0x7f;
    fail_lo = fail_hi = 0;
  }
  fl_bdev dev = {&mem_fail_ops, 512, cap / 512, true, m};
  fl_luks *l = NULL;
  TT_ASSERT_EQ(fl_luks_open(&dev, &l), FL_OK);
  fl_bdev *dec = NULL;
  TT_ASSERT_EQ(fl_luks_unlock(l, pass, pl, &dec), attendu);
  if (attendu == FL_OK) {
    TT_ASSERT(dec != NULL);
    fl_bdev_close(dec);
  } else {
    TT_ASSERT(dec == NULL);
  }
  fl_luks_close(l);
  free(m);
}

static void test_luks1_poursuite_apres_erreur(void) {
  char pass[256];
  size_t n = lire_pass(pass, sizeof pass, "pass.txt");
  TT_ASSERT(n > 0);
  unlock_mute(0, pass, n, FL_EIO);        /* un seul slot, E/S en échec */
  unlock_mute(1, pass, n, FL_OK);         /* slot 0 EIO, slot 1 bon : on continue */
  unlock_mute(1, "faux", 4, FL_EIO);      /* EIO l'emporte sur EBADPASS */
  unlock_mute(2, pass, n, FL_OK);         /* slot 0 au-dessus du plafond, slot 1 bon */
  unlock_mute(2, "faux", 4, FL_EBADPASS); /* ENOTSUP < EBADPASS */
}

/* split AF local au test (spec LUKS) */
static void diffuse(const char *h, uint8_t *buf, size_t len) {
  size_t hl = fl_hash_len(h);
  uint32_t i = 0;
  for (size_t off = 0; off < len; off += hl, i++) {
    size_t n = len - off < hl ? len - off : hl;
    uint8_t in[4 + 64], out[64];
    in[0] = (uint8_t)(i >> 24); in[1] = (uint8_t)(i >> 16); in[2] = (uint8_t)(i >> 8); in[3] = (uint8_t)i;
    memcpy(in + 4, buf + off, n);
    fl_hash(h, in, 4 + n, out);
    memcpy(buf + off, out, n);
  }
}

static void test_af_merge_aller_retour(void) {
  const uint32_t stripes = 4000;
  const size_t kl = 64;
  uint8_t key[64], back[64];
  for (size_t i = 0; i < kl; i++) key[i] = (uint8_t)(i * 7 + 3);
  uint8_t *split = malloc(kl * stripes);
  TT_ASSERT(split != NULL);
  uint8_t d[64] = {0};
  uint32_t s = 12345;
  for (uint32_t i = 0; i + 1 < stripes; i++) {
    for (size_t j = 0; j < kl; j++) { s = s * 1103515245u + 12345u; split[i * kl + j] = (uint8_t)(s >> 16); }
    for (size_t j = 0; j < kl; j++) d[j] ^= split[i * kl + j];
    diffuse("sha256", d, kl);
  }
  for (size_t j = 0; j < kl; j++) split[(stripes - 1) * kl + j] = d[j] ^ key[j];
  TT_ASSERT_EQ(fl_af_merge("sha256", split, kl, stripes, back), FL_OK);
  TT_ASSERT_MEM_EQ(back, key, kl);
  TT_ASSERT_EQ(fl_af_merge("md5", split, kl, stripes, back), FL_ENOTSUP);
  TT_ASSERT_EQ(fl_af_merge("sha256", split, kl, 0, back), FL_EINVAL);
  free(split);
}

/* crypt_bdev : écriture puis relecture sur un support mémoire, XTS 4K sans IV larges. */
static int mw_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf) {
  memcpy((uint8_t *)d->priv + lba * 512, buf, (size_t)count * 512);
  return FL_OK;
}
static const fl_bdev_ops memrw_ops = {mem_read, mw_write, NULL, NULL};

static void test_crypt_bdev_ecriture_4k(void) {
  size_t cap = 64 * 1024;
  uint8_t *m = calloc(1, cap);
  TT_ASSERT(m != NULL);
  fl_bdev dev = {&memrw_ops, 512, cap / 512, false, m};
  uint8_t mk[64];
  for (int i = 0; i < 64; i++) mk[i] = (uint8_t)(i + 1);
  fl_bdev *c = NULL;
  TT_ASSERT_EQ(fl_crypt_bdev_new(&dev, 4096, 0, "aes-xts-plain64", mk, 64, 4096, false, 0, &c), FL_OK);
  TT_ASSERT_EQ(c->sector_size, 4096);
  TT_ASSERT_EQ(c->sector_count, (cap - 4096) / 4096);
  uint8_t pt[8192], rd[8192];
  for (size_t i = 0; i < sizeof pt; i++) pt[i] = (uint8_t)(i * 31);
  TT_ASSERT_EQ(fl_bdev_write(c, 1, 2, pt), FL_OK);
  TT_ASSERT(memcmp(m + 4096 + 4096, pt, 4096) != 0); /* stocké chiffré */
  TT_ASSERT_EQ(fl_bdev_read(c, 1, 2, rd), FL_OK);
  TT_ASSERT_MEM_EQ(rd, pt, sizeof pt);
  /* IV de référence : secteur 1 de 4K, IV = 8 (unités de 512) */
  fl_sector_cipher *sc = NULL;
  TT_ASSERT_EQ(fl_sector_cipher_new("aes-xts-plain64", mk, 64, 4096, &sc), FL_OK);
  uint8_t ref[4096];
  TT_ASSERT_EQ(fl_sector_decrypt(sc, 8, m + 8192, ref, 4096), FL_OK);
  TT_ASSERT_MEM_EQ(ref, pt, 4096);
  fl_sector_cipher_free(sc);
  TT_ASSERT_EQ(fl_bdev_read(c, c->sector_count, 1, rd), FL_EINVAL);
  fl_bdev_close(c);
  /* paramètres hostiles */
  TT_ASSERT_EQ(fl_crypt_bdev_new(&dev, cap + 512, 0, "aes-xts-plain64", mk, 64, 512, false, 0, &c), FL_EINVAL);
  TT_ASSERT_EQ(fl_crypt_bdev_new(&dev, 0, cap + 512, "aes-xts-plain64", mk, 64, 512, false, 0, &c), FL_EINVAL);
  TT_ASSERT_EQ(fl_crypt_bdev_new(&dev, 0, 0, "des-ecb", mk, 64, 512, false, 0, &c), FL_ENOTSUP);
  free(m);
}

int main(void) {
  TT_RUN(test_af_merge_aller_retour);
  TT_RUN(test_crypt_bdev_ecriture_4k);
  TT_RUN(test_luks1_pas_luks);
  TT_RUN(test_luks1_corrompu);
  TT_RUN(test_luks1_poursuite_apres_erreur);
  TT_RUN(test_luks1_mauvaise_phrase);
  TT_RUN(test_luks1_essiv);
  TT_RUN(test_luks1_xts_sha1);
  TT_MAIN_END();
}
