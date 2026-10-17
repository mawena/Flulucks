#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <string.h>

#include "crypto/fl_crypto.h"
#include "fluluks/error.h"
#include "fluluks/secure.h"
#include "luks/luks_internal.h"

#define SLOTS 8
#define SLOT_ACTIVE 0x00AC71F3u
#define SLOT_INACTIVE 0x0000DEADu
#define MAX_STRIPES 65536u
#define DIGEST_LEN 20
#define MAX_PBKDF2_ITER (1u << 27) /* même plafond que LUKS2 : évite un déni de service par en-tête forgé */

typedef struct {
  uint32_t iterations;
  uint8_t salt[32];
  uint32_t kmo; /* secteurs de 512 */
  uint32_t stripes;
  bool unsupported; /* itérations au-dessus du plafond : slot non essayé */
} slot1;

typedef struct {
  char hash[32];
  uint64_t payload_off; /* octets */
  uint32_t key_bytes;
  uint8_t digest[DIGEST_LEN];
  uint8_t digest_salt[32];
  uint32_t digest_iter;
  int nslots;
  slot1 slots[SLOTS]; /* actifs et valides seulement */
} luks1;

static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static bool terminated(const uint8_t *f, size_t n) { return memchr(f, 0, n) != NULL; }

static size_t round512(size_t n) { return (n + 511) & ~(size_t)511; }

static const fl_luks_ops luks1_ops;

int fl_luks1_parse(struct fl_luks *l, const uint8_t *h) {
  if (!terminated(h + 8, 32) || !terminated(h + 40, 32) || !terminated(h + 72, 32)) return FL_ECORRUPT;
  uint32_t key_bytes = be32(h + 108);
  if (key_bytes != 16 && key_bytes != 32 && key_bytes != 64) return FL_ECORRUPT;
  uint32_t digest_iter = be32(h + 164);
  if (digest_iter == 0) return FL_ECORRUPT;
  if (digest_iter > MAX_PBKDF2_ITER) return FL_ENOTSUP;
  uint64_t size = fl_bdev_size_bytes(l->dev);
  uint64_t payload = (uint64_t)be32(h + 104) * 512;
  if (payload >= size) return FL_ECORRUPT;
  if (fl_hash_len((const char *)h + 72) == 0) return FL_ENOTSUP;
  const char *name = (const char *)h + 8, *mode = (const char *)h + 40;
  if (!*name || !*mode || strlen(name) + strlen(mode) + 2 > sizeof l->cipher) return FL_ECORRUPT;

  luks1 *x = calloc(1, sizeof *x);
  if (!x) return FL_ENOMEM;
  strcpy(x->hash, (const char *)h + 72);
  x->payload_off = payload;
  x->key_bytes = key_bytes;
  memcpy(x->digest, h + 112, DIGEST_LEN);
  memcpy(x->digest_salt, h + 132, 32);
  x->digest_iter = digest_iter;
  for (int i = 0; i < SLOTS; i++) {
    const uint8_t *s = h + 208 + 48 * i;
    uint32_t active = be32(s);
    if (active == SLOT_INACTIVE) continue;
    if (active != SLOT_ACTIVE) continue; /* valeur inconnue : slot ignoré */
    slot1 sl;
    sl.unsupported = false;
    sl.iterations = be32(s + 4);
    memcpy(sl.salt, s + 8, 32);
    sl.kmo = be32(s + 40);
    sl.stripes = be32(s + 44);
    if (sl.iterations == 0 || sl.stripes == 0 || sl.stripes > MAX_STRIPES) {
      free(x);
      return FL_ECORRUPT;
    }
    uint64_t start = (uint64_t)sl.kmo * 512;
    uint64_t len = round512((size_t)key_bytes * sl.stripes);
    if (start > size || len > size - start) {
      free(x);
      return FL_ECORRUPT;
    }
    sl.unsupported = sl.iterations > MAX_PBKDF2_ITER;
    x->slots[x->nslots++] = sl;
  }
  size_t nl = strlen(name), ml = strlen(mode);
  memcpy(l->cipher, name, nl);
  l->cipher[nl] = '-';
  memcpy(l->cipher + nl + 1, mode, ml + 1);
  l->ops = &luks1_ops;
  l->impl = x;
  return FL_OK;
}

static bool ct_equal(const uint8_t *a, const uint8_t *b, size_t n) {
  uint8_t acc = 0;
  for (size_t i = 0; i < n; i++) acc |= (uint8_t)(a[i] ^ b[i]);
  return acc == 0;
}

/* FL_OK = bon slot (mk rempli) ; FL_EBADPASS ou autre erreur = slot suivant (l'appelant classe les erreurs). */
static int try_slot(const fl_luks *l, const luks1 *x, const slot1 *s, const void *pass, size_t passlen,
                    uint8_t *mk) {
  size_t kb = x->key_bytes;
  size_t km_len = round512(kb * s->stripes);
  uint8_t *k = fl_secure_alloc(kb);
  uint8_t *raw = fl_secure_alloc(km_len);
  uint8_t *plain = fl_secure_alloc(km_len);
  fl_sector_cipher *c = NULL;
  int rc;
  if (!k || !raw || !plain) {
    rc = FL_ENOMEM;
    goto out;
  }
  rc = fl_pbkdf2(x->hash, pass, passlen, s->salt, sizeof s->salt, s->iterations, k, kb);
  if (rc != FL_OK) goto out;
  rc = fl_bdev_read_bytes(l->dev, (uint64_t)s->kmo * 512, km_len, raw);
  if (rc != FL_OK) goto out;
  rc = fl_sector_cipher_new(l->cipher, k, kb, 512, &c);
  if (rc == FL_EINVAL) rc = FL_ECORRUPT;
  if (rc != FL_OK) goto out;
  rc = fl_sector_decrypt(c, 0, raw, plain, km_len);
  if (rc != FL_OK) goto out;
  rc = fl_af_merge(x->hash, plain, kb, s->stripes, mk);
  if (rc != FL_OK) goto out;
  uint8_t dg[DIGEST_LEN];
  rc = fl_pbkdf2(x->hash, mk, kb, x->digest_salt, sizeof x->digest_salt, x->digest_iter, dg, sizeof dg);
  if (rc == FL_OK) rc = ct_equal(dg, x->digest, DIGEST_LEN) ? FL_OK : FL_EBADPASS;
out:
  fl_sector_cipher_free(c);
  fl_secure_free(k, kb);
  fl_secure_free(raw, km_len);
  fl_secure_free(plain, km_len);
  return rc;
}

/* Pertinence d'une erreur de slot (même ordre que LUKS2) : mémoire > E/S > mauvaise phrase > autre. */
static int err_rank(int rc) {
  if (rc == FL_ENOMEM) return 4;
  if (rc == FL_EIO) return 3;
  if (rc == FL_EBADPASS) return 2;
  return 1;
}

static int luks1_unlock(struct fl_luks *l, const void *pass, size_t passlen, fl_bdev **decrypted) {
  const luks1 *x = l->impl;
  uint8_t *mk = fl_secure_alloc(x->key_bytes);
  if (!mk) return FL_ENOMEM;
  int rc = FL_EBADPASS, best = 0;
  bool ok = false;
  for (int i = 0; i < x->nslots; i++) {
    int r = x->slots[i].unsupported ? FL_ENOTSUP : try_slot(l, x, &x->slots[i], pass, passlen, mk);
    if (r == FL_OK) {
      ok = true;
      break;
    }
    /* erreur d'un slot : on essaie les suivants, on rapporte la plus pertinente */
    if (!best || err_rank(r) > err_rank(best)) best = r;
  }
  if (!ok && best) rc = best;
  if (ok) {
    rc = fl_crypt_bdev_new(l->dev, x->payload_off, 0, l->cipher, mk, x->key_bytes, 512, false, 0, decrypted);
    if (rc != FL_OK) *decrypted = NULL;
  }
  fl_secure_free(mk, x->key_bytes);
  return rc;
}

static void luks1_destroy(struct fl_luks *l) {
  if (l->impl) explicit_bzero(l->impl, sizeof(luks1));
  free(l->impl);
  l->impl = NULL;
}

static const fl_luks_ops luks1_ops = {luks1_unlock, luks1_destroy};
