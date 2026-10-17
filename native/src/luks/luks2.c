#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <string.h>

#include "crypto/fl_crypto.h"
#include "fluluks/error.h"
#include "fluluks/secure.h"
#include "luks/luks2_internal.h"
#include "luks/luks_internal.h"

#define BIN_HDR 4096u
#define HDR_MIN (16u * 1024)
#define HDR_MAX (4u * 1024 * 1024)

static const uint8_t k_primary[6] = {'L', 'U', 'K', 'S', 0xBA, 0xBE};
static const uint8_t k_secondary[6] = {'S', 'K', 'U', 'L', 0xBA, 0xBE};
static const uint64_t k_sec_offsets[] = {0x4000, 0x8000, 0x10000, 0x20000, 0x40000,
                                         0x80000, 0x100000, 0x200000, 0x400000};

static uint64_t be64(const uint8_t *p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
  return v;
}

typedef struct {
  uint8_t *buf; /* hdr_size octets, vérifiés */
  size_t size;
  uint64_t seqid;
} hdr_img;

/* Charge et vérifie (magic, version, taille, somme de contrôle, zone JSON) l'en-tête à `off`. */
static int load_hdr(fl_bdev *dev, uint64_t off, bool secondary, hdr_img *out) {
  uint64_t dev_size = fl_bdev_size_bytes(dev);
  uint8_t bin[BIN_HDR];
  memset(out, 0, sizeof *out);
  if (off > dev_size || dev_size - off < BIN_HDR) return FL_ECORRUPT;
  int rc = fl_bdev_read_bytes(dev, off, BIN_HDR, bin);
  if (rc != FL_OK) return rc;
  if (memcmp(bin, secondary ? k_secondary : k_primary, 6) != 0) return FL_ECORRUPT;
  if (((bin[6] << 8) | bin[7]) != 2) return FL_ECORRUPT;
  uint64_t hs = be64(bin + 8);
  if (hs < HDR_MIN || hs > HDR_MAX || hs % 512 || hs > dev_size - off) return FL_ECORRUPT;
  if (be64(bin + 256) != off) return FL_ECORRUPT;
  if (!memchr(bin + 72, 0, 32)) return FL_ECORRUPT;
  if (strcmp((const char *)bin + 72, "sha256") != 0) return FL_ENOTSUP;
  uint8_t *buf = malloc((size_t)hs);
  if (!buf) return FL_ENOMEM;
  rc = fl_bdev_read_bytes(dev, off, (size_t)hs, buf);
  if (rc != FL_OK) {
    free(buf);
    return rc;
  }
  uint8_t want[32], got[32];
  memcpy(want, buf + 448, 32);
  memset(buf + 448, 0, 64);
  rc = fl_hash("sha256", buf, (size_t)hs, got);
  memcpy(buf + 448, want, 32); /* la zone csum n'est pas modifiée au final */
  if (rc != FL_OK || memcmp(want, got, 32) != 0 || !memchr(buf + BIN_HDR, 0, (size_t)hs - BIN_HDR)) {
    free(buf);
    return rc != FL_OK ? rc : FL_ECORRUPT;
  }
  out->buf = buf;
  out->size = (size_t)hs;
  out->seqid = be64(buf + 16);
  return FL_OK;
}

static const fl_luks_ops luks2_ops;

int fl_luks2_open(struct fl_luks *l, bool primary_ok) {
  hdr_img pri, sec;
  int rp = load_hdr(l->dev, 0, false, &pri);
  if (rp == FL_ENOMEM) return rp;

  /* l'en-tête secondaire se trouve à hdr_size ; sinon on essaie les décalages usuels */
  uint64_t offs[1 + sizeof k_sec_offsets / sizeof k_sec_offsets[0]];
  size_t no = 0;
  uint8_t bin[BIN_HDR];
  if (fl_bdev_size_bytes(l->dev) >= BIN_HDR && fl_bdev_read_bytes(l->dev, 0, BIN_HDR, bin) == FL_OK) {
    uint64_t hs = be64(bin + 8);
    if (hs >= HDR_MIN && hs <= HDR_MAX) offs[no++] = hs;
  }
  for (size_t i = 0; i < sizeof k_sec_offsets / sizeof k_sec_offsets[0]; i++) {
    bool dup = false;
    for (size_t j = 0; j < no; j++) dup |= offs[j] == k_sec_offsets[i];
    if (!dup) offs[no++] = k_sec_offsets[i];
  }
  int rs = FL_ECORRUPT;
  bool io_error = rp == FL_EIO;
  memset(&sec, 0, sizeof sec);
  for (size_t i = 0; i < no; i++) {
    rs = load_hdr(l->dev, offs[i], true, &sec);
    if (rs == FL_OK || rs == FL_ENOMEM) break;
    if (rs == FL_EIO) io_error = true; /* un secteur illisible n'interdit pas les autres décalages */
  }
  if (rs == FL_ENOMEM) {
    free(pri.buf);
    return rs;
  }

  /* ordre d'essai : plus grand seqid d'abord (égalité : primaire) */
  hdr_img *order[2];
  int n = 0;
  if (rp == FL_OK && rs == FL_OK) {
    order[n++] = sec.seqid > pri.seqid ? &sec : &pri;
    order[n++] = sec.seqid > pri.seqid ? &pri : &sec;
  } else if (rp == FL_OK) {
    order[n++] = &pri;
  } else if (rs == FL_OK) {
    order[n++] = &sec;
  }
  int rc;
  if (n > 0)
    rc = FL_ECORRUPT;
  else if (rp == FL_ENOTSUP)
    rc = FL_ENOTSUP;
  else if (io_error)
    rc = FL_EIO;
  else
    rc = primary_ok ? FL_ECORRUPT : FL_ENOTSUP;
  luks2 *x = n ? calloc(1, sizeof *x) : NULL;
  if (n && !x) rc = FL_ENOMEM;
  for (int i = 0; x && i < n; i++) {
    const char *json = (const char *)order[i]->buf + BIN_HDR;
    rc = fl_luks2_parse_json(json, fl_bdev_size_bytes(l->dev), x);
    /* on ne se rabat sur l'autre en-tête que si celui-ci est corrompu : un en-tête valide mais non
     * pris en charge (ex. réchiffrement en cours) ne doit jamais céder la place à des métadonnées périmées */
    if (rc != FL_ECORRUPT) break;
  }
  free(pri.buf);
  free(sec.buf);
  if (rc != FL_OK) {
    free(x);
    return rc;
  }
  memcpy(l->cipher, x->seg_enc, sizeof l->cipher);
  l->ops = &luks2_ops;
  l->impl = x;
  return FL_OK;
}

static bool equal(const uint8_t *a, const uint8_t *b, size_t n) { return memcmp(a, b, n) == 0; }

static size_t round512(size_t n) { return (n + 511) & ~(size_t)511; }

/* FL_OK = bon slot (mk rempli) ; FL_EBADPASS = slot suivant ; autre = erreur fatale. */
static int try_slot(const fl_luks *l, const luks2 *x, const l2_slot *s, const void *pass, size_t passlen,
                    uint8_t *mk) {
  size_t kb = s->area_key_size, mkl = s->key_size;
  size_t alen = round512(mkl * s->stripes);
  uint8_t *k = fl_secure_alloc(kb);
  uint8_t *raw = fl_secure_alloc(alen);
  uint8_t *plain = fl_secure_alloc(alen);
  fl_sector_cipher *c = NULL;
  int rc;
  if (!k || !raw || !plain) {
    rc = FL_ENOMEM;
    goto out;
  }
  if (s->kdf == L2_KDF_PBKDF2)
    rc = fl_pbkdf2(s->kdf_hash, pass, passlen, s->salt, s->salt_len, s->iterations, k, kb);
  else
    rc = fl_argon2(s->kdf == L2_KDF_ARGON2ID ? FL_ARGON2ID : FL_ARGON2I, pass, passlen, s->salt, s->salt_len,
                   s->time, s->memory, s->cpus, k, kb);
  if (rc == FL_EINVAL) rc = FL_ECORRUPT;
  if (rc != FL_OK) goto out;
  rc = fl_bdev_read_bytes(l->dev, s->area_off, alen, raw);
  if (rc != FL_OK) goto out;
  rc = fl_sector_cipher_new(s->area_enc, k, kb, 512, &c);
  if (rc == FL_EINVAL) rc = FL_ECORRUPT;
  if (rc != FL_OK) goto out;
  rc = fl_sector_decrypt(c, 0, raw, plain, alen);
  if (rc != FL_OK) goto out;
  rc = fl_af_merge(s->af_hash, plain, mkl, s->stripes, mk);
  if (rc != FL_OK) goto out;
  const l2_digest *d = &x->digests[s->digest];
  uint8_t dg[L2_MAX_DIGEST];
  rc = fl_pbkdf2(d->hash, mk, mkl, d->salt, d->salt_len, d->iterations, dg, d->digest_len);
  if (rc == FL_OK) rc = equal(dg, d->digest, d->digest_len) ? FL_OK : FL_EBADPASS;
out:
  fl_sector_cipher_free(c);
  fl_secure_free(k, kb);
  fl_secure_free(raw, alen);
  fl_secure_free(plain, alen);
  return rc;
}

/* Pertinence d'une erreur de slot, pour le rapport final : mémoire/E-S > mauvaise phrase > corrompu. */
static int err_rank(int rc) {
  if (rc == FL_ENOMEM) return 4;
  if (rc == FL_EIO) return 3;
  if (rc == FL_EBADPASS) return 2;
  return 1;
}

static int luks2_unlock(struct fl_luks *l, const void *pass, size_t passlen, fl_bdev **decrypted) {
  const luks2 *x = l->impl;
  uint8_t *mk = fl_secure_alloc(64);
  if (!mk) return FL_ENOMEM;
  static const uint8_t none = 0;
  if (!pass) pass = &none;
  int best = 0;
  bool ok = false;
  size_t mkl = 0;
  for (int i = 0; i < x->nslots; i++) {
    const l2_slot *s = &x->slots[i];
    if (!s->supported || s->priority == 0 || s->digest < 0) continue;
    int rc = try_slot(l, x, s, pass, passlen, mk);
    if (rc == FL_OK) {
      mkl = s->key_size;
      ok = true;
      break;
    }
    if (!best || err_rank(rc) > err_rank(best)) best = rc; /* on continue avec les slots suivants */
  }
  int rc = best ? best : FL_EBADPASS;
  if (ok) {
    rc = fl_crypt_bdev_new(l->dev, x->seg_off, x->seg_size, x->seg_enc, mk, mkl, x->sector_size,
                           false /* IV en unités de 512 octets, comme cryptsetup */, x->iv_tweak, decrypted);
    if (rc != FL_OK) *decrypted = NULL;
  }
  fl_secure_free(mk, 64);
  return rc;
}

static void luks2_destroy(struct fl_luks *l) {
  if (l->impl) explicit_bzero(l->impl, sizeof(luks2));
  free(l->impl);
  l->impl = NULL;
}

static const fl_luks_ops luks2_ops = {luks2_unlock, luks2_destroy};
