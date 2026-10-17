#ifndef FL_LUKS2_INTERNAL_H
#define FL_LUKS2_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define L2_MAX_SLOTS 64
#define L2_MAX_DIGESTS 64
#define L2_MAX_SALT 128
#define L2_MAX_DIGEST 64
#define L2_NAME 72

enum { L2_KDF_PBKDF2 = 1, L2_KDF_ARGON2I, L2_KDF_ARGON2ID };

typedef struct {
  bool supported; /* type/KDF/hash/chiffrement pris en charge (sinon slot ignoré) */
  int priority; /* 1 normal, 2 haute (0 = jamais : conservé mais ignoré) */
  uint32_t key_size;
  uint32_t stripes;
  char af_hash[32];
  uint64_t area_off, area_size;
  char area_enc[L2_NAME];
  uint32_t area_key_size;
  int kdf;
  char kdf_hash[32];
  uint32_t time, memory, cpus, iterations;
  uint8_t salt[L2_MAX_SALT];
  size_t salt_len;
  int digest; /* indice du digest lié (qui liste aussi le segment 0), -1 sinon */
} l2_slot;

typedef struct {
  char hash[32];
  uint32_t iterations;
  uint8_t salt[L2_MAX_SALT];
  size_t salt_len;
  uint8_t digest[L2_MAX_DIGEST];
  size_t digest_len;
} l2_digest;

typedef struct {
  uint64_t seg_off;
  uint64_t seg_size; /* 0 = dynamique (jusqu'à la fin) */
  uint64_t iv_tweak;
  uint32_t sector_size;
  char seg_enc[L2_NAME];
  int nslots, ndigests;
  l2_slot slots[L2_MAX_SLOTS]; /* triés : priorité décroissante */
  l2_digest digests[L2_MAX_DIGESTS];
} luks2;

/* Analyse la zone JSON (terminée par NUL) ; dev_size en octets. */
int fl_luks2_parse_json(const char *json, uint64_t dev_size, luks2 *out);

#endif
