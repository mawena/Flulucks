#ifndef FL_LUKS_INTERNAL_H
#define FL_LUKS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fluluks/blockdev.h"
#include "fluluks/luks.h"

/* Fusion anti-forensique (spec LUKS). src = keylen*stripes octets, key = keylen octets. */
int fl_af_merge(const char *hash, const uint8_t *src, size_t keylen, uint32_t stripes, uint8_t *key);

/* Périphérique déchiffrant [data_offset_bytes, +data_size_bytes) de dev (0 = jusqu'à la fin).
 * IV du secteur lba = iv_tweak + lba * (iv_large_sectors ? 1 : sector_size/512).
 * Emprunte dev ; copie la clé dans sa propre mémoire sécurisée. */
int fl_crypt_bdev_new(fl_bdev *dev, uint64_t data_offset_bytes, uint64_t data_size_bytes,
                      const char *cipher_spec, const uint8_t *mk, size_t mklen,
                      uint32_t sector_size, bool iv_large_sectors, uint64_t iv_tweak, fl_bdev **out);

/* Un format (LUKS1, LUKS2) fournit ces opérations. */
typedef struct {
  int (*unlock)(struct fl_luks *l, const void *pass, size_t passlen, fl_bdev **decrypted);
  void (*destroy)(struct fl_luks *l); /* libère impl */
} fl_luks_ops;

struct fl_luks {
  int version;
  fl_bdev *dev;
  char cipher[72];
  const fl_luks_ops *ops;
  void *impl;
};

/* Analyse l'en-tête LUKS1 (hdr = premiers octets du disque, hdr_len >= 592) ; remplit l. */
#define FL_LUKS1_HDR_LEN 592
int fl_luks1_parse(struct fl_luks *l, const uint8_t *hdr);

#endif
