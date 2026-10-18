#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "crypto/fl_crypto.h"
#include "fluluks/error.h"
#include "tt.h"

static size_t unhex(const char *h, uint8_t *out, size_t max) {
  size_t n = strlen(h) / 2;
  if (n > max) return 0;
  for (size_t i = 0; i < n; i++) {
    unsigned v;
    if (sscanf(h + 2 * i, "%2x", &v) != 1) return 0;
    out[i] = (uint8_t)v;
  }
  return n;
}

static void test_hash(void) {
  uint8_t out[64], exp[64];
  TT_ASSERT_EQ(fl_hash_len("sha1"), 20);
  TT_ASSERT_EQ(fl_hash_len("sha256"), 32);
  TT_ASSERT_EQ(fl_hash_len("sha512"), 64);
  TT_ASSERT_EQ(fl_hash_len("md5"), 0);
  TT_ASSERT_EQ(fl_hash("sha256", "abc", 3, out), FL_OK);
  unhex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", exp, 32);
  TT_ASSERT_MEM_EQ(out, exp, 32);
  TT_ASSERT_EQ(fl_hash("sha1", "abc", 3, out), FL_OK);
  unhex("a9993e364706816aba3e25717850c26c9cd0d89d", exp, 20);
  TT_ASSERT_MEM_EQ(out, exp, 20);
  TT_ASSERT_EQ(fl_hash("md5", "abc", 3, out), FL_ENOTSUP);
}

/* FIPS 180-4 : SHA-512 de « abc » et de la chaîne vide ; en plus, le message de 112 octets. */
static void test_crypto_sha512(void) {
  uint8_t out[64], exp[64];
  unhex("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", exp, 64);
  TT_ASSERT_EQ(fl_hash("sha512", "abc", 3, out), FL_OK);
  TT_ASSERT_MEM_EQ(out, exp, 64);
  unhex("cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
        "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e", exp, 64);
  TT_ASSERT_EQ(fl_hash("sha512", "", 0, out), FL_OK);
  TT_ASSERT_MEM_EQ(out, exp, 64);
  const char *m112 = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
  unhex("8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
        "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909", exp, 64);
  TT_ASSERT_EQ(fl_hash("sha512", m112, strlen(m112), out), FL_OK);
  TT_ASSERT_MEM_EQ(out, exp, 64);
}

static void test_pbkdf2_rfc7914(void) {
  uint8_t out[64], exp[64];
  unhex("55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
        "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783", exp, 64);
  TT_ASSERT_EQ(fl_pbkdf2("sha256", "passwd", 6, "salt", 4, 1, out, 64), FL_OK);
  TT_ASSERT_MEM_EQ(out, exp, 64);
  /* RFC 6070 : PBKDF2-HMAC-SHA1, c=2 */
  unhex("ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957", exp, 20);
  TT_ASSERT_EQ(fl_pbkdf2("sha1", "password", 8, "salt", 4, 2, out, 20), FL_OK);
  TT_ASSERT_MEM_EQ(out, exp, 20);
  TT_ASSERT_EQ(fl_pbkdf2("sha512", "p", 1, "s", 1, 1, out, 64), FL_OK);
  TT_ASSERT_EQ(fl_pbkdf2("md5", "p", 1, "s", 1, 1, out, 16), FL_ENOTSUP);
}

static void test_argon2i_reference(void) {
  /* Vecteur du README phc-winner-argon2, confirme par le CLI de reference. */
  uint8_t out[24], exp[24];
  unhex("45d7ac72e76f242b20b77b9bf9bf9d5915894e669a24e6c6", exp, 24);
  TT_ASSERT_EQ(fl_argon2(FL_ARGON2I, "password", 8, "somesalt", 8, 2, 65536, 4, out, 24), FL_OK);
  TT_ASSERT_MEM_EQ(out, exp, 24);
}

static void test_argon2id_reference(void) {
  /* Calcule avec le CLI de reference : echo -n password | argon2 somesalt -id -t 2 -m 16 -p 4 -l 24 */
  uint8_t out[32], exp[32];
  unhex("1758c6d82577fcdafec91b88b0f2b0d09f6be2cedc247054", exp, 24);
  TT_ASSERT_EQ(fl_argon2(FL_ARGON2ID, "password", 8, "somesalt", 8, 2, 65536, 4, out, 24), FL_OK);
  TT_ASSERT_MEM_EQ(out, exp, 24);
  /* ... -id -t 3 -m 12 -p 2 -l 32 */
  unhex("2e93c004379bd4a80aca990f1db8df34fc4c8f77671940a986d50cd31a6487f5", exp, 32);
  TT_ASSERT_EQ(fl_argon2(FL_ARGON2ID, "password", 8, "somesalt", 8, 3, 4096, 2, out, 32), FL_OK);
  TT_ASSERT_MEM_EQ(out, exp, 32);
}

static void test_argon2_params(void) {
  uint8_t out[32];
  TT_ASSERT_EQ(fl_argon2(FL_ARGON2ID, "p", 1, "somesalt", 8, 1, 4, 1, out, 32), FL_EINVAL);   /* m < 8p */
  TT_ASSERT_EQ(fl_argon2(FL_ARGON2ID, "p", 1, "somesalt", 8, 0, 64, 1, out, 32), FL_EINVAL);  /* t = 0 */
  TT_ASSERT_EQ(fl_argon2(FL_ARGON2ID, "p", 1, "somesalt", 8, 1, 64, 0, out, 32), FL_EINVAL);  /* p = 0 */
  TT_ASSERT_EQ(fl_argon2(FL_ARGON2ID, "p", 1, "s", 1, 1, 64, 1, out, 32), FL_EINVAL);         /* sel trop court */
  TT_ASSERT_EQ(fl_argon2((fl_argon2_type)9, "p", 1, "somesalt", 8, 1, 64, 1, out, 32), FL_EINVAL);
}

static char *lire(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *b = malloc((size_t)n + 1);
  if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
  if (b) b[n] = 0;
  fclose(f);
  return b;
}

static void test_kat_secteurs(void) {
  char *txt = lire(tt_fixture("cipher-kat.json"));
  TT_ASSERT(txt != NULL);
  cJSON *arr = cJSON_Parse(txt);
  free(txt);
  TT_ASSERT(arr != NULL && cJSON_IsArray(arr));
  int n = cJSON_GetArraySize(arr);
  TT_ASSERT(n >= 4);
  for (int i = 0; i < n; i++) {
    cJSON *e = cJSON_GetArrayItem(arr, i);
    char spec[64];
    snprintf(spec, sizeof spec, "aes-%s", cJSON_GetObjectItem(e, "mode")->valuestring);
    uint8_t key[64], *plain = malloc(1 << 14), *ciph = malloc(1 << 14), *buf = malloc(1 << 14);
    size_t klen = unhex(cJSON_GetObjectItem(e, "key")->valuestring, key, sizeof key);
    size_t plen = unhex(cJSON_GetObjectItem(e, "plain")->valuestring, plain, 1 << 14);
    size_t clen = unhex(cJSON_GetObjectItem(e, "cipher")->valuestring, ciph, 1 << 14);
    uint32_t ss = (uint32_t)cJSON_GetObjectItem(e, "sector_size")->valueint;
    uint64_t iv = (uint64_t)cJSON_GetObjectItem(e, "iv_sector")->valuedouble;
    uint64_t step = (uint64_t)cJSON_GetObjectItem(e, "iv_step")->valuedouble;
    fl_sector_cipher *c = NULL;
    if (!(klen && plen && plen == clen && plen % ss == 0)) { free(plain); free(ciph); free(buf); cJSON_Delete(arr); }
    TT_ASSERT(klen && plen && plen == clen && plen % ss == 0);
    TT_ASSERT_EQ(klen * 8, (size_t)cJSON_GetObjectItem(e, "key_bits")->valueint);
    TT_ASSERT_EQ(fl_sector_cipher_new(spec, key, klen, ss, &c), FL_OK);
    for (size_t k = 0; k < plen / ss; k++) {
      TT_ASSERT_EQ(fl_sector_encrypt(c, iv + k * step, plain + k * ss, buf + k * ss, ss), FL_OK);
    }
    TT_ASSERT_MEM_EQ(buf, ciph, plen);
    memset(buf, 0, plen);
    for (size_t k = 0; k < plen / ss; k++) {
      TT_ASSERT_EQ(fl_sector_decrypt(c, iv + k * step, ciph + k * ss, buf + k * ss, ss), FL_OK);
    }
    TT_ASSERT_MEM_EQ(buf, plain, plen);
    if (step == 1) { /* appel multi-secteurs : le secteur k utilise iv + k */
      memset(buf, 0, plen);
      TT_ASSERT_EQ(fl_sector_encrypt(c, iv, plain, buf, plen), FL_OK);
      TT_ASSERT_MEM_EQ(buf, ciph, plen);
      TT_ASSERT_EQ(fl_sector_decrypt(c, iv, ciph, buf, plen), FL_OK);
      TT_ASSERT_MEM_EQ(buf, plain, plen);
      /* en place */
      memcpy(buf, ciph, plen);
      TT_ASSERT_EQ(fl_sector_decrypt(c, iv, buf, buf, plen), FL_OK);
      TT_ASSERT_MEM_EQ(buf, plain, plen);
    }
    fl_sector_cipher_free(c);
    free(plain); free(ciph); free(buf);
  }
  cJSON_Delete(arr);
}

static void test_spec_inconnue(void) {
  uint8_t key[64] = {0};
  fl_sector_cipher *c = NULL;
  TT_ASSERT_EQ(fl_sector_cipher_new("twofish-xts-plain64", key, 64, 512, &c), FL_ENOTSUP);
  TT_ASSERT(c == NULL);
  TT_ASSERT_EQ(fl_sector_cipher_new("aes-xts-plain64", key, 20, 512, &c), FL_EINVAL);
  TT_ASSERT_EQ(fl_sector_cipher_new("aes-xts-plain64", key, 48, 512, &c), FL_EINVAL);
  TT_ASSERT_EQ(fl_sector_cipher_new("aes-cbc-plain64", key, 20, 512, &c), FL_EINVAL);
  TT_ASSERT_EQ(fl_sector_cipher_new("aes-cbc-essiv:sha256", key, 24, 512, &c), FL_OK);
  fl_sector_cipher_free(c);
  TT_ASSERT_EQ(fl_sector_cipher_new("aes-xts-plain64", key, 32, 512, &c), FL_OK);
  uint8_t buf[1024] = {0};
  TT_ASSERT_EQ(fl_sector_encrypt(c, 0, buf, buf, 0), FL_EINVAL);
  TT_ASSERT_EQ(fl_sector_encrypt(c, 0, buf, buf, 500), FL_EINVAL);
  TT_ASSERT_EQ(fl_sector_encrypt(c, 0, buf, buf, 1024), FL_OK);
  fl_sector_cipher_free(c);
  fl_sector_cipher_free(NULL);
  TT_ASSERT_EQ(fl_sector_cipher_new("aes-xts-plain64", key, 64, 100, &c), FL_EINVAL); /* taille de secteur invalide */
}

int main(void) {
  TT_RUN(test_hash);
  TT_RUN(test_crypto_sha512);
  TT_RUN(test_pbkdf2_rfc7914);
  TT_RUN(test_argon2i_reference);
  TT_RUN(test_argon2id_reference);
  TT_RUN(test_argon2_params);
  TT_RUN(test_kat_secteurs);
  TT_RUN(test_spec_inconnue);
  TT_MAIN_END();
}
