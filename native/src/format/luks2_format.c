/* En-tête LUKS2 (phase 8) : ce qu'écrit `cryptsetup luksFormat --type luks2` d'Ubuntu, avec un seul
 * emplacement de clé.
 *
 * Disposition : en-tête primaire à 0 et secondaire à 16 Kio (16 Kio chacun : 4 Kio binaires + 12 Kio de
 * JSON), zone des emplacements de 32 Kio à 16 Mio (emplacement 0 : 4 000 bandes AF de 64 octets, arrondies
 * à 4 Kio), données à 16 Mio jusqu'à la fin. Tout ce qui précède les données est d'abord mis à zéro.
 * Aléa (clé maîtresse, sels, UUID, bandes AF) : /dev/urandom par fl_random. Clé maîtresse, clé de
 * l'emplacement et bandes en clair en mémoire sécurisée, effacées sur tous les chemins. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto/fl_crypto.h"
#include "fluluks/error.h"
#include "fluluks/format.h"
#include "fluluks/secure.h"
#include "format/format_internal.h"
#include "luks/luks_internal.h"

#define MIB (1024ull * 1024ull)
#define HDR 16384u
#define BIN 4096u
#define JSON_SIZE (HDR - BIN)
#define AREA_OFF (2u * HDR)
#define DATA_OFF (16 * MIB)
#define KEY 64u
#define STRIPES 4000u
#define AF_LEN (KEY * STRIPES)                 /* 256 000 */
#define AREA_LEN ((AF_LEN + 4095u) & ~4095u)    /* 258 048 */
#define SALT 32u
#define DIGEST_ITER 1000u
#define CIPHER "aes-xts-plain64"

static void be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void be64(uint8_t *p, uint64_t v) {
  for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (56 - 8 * i));
}

static void b64(const uint8_t *in, size_t n, char *out) {
  static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = t[v >> 18 & 63];
    out[o++] = t[v >> 12 & 63];
    out[o++] = i + 1 < n ? t[v >> 6 & 63] : '=';
    out[o++] = i + 2 < n ? t[v & 63] : '=';
  }
  out[o] = 0;
}

/* En-tête complet (binaire + JSON) à `off`, avec sa somme SHA-256. */
static int write_hdr(fl_bdev *part, uint8_t *h, const char *json, const uint8_t salt[64], const char *uuid,
                     uint64_t off) {
  memset(h, 0, HDR);
  memcpy(h, off ? "SKUL\xBA\xBE" : "LUKS\xBA\xBE", 6);
  be16(h + 6, 2);
  be64(h + 8, HDR);
  be64(h + 16, 1); /* seqid */
  memcpy(h + 72, "sha256", 6);
  memcpy(h + 104, salt, 64);
  memcpy(h + 168, uuid, 36);
  be64(h + 256, off);
  memcpy(h + BIN, json, strlen(json));
  int rc = fl_hash("sha256", h, HDR, h + 448);
  if (rc == FL_OK) rc = fl_bdev_write(part, off / part->sector_size, HDR / part->sector_size, h);
  return rc;
}

int fl_luks2_format(fl_bdev *part, const void *pass, size_t passlen, uint32_t kdf_mem_kib, fl_bdev **decrypted) {
  if (decrypted) *decrypted = NULL;
  if (!part || !decrypted || !pass || passlen == 0 || kdf_mem_kib < 8192) return FL_EINVAL;
  uint32_t ss = part->sector_size;
  if (ss < 512 || ss > 4096 || (ss & (ss - 1))) return FL_EINVAL;
  if (fl_bdev_size_bytes(part) < DATA_OFF + MIB) return FL_ENOSPC;
  uint32_t csec = ss; /* secteurs chiffrés : 512, ou ceux du disque s'ils sont plus grands */

  /* aléa : clé maîtresse, bandes AF, sels (emplacement, condensé, deux en-têtes), UUID */
  enum { R_MK = 0, R_KSALT = R_MK + KEY, R_DSALT = R_KSALT + SALT, R_H1 = R_DSALT + SALT, R_H2 = R_H1 + 64,
         R_UUID = R_H2 + 64, R_AF = R_UUID + 16, R_LEN = R_AF + AF_LEN - KEY };
  uint8_t *r = fl_secure_alloc(R_LEN);
  uint8_t *k = fl_secure_alloc(KEY);
  uint8_t *af = fl_secure_alloc(AREA_LEN);
  uint8_t *enc = malloc(AREA_LEN), *h = malloc(HDR);
  char *json = malloc(JSON_SIZE);
  fl_sector_cipher *c = NULL;
  int rc = r && k && af && enc && h && json ? FL_OK : FL_ENOMEM;
  if (rc == FL_OK) rc = fl_random(r, R_LEN);

  uint8_t *mk = r + R_MK;
  uint8_t dg[32];
  if (rc == FL_OK)
    rc = fl_argon2(FL_ARGON2ID, pass, passlen, r + R_KSALT, SALT, 4, kdf_mem_kib, 4, k, KEY);
  if (rc == FL_OK) rc = fl_af_split("sha256", mk, KEY, STRIPES, r + R_AF, af);
  if (rc == FL_OK) rc = fl_sector_cipher_new(CIPHER, k, KEY, 512, &c);
  if (rc == FL_OK) rc = fl_sector_encrypt(c, 0, af, enc, AF_LEN);
  if (rc == FL_OK) {
    memset(enc + AF_LEN, 0, AREA_LEN - AF_LEN);
    rc = fl_pbkdf2("sha256", mk, KEY, r + R_DSALT, SALT, DIGEST_ITER, dg, sizeof dg);
  }

  char uuid[37];
  if (rc == FL_OK) {
    uint8_t *u = r + R_UUID;
    u[6] = (uint8_t)((u[6] & 0x0F) | 0x40); /* version 4 */
    u[8] = (uint8_t)((u[8] & 0x3F) | 0x80);
    snprintf(uuid, sizeof uuid, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", u[0], u[1],
             u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
    char ks[48], ds[48], dd[48];
    b64(r + R_KSALT, SALT, ks);
    b64(r + R_DSALT, SALT, ds);
    b64(dg, sizeof dg, dd);
    int n = snprintf(
        json, JSON_SIZE,
        "{\"keyslots\":{\"0\":{\"type\":\"luks2\",\"key_size\":%u,\"af\":{\"type\":\"luks1\",\"stripes\":%u,"
        "\"hash\":\"sha256\"},\"area\":{\"type\":\"raw\",\"offset\":\"%u\",\"size\":\"%u\",\"encryption\":\""
        CIPHER "\",\"key_size\":%u},\"kdf\":{\"type\":\"argon2id\",\"time\":4,\"memory\":%u,\"cpus\":4,"
        "\"salt\":\"%s\"}}},\"tokens\":{},\"segments\":{\"0\":{\"type\":\"crypt\",\"offset\":\"%llu\","
        "\"size\":\"dynamic\",\"iv_tweak\":\"0\",\"encryption\":\"" CIPHER "\",\"sector_size\":%u}},"
        "\"digests\":{\"0\":{\"type\":\"pbkdf2\",\"keyslots\":[\"0\"],\"segments\":[\"0\"],\"hash\":\"sha256\","
        "\"iterations\":%u,\"salt\":\"%s\",\"digest\":\"%s\"}},\"config\":{\"json_size\":\"%u\","
        "\"keyslots_size\":\"%llu\"}}",
        KEY, STRIPES, AREA_OFF, AREA_LEN, KEY, kdf_mem_kib, ks, (unsigned long long)DATA_OFF, csec, DIGEST_ITER,
        ds, dd, JSON_SIZE, (unsigned long long)(DATA_OFF - AREA_OFF));
    if (n < 0 || n >= (int)JSON_SIZE) rc = FL_EINVAL;
  }

  /* écriture : zone d'en-têtes et d'emplacements à zéro, emplacement 0, secondaire, primaire */
  if (rc == FL_OK) rc = fl_fmt_zero(part, 0, DATA_OFF / ss);
  if (rc == FL_OK) rc = fl_bdev_write(part, AREA_OFF / ss, AREA_LEN / ss, enc);
  if (rc == FL_OK) rc = write_hdr(part, h, json, r + R_H2, uuid, HDR);
  if (rc == FL_OK) rc = write_hdr(part, h, json, r + R_H1, uuid, 0);
  if (rc == FL_OK) rc = fl_bdev_flush(part);
  if (rc == FL_OK)
    rc = fl_crypt_bdev_new(part, DATA_OFF, 0, CIPHER, mk, KEY, csec, false, 0, decrypted);
  if (rc != FL_OK) *decrypted = NULL;

  fl_memzero(dg, sizeof dg);
  fl_sector_cipher_free(c);
  fl_secure_free(r, R_LEN);
  fl_secure_free(k, KEY);
  fl_secure_free(af, AREA_LEN);
  free(enc);
  free(h);
  free(json);
  return rc;
}
