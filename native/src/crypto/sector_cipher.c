#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "crypto/fl_crypto.h"
#include "fluluks/error.h"
#include "fluluks/secure.h"
#include "mbedtls/aes.h"
#include "mbedtls/md.h"

typedef enum { M_XTS, M_CBC_PLAIN64, M_CBC_ESSIV } fl_sc_mode;

struct fl_sector_cipher {
  fl_sc_mode mode;
  uint32_t sector_size;
  mbedtls_aes_xts_context xts_enc, xts_dec;
  mbedtls_aes_context cbc_enc, cbc_dec;
  mbedtls_aes_context essiv; /* AES-ECB, cle = SHA-256(cle volume) */
};

static void put_le64(uint8_t iv[16], uint64_t n) {
  memset(iv, 0, 16);
  for (int i = 0; i < 8; i++) iv[i] = (uint8_t)(n >> (8 * i));
}

static void wipe_free(fl_sector_cipher *c) {
  if (!c) return;
  mbedtls_aes_xts_free(&c->xts_enc);
  mbedtls_aes_xts_free(&c->xts_dec);
  mbedtls_aes_free(&c->cbc_enc);
  mbedtls_aes_free(&c->cbc_dec);
  mbedtls_aes_free(&c->essiv);
  fl_secure_free(c, sizeof *c);
}

int fl_sector_cipher_new(const char *spec, const uint8_t *key, size_t keylen,
                         uint32_t sector_size, fl_sector_cipher **out) {
  if (!out) return FL_EINVAL;
  *out = NULL;
  if (!spec) return FL_EINVAL;
  fl_sc_mode mode;
  if (strcmp(spec, "aes-xts-plain64") == 0) mode = M_XTS;
  else if (strcmp(spec, "aes-cbc-plain64") == 0) mode = M_CBC_PLAIN64;
  else if (strcmp(spec, "aes-cbc-essiv:sha256") == 0) mode = M_CBC_ESSIV;
  else return FL_ENOTSUP;

  if (!key) return FL_EINVAL;
  if (mode == M_XTS) {
    if (keylen != 32 && keylen != 64) return FL_EINVAL;
  } else if (keylen != 16 && keylen != 24 && keylen != 32) {
    return FL_EINVAL;
  }
  if (sector_size < 16 || sector_size % 16 != 0) return FL_EINVAL;

  fl_sector_cipher *c = fl_secure_alloc(sizeof *c);
  if (!c) return FL_ENOMEM;
  c->mode = mode;
  c->sector_size = sector_size;
  mbedtls_aes_xts_init(&c->xts_enc);
  mbedtls_aes_xts_init(&c->xts_dec);
  mbedtls_aes_init(&c->cbc_enc);
  mbedtls_aes_init(&c->cbc_dec);
  mbedtls_aes_init(&c->essiv);

  int bad = 0;
  if (mode == M_XTS) {
    bad = mbedtls_aes_xts_setkey_enc(&c->xts_enc, key, (unsigned)keylen * 8) ||
          mbedtls_aes_xts_setkey_dec(&c->xts_dec, key, (unsigned)keylen * 8);
  } else {
    bad = mbedtls_aes_setkey_enc(&c->cbc_enc, key, (unsigned)keylen * 8) ||
          mbedtls_aes_setkey_dec(&c->cbc_dec, key, (unsigned)keylen * 8);
    if (!bad && mode == M_CBC_ESSIV) {
      uint8_t h[32];
      bad = mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, keylen, h) ||
            mbedtls_aes_setkey_enc(&c->essiv, h, 256);
      explicit_bzero(h, sizeof h);
    }
  }
  if (bad) {
    wipe_free(c);
    return FL_EIO;
  }
  *out = c;
  return FL_OK;
}

static int sector_op(fl_sector_cipher *c, int enc, uint64_t iv_sector, const void *in, void *out,
                     size_t len) {
  if (!c || !in || !out) return FL_EINVAL;
  if (len == 0 || len % c->sector_size != 0) return FL_EINVAL;
  const uint8_t *src = in;
  uint8_t *dst = out;
  size_t n = len / c->sector_size;
  for (size_t k = 0; k < n; k++) {
    uint8_t iv[16];
    uint64_t num = iv_sector + k;
    put_le64(iv, num);
    int r;
    if (c->mode == M_XTS) {
      r = mbedtls_aes_crypt_xts(enc ? &c->xts_enc : &c->xts_dec, enc ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
                                c->sector_size, iv, src, dst);
    } else {
      if (c->mode == M_CBC_ESSIV) {
        uint8_t e[16];
        r = mbedtls_aes_crypt_ecb(&c->essiv, MBEDTLS_AES_ENCRYPT, iv, e);
        memcpy(iv, e, 16);
        explicit_bzero(e, sizeof e);
        if (r) return FL_EIO;
      }
      r = mbedtls_aes_crypt_cbc(enc ? &c->cbc_enc : &c->cbc_dec, enc ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
                                c->sector_size, iv, src, dst);
    }
    if (r) return FL_EIO;
    src += c->sector_size;
    dst += c->sector_size;
  }
  return FL_OK;
}

int fl_sector_decrypt(fl_sector_cipher *c, uint64_t iv_sector, const void *in, void *out, size_t len) {
  return sector_op(c, 0, iv_sector, in, out, len);
}

int fl_sector_encrypt(fl_sector_cipher *c, uint64_t iv_sector, const void *in, void *out, size_t len) {
  return sector_op(c, 1, iv_sector, in, out, len);
}

void fl_sector_cipher_free(fl_sector_cipher *c) { wipe_free(c); }
