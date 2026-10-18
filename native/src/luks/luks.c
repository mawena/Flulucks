#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <string.h>

#include "fluluks/error.h"
#include "luks/luks_internal.h"
#include "fluluks/secure.h"

static const uint8_t k_magic[6] = {'L', 'U', 'K', 'S', 0xBA, 0xBE};

int fl_luks_open(fl_bdev *dev, fl_luks **out) {
  if (!out) return FL_EINVAL;
  *out = NULL;
  if (!dev) return FL_EINVAL;
  uint8_t hdr[FL_LUKS1_HDR_LEN];
  if (fl_bdev_size_bytes(dev) < sizeof hdr) return FL_ENOTSUP;
  int rc = fl_bdev_read_bytes(dev, 0, sizeof hdr, hdr);
  if (rc != FL_OK) return rc;
  bool magic_ok = memcmp(hdr, k_magic, sizeof k_magic) == 0;
  int version = (hdr[6] << 8) | hdr[7];

  fl_luks *l = calloc(1, sizeof *l);
  if (!l) return FL_ENOMEM;
  l->dev = dev;
  if (magic_ok && version == 1) {
    l->version = 1;
    rc = fl_luks1_parse(l, hdr);
  } else {
    /* LUKS2, ou primaire illisible (magie/version abîmées) : l'en-tête secondaire peut le sauver.
     * ENOTSUP seulement si aucun en-tête SKUL valide n'existe. */
    l->version = 2;
    rc = fl_luks2_open(l, magic_ok && version == 2);
  }
  if (rc != FL_OK) {
    fl_luks_close(l);
    return rc;
  }
  *out = l;
  return FL_OK;
}

int fl_luks_version(const fl_luks *l) { return l ? l->version : 0; }

const char *fl_luks_cipher(const fl_luks *l) { return l ? l->cipher : NULL; }

int fl_luks_max_kdf_memory_kib(const fl_luks *l, uint32_t *out) {
  if (!l || !out) return FL_EINVAL;
  *out = l->version == 2 && l->impl ? fl_luks2_max_kdf_memory_kib(l) : 0; /* LUKS1 : PBKDF2 seulement */
  return FL_OK;
}

int fl_luks_unlock(fl_luks *l, const void *pass, size_t passlen, fl_bdev **decrypted) {
  if (!decrypted) return FL_EINVAL;
  *decrypted = NULL;
  if (!l || !l->ops || (!pass && passlen)) return FL_EINVAL;
  return l->ops->unlock(l, pass, passlen, decrypted);
}

void fl_luks_close(fl_luks *l) {
  if (!l) return;
  if (l->ops && l->ops->destroy) l->ops->destroy(l);
  fl_memzero(l, sizeof *l);
  free(l);
}
