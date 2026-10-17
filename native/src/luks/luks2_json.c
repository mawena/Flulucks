#define _DEFAULT_SOURCE
#include <string.h>

#include "cJSON.h"
#include "crypto/fl_crypto.h"
#include "fluluks/error.h"
#include "luks/luks2_internal.h"
#include "luks/luks_internal.h"

#define MAX_STRIPES 65536u
#define MAX_ARGON2_KIB 4194304u /* maximum de cryptsetup */

static const cJSON *member(const cJSON *o, const char *k) { return cJSON_GetObjectItemCaseSensitive((cJSON *)o, k); }

/* Décimal strict : chiffres seulement, sans signe, sans dépassement de 64 bits. */
static int parse_u64(const char *s, uint64_t *out) {
  if (!s || !*s) return FL_ECORRUPT;
  uint64_t v = 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9') return FL_ECORRUPT;
    uint64_t d = (uint64_t)(*s - '0');
    if (v > (UINT64_MAX - d) / 10) return FL_ECORRUPT;
    v = v * 10 + d;
  }
  *out = v;
  return FL_OK;
}

/* Entier 64 bits porté par une chaîne JSON. */
static int get_u64(const cJSON *o, const char *k, uint64_t *out) {
  const cJSON *m = member(o, k);
  if (!cJSON_IsString(m)) return FL_ECORRUPT;
  return parse_u64(m->valuestring, out);
}

/* Entier JSON (nombre) dans [lo, hi]. */
static int get_uint(const cJSON *o, const char *k, uint64_t lo, uint64_t hi, uint32_t *out) {
  const cJSON *m = member(o, k);
  if (!cJSON_IsNumber(m)) return FL_ECORRUPT;
  double d = m->valuedouble;
  if (!(d >= (double)lo && d <= (double)hi) || d != (double)(uint64_t)d) return FL_ECORRUPT;
  *out = (uint32_t)(uint64_t)d;
  return FL_OK;
}

static int get_str(const cJSON *o, const char *k, char *dst, size_t cap) {
  const cJSON *m = member(o, k);
  if (!cJSON_IsString(m) || !m->valuestring) return FL_ECORRUPT;
  size_t n = strlen(m->valuestring);
  if (n == 0 || n >= cap) return FL_ECORRUPT;
  memcpy(dst, m->valuestring, n + 1);
  return FL_OK;
}

static int get_b64(const cJSON *o, const char *k, uint8_t *dst, size_t cap, size_t *len) {
  const cJSON *m = member(o, k);
  if (!cJSON_IsString(m) || !m->valuestring) return FL_ECORRUPT;
  int rc = fl_b64_decode(m->valuestring, dst, cap, len);
  if (rc != FL_OK) return rc;
  return *len ? FL_OK : FL_ECORRUPT;
}

static bool cipher_supported(const char *c) {
  return !strcmp(c, "aes-xts-plain64") || !strcmp(c, "aes-cbc-essiv:sha256") || !strcmp(c, "aes-cbc-plain64");
}

static bool in_set_key(uint32_t k) { return k == 16 || k == 32 || k == 64; }

static size_t round512(size_t n) { return (n + 511) & ~(size_t)511; }

static int parse_slot(const cJSON *ks, uint64_t dev_size, l2_slot *s) {
  int rc;
  char type[16];
  if (!cJSON_IsObject(ks)) return FL_ECORRUPT;
  if ((rc = get_str(ks, "type", type, sizeof type)) != FL_OK) return rc;
  if (strcmp(type, "luks2") != 0) return FL_ENOTSUP;
  s->priority = 1;
  if (member(ks, "priority")) {
    uint32_t p;
    if ((rc = get_uint(ks, "priority", 0, 2, &p)) != FL_OK) return rc;
    s->priority = (int)p;
  }
  if ((rc = get_uint(ks, "key_size", 16, 64, &s->key_size)) != FL_OK) return rc;
  if (!in_set_key(s->key_size)) return FL_ECORRUPT;

  const cJSON *af = member(ks, "af");
  if (!cJSON_IsObject(af)) return FL_ECORRUPT;
  if ((rc = get_str(af, "type", type, sizeof type)) != FL_OK) return rc;
  if (strcmp(type, "luks1") != 0) return FL_ENOTSUP;
  if ((rc = get_uint(af, "stripes", 1, MAX_STRIPES, &s->stripes)) != FL_OK) return rc;
  if ((rc = get_str(af, "hash", s->af_hash, sizeof s->af_hash)) != FL_OK) return rc;
  if (fl_hash_len(s->af_hash) == 0) return FL_ENOTSUP;

  const cJSON *ar = member(ks, "area");
  if (!cJSON_IsObject(ar)) return FL_ECORRUPT;
  if ((rc = get_str(ar, "type", type, sizeof type)) != FL_OK) return rc;
  if (strcmp(type, "raw") != 0) return FL_ECORRUPT;
  if ((rc = get_u64(ar, "offset", &s->area_off)) != FL_OK) return rc;
  if ((rc = get_u64(ar, "size", &s->area_size)) != FL_OK) return rc;
  if ((rc = get_str(ar, "encryption", s->area_enc, sizeof s->area_enc)) != FL_OK) return rc;
  if ((rc = get_uint(ar, "key_size", 16, 64, &s->area_key_size)) != FL_OK) return rc;
  if (!in_set_key(s->area_key_size)) return FL_ECORRUPT;
  uint64_t need = round512((size_t)s->key_size * s->stripes);
  if (s->area_off % 512 || s->area_size % 512 || s->area_off > dev_size || s->area_size > dev_size - s->area_off ||
      s->area_size < need)
    return FL_ECORRUPT;
  if (!cipher_supported(s->area_enc)) return FL_ENOTSUP;

  const cJSON *kdf = member(ks, "kdf");
  if (!cJSON_IsObject(kdf)) return FL_ECORRUPT;
  if ((rc = get_str(kdf, "type", type, sizeof type)) != FL_OK) return rc;
  if ((rc = get_b64(kdf, "salt", s->salt, sizeof s->salt, &s->salt_len)) != FL_OK) return rc;
  if (!strcmp(type, "pbkdf2")) {
    s->kdf = L2_KDF_PBKDF2;
    if ((rc = get_str(kdf, "hash", s->kdf_hash, sizeof s->kdf_hash)) != FL_OK) return rc;
    if ((rc = get_uint(kdf, "iterations", 1, UINT32_MAX, &s->iterations)) != FL_OK) return rc;
    if (fl_hash_len(s->kdf_hash) == 0) return FL_ENOTSUP;
  } else if (!strcmp(type, "argon2i") || !strcmp(type, "argon2id")) {
    s->kdf = type[7] ? L2_KDF_ARGON2ID : L2_KDF_ARGON2I;
    if ((rc = get_uint(kdf, "cpus", 1, 64, &s->cpus)) != FL_OK) return rc;
    if ((rc = get_uint(kdf, "time", 1, 1000, &s->time)) != FL_OK) return rc;
    if ((rc = get_uint(kdf, "memory", 8u * s->cpus, MAX_ARGON2_KIB, &s->memory)) != FL_OK) return rc;
    if (s->salt_len < 8) return FL_ECORRUPT;
  } else {
    return FL_ENOTSUP;
  }
  s->digest = -1;
  return FL_OK;
}

static int parse_digest(const cJSON *d, l2_digest *out) {
  int rc;
  char type[16];
  if (!cJSON_IsObject(d)) return FL_ECORRUPT;
  if ((rc = get_str(d, "type", type, sizeof type)) != FL_OK) return rc;
  if (strcmp(type, "pbkdf2") != 0) return FL_ENOTSUP;
  if ((rc = get_str(d, "hash", out->hash, sizeof out->hash)) != FL_OK) return rc;
  if ((rc = get_uint(d, "iterations", 1, UINT32_MAX, &out->iterations)) != FL_OK) return rc;
  if ((rc = get_b64(d, "salt", out->salt, sizeof out->salt, &out->salt_len)) != FL_OK) return rc;
  if ((rc = get_b64(d, "digest", out->digest, sizeof out->digest, &out->digest_len)) != FL_OK) return rc;
  if (out->digest_len < 16) return FL_ECORRUPT; /* un digest trop court accepterait n'importe quelle clé */
  if (fl_hash_len(out->hash) == 0) return FL_ENOTSUP;
  return FL_OK;
}

/* Tableau JSON de chaînes décimales -> identifiants. */
static int array_ids(const cJSON *arr, uint64_t *ids, int cap, int *n) {
  *n = 0;
  if (!cJSON_IsArray(arr)) return FL_ECORRUPT;
  const cJSON *e;
  cJSON_ArrayForEach(e, arr) {
    if (!cJSON_IsString(e) || *n >= cap) return FL_ECORRUPT;
    int rc = parse_u64(e->valuestring, &ids[(*n)++]);
    if (rc != FL_OK) return rc;
  }
  return FL_OK;
}

static int parse_segment(const cJSON *sg, uint64_t dev_size, luks2 *x) {
  int rc;
  char type[16];
  if (!cJSON_IsObject(sg)) return FL_ECORRUPT;
  if ((rc = get_str(sg, "type", type, sizeof type)) != FL_OK) return rc;
  if (strcmp(type, "crypt") != 0) return FL_ENOTSUP;
  if ((rc = get_u64(sg, "offset", &x->seg_off)) != FL_OK) return rc;
  const cJSON *sz = member(sg, "size");
  if (cJSON_IsString(sz) && sz->valuestring && !strcmp(sz->valuestring, "dynamic")) {
    x->seg_size = 0;
  } else {
    if ((rc = get_u64(sg, "size", &x->seg_size)) != FL_OK) return rc;
    if (x->seg_size == 0) return FL_ECORRUPT;
  }
  const cJSON *iv = member(sg, "iv_tweak");
  x->iv_tweak = 0;
  if (iv && (rc = get_u64(sg, "iv_tweak", &x->iv_tweak)) != FL_OK) return rc;
  if ((rc = get_str(sg, "encryption", x->seg_enc, sizeof x->seg_enc)) != FL_OK) return rc;
  if ((rc = get_uint(sg, "sector_size", 512, 4096, &x->sector_size)) != FL_OK) return rc;
  uint32_t ss = x->sector_size;
  if (ss != 512 && ss != 1024 && ss != 2048 && ss != 4096) return FL_ECORRUPT;
  if (x->seg_off >= dev_size || x->seg_off % 512) return FL_ECORRUPT;
  if (x->seg_size && x->seg_size > dev_size - x->seg_off) return FL_ECORRUPT;
  if (!cipher_supported(x->seg_enc)) return FL_ENOTSUP;
  return FL_OK;
}

int fl_luks2_parse_json(const char *json, uint64_t dev_size, luks2 *x) {
  memset(x, 0, sizeof *x);
  cJSON *root = cJSON_Parse(json);
  if (!root) return FL_ECORRUPT;
  int rc = FL_ECORRUPT;
  uint64_t ids[L2_MAX_SLOTS];
  const cJSON *keyslots = member(root, "keyslots"), *segments = member(root, "segments"),
              *digests = member(root, "digests"), *e;
  if (!cJSON_IsObject(root) || !cJSON_IsObject(keyslots) || !cJSON_IsObject(segments) || !cJSON_IsObject(digests))
    goto out;

  /* segments : un seul, "0", de type crypt */
  int nseg = cJSON_GetArraySize(segments);
  if (nseg != 1) {
    rc = nseg > 1 ? FL_ENOTSUP : FL_ECORRUPT;
    goto out;
  }
  const cJSON *seg = segments->child;
  if (!seg->string || strcmp(seg->string, "0") != 0) {
    rc = FL_ENOTSUP;
    goto out;
  }
  if ((rc = parse_segment(seg, dev_size, x)) != FL_OK) goto out;

  /* keyslots */
  cJSON_ArrayForEach(e, keyslots) {
    if (x->nslots >= L2_MAX_SLOTS || !e->string) {
      rc = FL_ECORRUPT;
      goto out;
    }
    l2_slot *s = &x->slots[x->nslots];
    if ((rc = parse_u64(e->string, &ids[x->nslots])) != FL_OK) goto out;
    if ((rc = parse_slot(e, dev_size, s)) != FL_OK) goto out;
    x->nslots++;
  }

  /* digests : lient les slots (et doivent lister le segment 0) */
  cJSON_ArrayForEach(e, digests) {
    if (x->ndigests >= L2_MAX_DIGESTS) {
      rc = FL_ECORRUPT;
      goto out;
    }
    l2_digest *d = &x->digests[x->ndigests];
    if ((rc = parse_digest(e, d)) != FL_OK) goto out;
    uint64_t kid[L2_MAX_SLOTS], sid[4];
    int nk, ns;
    if ((rc = array_ids(member(e, "keyslots"), kid, L2_MAX_SLOTS, &nk)) != FL_OK) goto out;
    if ((rc = array_ids(member(e, "segments"), sid, 4, &ns)) != FL_OK) goto out;
    bool seg0 = false;
    for (int i = 0; i < ns; i++) seg0 |= sid[i] == 0;
    for (int i = 0; i < nk; i++) {
      int found = -1;
      for (int j = 0; j < x->nslots; j++)
        if (ids[j] == kid[i]) {
          found = j;
          break;
        }
      if (found < 0) {
        rc = FL_ECORRUPT;
        goto out;
      }
      if (seg0 && x->slots[found].digest < 0) x->slots[found].digest = x->ndigests;
    }
    x->ndigests++;
  }

  /* tri stable : priorité décroissante */
  for (int i = 1; i < x->nslots; i++) {
    l2_slot t = x->slots[i];
    int j = i - 1;
    while (j >= 0 && x->slots[j].priority < t.priority) {
      x->slots[j + 1] = x->slots[j];
      j--;
    }
    x->slots[j + 1] = t;
  }
  rc = FL_OK;
out:
  cJSON_Delete(root);
  if (rc != FL_OK) memset(x, 0, sizeof *x);
  return rc;
}
