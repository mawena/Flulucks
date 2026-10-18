#define _DEFAULT_SOURCE
#include "crypto/fl_crypto.h"

#include <string.h>
#include <strings.h>

#include "argon2.h"
#include "fluluks/error.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "fluluks/secure.h"

static int md_of(const char *hash, mbedtls_md_type_t *t, size_t *len) {
  if (!hash) return FL_ENOTSUP;
  if (strcmp(hash, "sha1") == 0) { *t = MBEDTLS_MD_SHA1; *len = 20; }
  else if (strcmp(hash, "sha256") == 0) { *t = MBEDTLS_MD_SHA256; *len = 32; }
  else if (strcmp(hash, "sha512") == 0) { *t = MBEDTLS_MD_SHA512; *len = 64; }
  else return FL_ENOTSUP;
  return FL_OK;
}

size_t fl_hash_len(const char *hash) {
  mbedtls_md_type_t t;
  size_t n = 0;
  return md_of(hash, &t, &n) == FL_OK ? n : 0;
}

int fl_hash(const char *hash, const void *data, size_t len, void *out) {
  mbedtls_md_type_t t;
  size_t n;
  if (md_of(hash, &t, &n) != FL_OK) return FL_ENOTSUP;
  if (!out || (!data && len)) return FL_EINVAL;
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(t);
  if (!info) return FL_ENOTSUP;
  return mbedtls_md(info, data, len, out) == 0 ? FL_OK : FL_EIO;
}

int fl_pbkdf2(const char *hash, const void *pw, size_t pwlen, const void *salt, size_t saltlen,
              uint32_t iterations, void *out, size_t outlen) {
  mbedtls_md_type_t t;
  size_t n;
  if (md_of(hash, &t, &n) != FL_OK) return FL_ENOTSUP;
  if (!out || !outlen || iterations == 0 || (!pw && pwlen) || (!salt && saltlen)) return FL_EINVAL;
  int r = mbedtls_pkcs5_pbkdf2_hmac_ext(t, pw, pwlen, salt, saltlen, iterations, (uint32_t)outlen, out);
  if (r != 0) {
    fl_memzero(out, outlen);
    return FL_EINVAL;
  }
  return FL_OK;
}

int fl_argon2(fl_argon2_type t, const void *pw, size_t pwlen, const void *salt, size_t saltlen,
              uint32_t t_cost, uint32_t m_cost_kib, uint32_t parallel, void *out, size_t outlen) {
  if (t != FL_ARGON2I && t != FL_ARGON2ID) return FL_EINVAL;
  if (!out || !outlen || outlen > UINT32_MAX || pwlen > UINT32_MAX || saltlen > UINT32_MAX) return FL_EINVAL;
  if ((!pw && pwlen) || !salt) return FL_EINVAL;
  if (parallel == 0 || m_cost_kib < 8u * parallel) return FL_EINVAL;
  argon2_context ctx;
  memset(&ctx, 0, sizeof ctx);
  ctx.out = out;
  ctx.outlen = (uint32_t)outlen;
  ctx.pwd = (uint8_t *)(uintptr_t)pw;
  ctx.pwdlen = (uint32_t)pwlen;
  ctx.salt = (uint8_t *)(uintptr_t)salt;
  ctx.saltlen = (uint32_t)saltlen;
  ctx.t_cost = t_cost;
  ctx.m_cost = m_cost_kib;
  ctx.lanes = parallel;
  ctx.threads = parallel;
  ctx.version = ARGON2_VERSION_13;
  ctx.flags = ARGON2_DEFAULT_FLAGS; /* la memoire interne est effacee par argon2 (clear_internal_memory) ; mot de passe non modifie */
  int r = argon2_ctx(&ctx, t == FL_ARGON2I ? Argon2_i : Argon2_id);
  if (r == ARGON2_OK) return FL_OK;
  fl_memzero(out, outlen);
  if (r == ARGON2_MEMORY_ALLOCATION_ERROR) return FL_ENOMEM;
  if (r == ARGON2_THREAD_FAIL) return FL_EIO;
  return FL_EINVAL;
}
