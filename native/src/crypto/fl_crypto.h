#ifndef FL_CRYPTO_H
#define FL_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

/* "sha1"->20, "sha256"->32, "sha512"->64, inconnu->0 */
size_t fl_hash_len(const char *hash);
int fl_hash(const char *hash, const void *data, size_t len, void *out);
int fl_pbkdf2(const char *hash, const void *pw, size_t pwlen, const void *salt, size_t saltlen,
              uint32_t iterations, void *out, size_t outlen);

typedef enum { FL_ARGON2I = 1, FL_ARGON2ID = 2 } fl_argon2_type;
/* Version 0x13. FL_ENOMEM si l'allocation echoue, FL_EINVAL pour des parametres invalides. */
int fl_argon2(fl_argon2_type t, const void *pw, size_t pwlen, const void *salt, size_t saltlen,
              uint32_t t_cost, uint32_t m_cost_kib, uint32_t parallel, void *out, size_t outlen);

typedef struct fl_sector_cipher fl_sector_cipher;
/* spec: "aes-xts-plain64", "aes-cbc-essiv:sha256", "aes-cbc-plain64".
 * Spec inconnue -> FL_ENOTSUP ; mauvaise longueur de cle -> FL_EINVAL. */
int fl_sector_cipher_new(const char *spec, const uint8_t *key, size_t keylen,
                         uint32_t sector_size, fl_sector_cipher **out);
/* len multiple de sector_size ; le secteur k du tampon utilise l'IV iv_sector + k. */
int fl_sector_decrypt(fl_sector_cipher *c, uint64_t iv_sector, const void *in, void *out, size_t len);
int fl_sector_encrypt(fl_sector_cipher *c, uint64_t iv_sector, const void *in, void *out, size_t len);
void fl_sector_cipher_free(fl_sector_cipher *c); /* efface les cles */

#endif
