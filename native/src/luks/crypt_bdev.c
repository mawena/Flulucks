#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <string.h>

#include "crypto/fl_crypto.h"
#include "fluluks/error.h"
#include "luks/luks_internal.h"
#include "fluluks/secure.h"

#define CHUNK_BYTES (1u << 20)

typedef struct {
  fl_bdev *parent;
  fl_sector_cipher *cipher;
  uint64_t first_psec; /* premier secteur du parent */
  uint32_t ratio;      /* secteurs parent par secteur déchiffré */
  uint32_t iv_step;
  uint64_t iv_tweak;
} cb_priv;

static uint32_t chunk_secs(const fl_bdev *d) {
  uint32_t n = CHUNK_BYTES / d->sector_size;
  return n ? n : 1;
}

static int cb_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  cb_priv *p = d->priv;
  uint8_t *out = buf;
  uint32_t cs = chunk_secs(d);
  while (count) {
    uint32_t n = count < cs ? count : cs;
    int rc = fl_bdev_read(p->parent, p->first_psec + lba * p->ratio, n * p->ratio, out);
    if (rc != FL_OK) return rc;
    if (p->iv_step == 1) {
      rc = fl_sector_decrypt(p->cipher, p->iv_tweak + lba, out, out, (size_t)n * d->sector_size);
      if (rc != FL_OK) return rc;
    } else {
      for (uint32_t k = 0; k < n; k++) {
        rc = fl_sector_decrypt(p->cipher, p->iv_tweak + (lba + k) * p->iv_step,
                               out + (size_t)k * d->sector_size, out + (size_t)k * d->sector_size,
                               d->sector_size);
        if (rc != FL_OK) return rc;
      }
    }
    lba += n;
    count -= n;
    out += (size_t)n * d->sector_size;
  }
  return FL_OK;
}

static int cb_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf) {
  cb_priv *p = d->priv;
  uint32_t cs = chunk_secs(d);
  if (cs > count) cs = count;
  uint8_t *tmp = malloc((size_t)cs * d->sector_size);
  if (!tmp) return FL_ENOMEM;
  const uint8_t *in = buf;
  int rc = FL_OK;
  while (count && rc == FL_OK) {
    uint32_t n = count < cs ? count : cs;
    for (uint32_t k = 0; k < n && rc == FL_OK; k++)
      rc = fl_sector_encrypt(p->cipher, p->iv_tweak + (lba + k) * p->iv_step,
                             in + (size_t)k * d->sector_size, tmp + (size_t)k * d->sector_size,
                             d->sector_size);
    if (rc == FL_OK) rc = fl_bdev_write(p->parent, p->first_psec + lba * p->ratio, n * p->ratio, tmp);
    lba += n;
    count -= n;
    in += (size_t)n * d->sector_size;
  }
  fl_memzero(tmp, (size_t)cs * d->sector_size);
  free(tmp);
  return rc;
}

static int cb_flush(fl_bdev *d) { return fl_bdev_flush(((cb_priv *)d->priv)->parent); }

/* Ne ferme jamais le parent. */
static void cb_destroy(fl_bdev *d) {
  if (!d) return;
  cb_priv *p = d->priv;
  if (p) {
    fl_sector_cipher_free(p->cipher);
    free(p);
  }
  free(d);
}

static const fl_bdev_ops cb_ops = {cb_read, cb_write, cb_flush, cb_destroy};

int fl_crypt_bdev_new(fl_bdev *dev, uint64_t data_offset_bytes, uint64_t data_size_bytes,
                      const char *cipher_spec, const uint8_t *mk, size_t mklen,
                      uint32_t sector_size, bool iv_large_sectors, uint64_t iv_tweak, fl_bdev **out) {
  if (!out) return FL_EINVAL;
  *out = NULL;
  if (!dev || !cipher_spec || !mk || dev->sector_size == 0) return FL_EINVAL;
  if (sector_size < 512 || sector_size > 4096 || (sector_size & (sector_size - 1))) return FL_EINVAL;
  if (sector_size % dev->sector_size != 0 || data_offset_bytes % dev->sector_size != 0) return FL_EINVAL;
  uint64_t size = fl_bdev_size_bytes(dev);
  if (data_offset_bytes > size) return FL_EINVAL;
  uint64_t avail = size - data_offset_bytes;
  if (data_size_bytes == 0) data_size_bytes = avail;
  else if (data_size_bytes > avail) return FL_EINVAL;

  fl_sector_cipher *c = NULL;
  int rc = fl_sector_cipher_new(cipher_spec, mk, mklen, sector_size, &c);
  if (rc != FL_OK) return rc;
  fl_bdev *d = calloc(1, sizeof *d);
  cb_priv *p = calloc(1, sizeof *p);
  if (!d || !p) {
    free(d);
    free(p);
    fl_sector_cipher_free(c);
    return FL_ENOMEM;
  }
  p->parent = dev;
  p->cipher = c;
  p->first_psec = data_offset_bytes / dev->sector_size;
  p->ratio = sector_size / dev->sector_size;
  p->iv_step = iv_large_sectors ? 1 : sector_size / 512;
  p->iv_tweak = iv_tweak;
  d->ops = &cb_ops;
  d->sector_size = sector_size;
  d->sector_count = data_size_bytes / sector_size;
  d->read_only = dev->read_only;
  d->priv = p;
  *out = d;
  return FL_OK;
}
