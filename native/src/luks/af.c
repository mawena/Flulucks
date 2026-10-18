#define _DEFAULT_SOURCE
#include <string.h>

#include "crypto/fl_crypto.h"
#include "fluluks/error.h"
#include "fluluks/secure.h"
#include "luks/luks_internal.h"

static int diffuse(const char *hash, size_t hl, uint8_t *buf, size_t len) {
  uint32_t idx = 0;
  uint8_t in[4 + 64], out[64];
  int rc = FL_OK;
  for (size_t off = 0; off < len; off += hl, idx++) {
    size_t n = len - off < hl ? len - off : hl;
    in[0] = (uint8_t)(idx >> 24);
    in[1] = (uint8_t)(idx >> 16);
    in[2] = (uint8_t)(idx >> 8);
    in[3] = (uint8_t)idx;
    memcpy(in + 4, buf + off, n);
    rc = fl_hash(hash, in, 4 + n, out);
    if (rc != FL_OK) break;
    memcpy(buf + off, out, n);
  }
  fl_memzero(in, sizeof in);
  fl_memzero(out, sizeof out);
  return rc;
}

int fl_af_merge(const char *hash, const uint8_t *src, size_t keylen, uint32_t stripes, uint8_t *key) {
  if (!hash || !src || !key || keylen == 0 || stripes == 0) return FL_EINVAL;
  size_t hl = fl_hash_len(hash);
  if (hl == 0) return FL_ENOTSUP;
  uint8_t *d = fl_secure_alloc(keylen);
  if (!d) return FL_ENOMEM;
  int rc = FL_OK;
  for (uint32_t i = 0; i + 1 < stripes; i++) {
    const uint8_t *blk = src + (size_t)i * keylen;
    for (size_t j = 0; j < keylen; j++) d[j] ^= blk[j];
    rc = diffuse(hash, hl, d, keylen);
    if (rc != FL_OK) goto out;
  }
  const uint8_t *last = src + (size_t)(stripes - 1) * keylen;
  for (size_t j = 0; j < keylen; j++) key[j] = d[j] ^ last[j];
out:
  fl_secure_free(d, keylen);
  return rc;
}
