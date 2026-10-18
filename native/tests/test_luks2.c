#define _DEFAULT_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "crypto/fl_crypto.h"
#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/luks.h"
#include "fluluks/partition.h"
#include "luks/luks_internal.h"
#include "cJSON.h"
#include "tt.h"

#define CHUNK (1u << 20)

static fl_bdev *ouvrir(const char *nom) {
  fl_bdev *d = NULL;
  if (fl_file_bdev_open(tt_fixture(nom), 512, true, &d) != FL_OK) return NULL;
  return d;
}

static size_t lire_pass(char *buf, size_t cap, const char *nom) {
  FILE *f = fopen(tt_fixture(nom), "rb");
  if (!f) return 0;
  size_t n = fread(buf, 1, cap, f);
  fclose(f);
  return n;
}

static void comparer_ext4(fl_bdev *dec) {
  fl_bdev *ref = ouvrir("ext4-plain.img");
  TT_ASSERT(ref != NULL);
  TT_ASSERT_EQ(fl_bdev_size_bytes(dec), 64u * 1024 * 1024);
  uint8_t *a = malloc(CHUNK), *b = malloc(CHUNK);
  TT_ASSERT(a && b);
  uint64_t size = fl_bdev_size_bytes(dec);
  for (uint64_t off = 0; off < size; off += CHUNK) {
    if (fl_bdev_read_bytes(dec, off, CHUNK, a) != FL_OK || fl_bdev_read_bytes(ref, off, CHUNK, b) != FL_OK ||
        memcmp(a, b, CHUNK) != 0) {
      fprintf(stderr, "difference au decalage %llu\n", (unsigned long long)off);
      tt_failures++;
      break;
    }
  }
  free(a);
  free(b);
  fl_bdev_close(ref);
}

/* Ouvre, déverrouille avec le fichier de phrase donné, compare à ext4-plain.img. */
static void deverrouiller(fl_bdev *dev, const char *passfile, const char *cipher, uint32_t sector_size) {
  char pass[256];
  size_t n = lire_pass(pass, sizeof pass, passfile);
  TT_ASSERT(n > 0);
  fl_luks *l = NULL;
  TT_ASSERT_EQ(fl_luks_open(dev, &l), FL_OK);
  TT_ASSERT_EQ(fl_luks_version(l), 2);
  TT_ASSERT_STR_EQ(fl_luks_cipher(l), cipher);
  fl_bdev *dec = NULL;
  TT_ASSERT_EQ(fl_luks_unlock(l, pass, n, &dec), FL_OK);
  TT_ASSERT_EQ(dec->sector_size, sector_size);
  comparer_ext4(dec);
  uint8_t z[512] = {0};
  TT_ASSERT_EQ(fl_bdev_write(dec, 0, 1, z), FL_EROFS);
  fl_bdev_close(dec);
  fl_luks_close(l);
}

static void test_luks2_gpt_argon2id(void) {
  fl_bdev *d = ouvrir("gpt-luks2.img");
  TT_ASSERT(d != NULL);
  fl_part_table t;
  TT_ASSERT_EQ(fl_part_scan(d, &t), FL_OK);
  TT_ASSERT(t.count >= 1);
  fl_bdev *s = NULL;
  TT_ASSERT_EQ(fl_slice_bdev_new(d, t.parts[0].first_lba, t.parts[0].sector_count, &s), FL_OK);
  deverrouiller(s, "pass.txt", "aes-xts-plain64", 512);
  fl_bdev_close(s);
  fl_bdev_close(d);
}

static void test_luks2_4k_argon2i(void) {
  fl_bdev *d = ouvrir("whole-luks2-4k.img");
  TT_ASSERT(d != NULL);
  deverrouiller(d, "pass.txt", "aes-xts-plain64", 4096);
  fl_bdev_close(d);
}

static void test_luks2_deuxieme_slot_utf8(void) {
  fl_bdev *d = ouvrir("whole-luks2-2slots.img");
  TT_ASSERT(d != NULL);
  deverrouiller(d, "pass2.txt", "aes-cbc-plain64", 512);
  deverrouiller(d, "pass.txt", "aes-cbc-plain64", 512);
  fl_bdev_close(d);
}

static void test_luks2_mauvaise_phrase(void) {
  fl_bdev *d = ouvrir("whole-luks2-2slots.img");
  TT_ASSERT(d != NULL);
  fl_luks *l = NULL;
  TT_ASSERT_EQ(fl_luks_open(d, &l), FL_OK);
  fl_bdev *dec = (fl_bdev *)1;
  TT_ASSERT_EQ(fl_luks_unlock(l, "faux", 4, &dec), FL_EBADPASS);
  TT_ASSERT(dec == NULL);
  fl_luks_close(l);
  fl_bdev_close(d);
}

static void test_luks2_entete_secondaire(void) {
  fl_bdev *d = ouvrir("luks2-entete-abime.img");
  TT_ASSERT(d != NULL);
  deverrouiller(d, "pass.txt", "aes-xts-plain64", 4096);
  fl_bdev_close(d);
}

static void test_luks2_tronque(void) {
  fl_bdev *d = ouvrir("luks-tronque.img");
  TT_ASSERT(d != NULL);
  fl_luks *l = (fl_luks *)1;
  TT_ASSERT_EQ(fl_luks_open(d, &l), FL_ECORRUPT);
  TT_ASSERT(l == NULL);
  fl_bdev_close(d);
}

static void test_luks2_taille_dynamique_impaire(void) {
  fl_bdev *a = ouvrir("whole-luks2-4k.img"), *b = ouvrir("whole-luks2-impair.img");
  TT_ASSERT(a && b);
  char pass[256];
  size_t n = lire_pass(pass, sizeof pass, "pass.txt");
  fl_luks *la = NULL, *lb = NULL;
  TT_ASSERT_EQ(fl_luks_open(a, &la), FL_OK);
  TT_ASSERT_EQ(fl_luks_open(b, &lb), FL_OK);
  fl_bdev *da = NULL, *db = NULL;
  TT_ASSERT_EQ(fl_luks_unlock(la, pass, n, &da), FL_OK);
  TT_ASSERT_EQ(fl_luks_unlock(lb, pass, n, &db), FL_OK);
  TT_ASSERT_EQ(da->sector_count, db->sector_count);
  TT_ASSERT_EQ(da->sector_size, db->sector_size);
  fl_bdev_close(da);
  fl_bdev_close(db);
  fl_luks_close(la);
  fl_luks_close(lb);
  fl_bdev_close(a);
  fl_bdev_close(b);
}

static void test_b64(void) {
  uint8_t o[16];
  size_t n = 99;
  TT_ASSERT_EQ(fl_b64_decode("", o, sizeof o, &n), FL_OK);
  TT_ASSERT_EQ(n, 0);
  TT_ASSERT_EQ(fl_b64_decode("Zm9vYg==", o, sizeof o, &n), FL_OK);
  TT_ASSERT_EQ(n, 4);
  TT_ASSERT_MEM_EQ(o, "foob", 4);
  TT_ASSERT_EQ(fl_b64_decode("Zm9vYmE=", o, sizeof o, &n), FL_OK);
  TT_ASSERT_EQ(n, 5);
  TT_ASSERT_EQ(fl_b64_decode("Zm9v", o, sizeof o, &n), FL_OK);
  TT_ASSERT_EQ(n, 3);
  TT_ASSERT_EQ(fl_b64_decode("Zm9", o, sizeof o, &n), FL_ECORRUPT);
  TT_ASSERT_EQ(fl_b64_decode("Zm9v!A==", o, sizeof o, &n), FL_ECORRUPT);
  TT_ASSERT_EQ(fl_b64_decode("Zm=v", o, sizeof o, &n), FL_ECORRUPT);
  TT_ASSERT_EQ(fl_b64_decode("Zg==Zg==", o, sizeof o, &n), FL_ECORRUPT);
  TT_ASSERT_EQ(fl_b64_decode("Zm9vYmFyZm9v", o, 8, &n), FL_ECORRUPT);
}

/* --- images en mémoire : le JSON est modifié structurellement (cJSON), puis l'en-tête est re-signé.
 * Aucune dépendance aux sels aléatoires des fixtures. --- */
#define MEMCAP (32u * 1024 * 1024)
#define LOADLEN (20u * 1024 * 1024) /* en-têtes, zones de clés, début du segment (16 Mio) */
static size_t HDR = 16384;          /* hdr_size, relu dans l'en-tête binaire par mem_load ; secondaire à HDR */
typedef struct {
  uint8_t *m;
  uint64_t fail_lo, fail_hi; /* secteurs en erreur d'E/S [lo, hi) */
  fl_bdev dev;
} memimg;

static int mem_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  memimg *mi = d->priv;
  if (lba < mi->fail_hi && lba + count > mi->fail_lo) return FL_EIO;
  memcpy(buf, mi->m + lba * 512, (size_t)count * 512);
  return FL_OK;
}
static const fl_bdev_ops mem_ops = {mem_read, NULL, NULL, NULL};

static int mem_load(memimg *mi, const char *img) {
  memset(mi, 0, sizeof *mi);
  fl_bdev *src = ouvrir(img);
  if (!src) return -1;
  mi->m = calloc(1, MEMCAP);
  uint64_t n = fl_bdev_size_bytes(src);
  if (n > LOADLEN) n = LOADLEN;
  if (!mi->m || fl_bdev_read_bytes(src, 0, (size_t)n, mi->m) != FL_OK) return -1;
  fl_bdev_close(src);
  mi->dev = (fl_bdev){&mem_ops, 512, MEMCAP / 512, true, mi};
  uint64_t hs = 0; /* hdr_size : u64 big-endian à l'octet 8 (indépendant des défauts de cryptsetup) */
  for (int i = 0; i < 8; i++) hs = (hs << 8) | mi->m[8 + i];
  if (hs < 16384 || hs > (4u << 20) || (hs & 4095)) return -1;
  HDR = (size_t)hs;
  return 0;
}
static void mem_free(memimg *mi) { free(mi->m); }

static void resigner(uint8_t *h) {
  memset(h + 448, 0, 64);
  uint8_t dg[32];
  fl_hash("sha256", h, HDR, dg);
  memcpy(h + 448, dg, 32);
}

/* Fixe `chemin` ("a.b.c") à la valeur JSON `val` (NULL = supprimer) dans l'en-tête h. chemin "" = remplacer la racine. */
#define P_PRI 1
#define P_SEC 2
static int patch_one(uint8_t *h, const char *chemin, const char *val) {
  char *j = (char *)h + 4096;
  cJSON *root = cJSON_Parse(j);
  if (!root) return -1;
  cJSON *v = val ? cJSON_Parse(val) : NULL;
  if (*chemin == 0) {
    cJSON_Delete(root);
    root = v;
  } else {
    char path[128];
    snprintf(path, sizeof path, "%s", chemin);
    cJSON *cur = root;
    char *save = NULL, *tok = strtok_r(path, ".", &save), *next;
    while ((next = strtok_r(NULL, ".", &save)) != NULL) {
      cur = cJSON_GetObjectItemCaseSensitive(cur, tok);
      if (!cur) return -1;
      tok = next;
    }
    if (cJSON_GetObjectItemCaseSensitive(cur, tok)) {
      if (v) cJSON_ReplaceItemInObjectCaseSensitive(cur, tok, v);
      else cJSON_DeleteItemFromObjectCaseSensitive(cur, tok);
    } else if (v) {
      cJSON_AddItemToObject(cur, tok, v);
    }
  }
  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!txt || strlen(txt) >= HDR - 4096) return -1;
  memset(j, 0, HDR - 4096);
  memcpy(j, txt, strlen(txt));
  free(txt);
  resigner(h);
  return 0;
}

static int mem_patch(memimg *mi, int which, const char *chemin, const char *val) {
  if ((which & P_PRI) && patch_one(mi->m, chemin, val)) return -1;
  if ((which & P_SEC) && patch_one(mi->m + HDR, chemin, val)) return -1;
  return 0;
}

static void mem_seqid(memimg *mi, int which, uint8_t seq) {
  if (which & P_PRI) { mi->m[16 + 7] = seq; resigner(mi->m); }
  if (which & P_SEC) { mi->m[HDR + 16 + 7] = seq; resigner(mi->m + HDR); }
}

/* image + modification sur les deux en-têtes -> code de fl_luks_open */
static int ouvrir_patche(const char *img, const char *chemin, const char *val) {
  memimg mi;
  if (mem_load(&mi, img)) return -1000;
  if (mem_patch(&mi, P_PRI | P_SEC, chemin, val)) { mem_free(&mi); return -1001; }
  fl_luks *l = NULL;
  int rc = fl_luks_open(&mi.dev, &l);
  fl_luks_close(l);
  mem_free(&mi);
  return rc;
}

static void test_luks2_hostile(void) {
  const char *img = "whole-luks2-4k.img";
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.af.stripes", "4000"), FL_OK);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.af.stripes", "0"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.af.stripes", "65537"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.kdf.memory", "4194305"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.kdf.memory", "8"), FL_ECORRUPT); /* < 8*cpus */
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.kdf.cpus", "65"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.kdf.cpus", "0"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.kdf.time", "1001"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.kdf.time", "0"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.key_size", "48"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.area.key_size", "48"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.area.offset", "\"-1\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.area.offset", "\"99999999999999999999\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.area.offset", "\"0x8000\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.area.offset", "\"33000\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.area.size", "\"1024\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.area.size", "\"999999999999\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.area.type", "\"journal\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "segments.0.offset", "\"999999999999\""), FL_ECORRUPT);
  {
    /* décalage du segment lu dans le JSON de l'image (pas de valeur codée en dur) */
    memimg mi;
    TT_ASSERT_EQ(mem_load(&mi, img), 0);
    cJSON *root = cJSON_Parse((const char *)mi.m + 4096);
    TT_ASSERT(root != NULL);
    cJSON *o = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "segments"), "0"), "offset");
    TT_ASSERT(cJSON_IsString(o));
    char val[64];
    snprintf(val, sizeof val, "\"%llu\"", (unsigned long long)strtoull(o->valuestring, NULL, 10) + 1);
    cJSON_Delete(root);
    mem_free(&mi);
    TT_ASSERT_EQ(ouvrir_patche(img, "segments.0.offset", val), FL_ECORRUPT); /* non aligné */
  }
  TT_ASSERT_EQ(ouvrir_patche(img, "segments.0.sector_size", "4097"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.kdf.salt", "\"!!!\""), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "digests.0.iterations", "0"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "digests.0.keyslots", "[\"7\"]"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots", "[]"), FL_ECORRUPT);
  TT_ASSERT_EQ(ouvrir_patche(img, "", "[1]"), FL_ECORRUPT);
  /* bien formé mais non pris en charge : aucun slot utilisable -> ENOTSUP */
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.type", "\"autre\""), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.kdf.type", "\"scrypt\""), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.af.type", "\"luks2\""), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.af.hash", "\"md5\""), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "keyslots.0.area.encryption", "\"serpent-xts-plain64\""), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "segments.0.type", "\"linear\""), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "segments.0.encryption", "\"serpent-xts-plain64\""), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "segments.5", "{\"type\":\"crypt\"}"), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "segments.0.integrity", "{\"type\":\"hmac(sha256)\"}"), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "config.requirements", "{\"mandatory\":[\"online-reencrypt\"]}"), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "config.requirements", "{\"mandatory\":[]}"), FL_OK);
  TT_ASSERT_EQ(ouvrir_patche(img, "digests.0.iterations", "134217729"), FL_ENOTSUP);
  TT_ASSERT_EQ(ouvrir_patche(img, "digests.0.iterations", "134217728"), FL_OK);
}

static void unlock_attendu(memimg *mi, const char *passfile, int attendu) {
  char pass[256];
  size_t n = lire_pass(pass, sizeof pass, passfile);
  TT_ASSERT(n > 0);
  fl_luks *l = NULL;
  TT_ASSERT_EQ(fl_luks_open(&mi->dev, &l), FL_OK);
  fl_bdev *dec = NULL;
  TT_ASSERT_EQ(fl_luks_unlock(l, pass, n, &dec), attendu);
  TT_ASSERT((dec != NULL) == (attendu == FL_OK));
  fl_bdev_close(dec);
  fl_luks_close(l);
}

/* pass.txt ouvre le slot 0 seulement, pass2.txt le slot 1 seulement. */
static void test_luks2_priorite_et_digest(void) {
  memimg mi;
  TT_ASSERT_EQ(mem_load(&mi, "whole-luks2-2slots.img"), 0);
  /* témoins positifs sur l'image intacte */
  unlock_attendu(&mi, "pass.txt", FL_OK);
  unlock_attendu(&mi, "pass2.txt", FL_OK);
  /* slot 0 en priorité 0 : jamais essayé */
  TT_ASSERT_EQ(mem_patch(&mi, P_PRI | P_SEC, "keyslots.0.priority", "0"), 0);
  unlock_attendu(&mi, "pass.txt", FL_EBADPASS);
  unlock_attendu(&mi, "pass2.txt", FL_OK);
  /* priorité 2 sur le slot 0 : toujours utilisable */
  TT_ASSERT_EQ(mem_patch(&mi, P_PRI | P_SEC, "keyslots.0.priority", "2"), 0);
  unlock_attendu(&mi, "pass.txt", FL_OK);
  unlock_attendu(&mi, "pass2.txt", FL_OK);
  /* le digest ne lie plus que le slot 1 : le slot 0 est refusé même avec la bonne phrase */
  TT_ASSERT_EQ(mem_patch(&mi, P_PRI | P_SEC, "digests.0.keyslots", "[\"1\"]"), 0);
  unlock_attendu(&mi, "pass.txt", FL_EBADPASS);
  unlock_attendu(&mi, "pass2.txt", FL_OK);
  /* le digest ne liste plus le segment 0 : plus aucun slot lié -> ENOTSUP à l'ouverture */
  TT_ASSERT_EQ(mem_patch(&mi, P_PRI | P_SEC, "digests.0.segments", "[]"), 0);
  fl_luks *l = NULL;
  TT_ASSERT_EQ(fl_luks_open(&mi.dev, &l), FL_ENOTSUP);
  mem_free(&mi);
}

static void test_luks2_slot_non_supporte_ignore(void) {
  memimg mi;
  TT_ASSERT_EQ(mem_load(&mi, "whole-luks2-2slots.img"), 0);
  TT_ASSERT_EQ(mem_patch(&mi, P_PRI | P_SEC, "keyslots.1.area.encryption", "\"serpent-xts-plain64\""), 0);
  unlock_attendu(&mi, "pass.txt", FL_OK);
  unlock_attendu(&mi, "pass2.txt", FL_EBADPASS);
  mem_free(&mi);
  /* itérations PBKDF2 hostiles sur un seul slot : ignoré aussi */
  TT_ASSERT_EQ(mem_load(&mi, "whole-luks2-2slots.img"), 0);
  TT_ASSERT_EQ(mem_patch(&mi, P_PRI | P_SEC, "keyslots.1.kdf.iterations", "134217729"), 0);
  unlock_attendu(&mi, "pass.txt", FL_OK);
  unlock_attendu(&mi, "pass2.txt", FL_EBADPASS);
  TT_ASSERT_EQ(mem_patch(&mi, P_PRI | P_SEC, "keyslots.0.kdf.iterations", "134217729"), 0);
  fl_luks *l = NULL;
  TT_ASSERT_EQ(fl_luks_open(&mi.dev, &l), FL_ENOTSUP);
  mem_free(&mi);
}

static void test_luks2_magie_abimee(void) {
  memimg mi;
  TT_ASSERT_EQ(mem_load(&mi, "whole-luks2-4k.img"), 0);
  memset(mi.m, 0, 6); /* magie du primaire détruite */
  unlock_attendu(&mi, "pass.txt", FL_OK);
  mem_free(&mi);
  /* version du primaire détruite */
  TT_ASSERT_EQ(mem_load(&mi, "whole-luks2-4k.img"), 0);
  mi.m[7] = 9;
  unlock_attendu(&mi, "pass.txt", FL_OK);
  /* magie détruite des deux côtés : plus du LUKS */
  memset(mi.m, 0, 6);
  memset(mi.m + HDR, 0, 6);
  fl_luks *l = (fl_luks *)1;
  TT_ASSERT_EQ(fl_luks_open(&mi.dev, &l), FL_ENOTSUP);
  TT_ASSERT(l == NULL);
  mem_free(&mi);
}

static void test_luks2_choix_entete(void) {
  memimg mi;
  fl_luks *l = NULL;
  /* primaire valide (seqid plus grand) mais non pris en charge : jamais de repli sur le secondaire périmé */
  TT_ASSERT_EQ(mem_load(&mi, "whole-luks2-4k.img"), 0);
  TT_ASSERT_EQ(mem_patch(&mi, P_PRI, "segments.5", "{\"type\":\"crypt\"}"), 0);
  mem_seqid(&mi, P_PRI, 9);
  TT_ASSERT_EQ(fl_luks_open(&mi.dev, &l), FL_ENOTSUP);
  /* même chose côté secondaire plus récent */
  mem_free(&mi);
  TT_ASSERT_EQ(mem_load(&mi, "whole-luks2-4k.img"), 0);
  TT_ASSERT_EQ(mem_patch(&mi, P_SEC, "segments.5", "{\"type\":\"crypt\"}"), 0);
  mem_seqid(&mi, P_SEC, 9);
  TT_ASSERT_EQ(fl_luks_open(&mi.dev, &l), FL_ENOTSUP);
  mem_free(&mi);
  /* primaire à JSON corrompu mais somme valide, secondaire plus ancien et sain : repli */
  TT_ASSERT_EQ(mem_load(&mi, "whole-luks2-4k.img"), 0);
  TT_ASSERT_EQ(mem_patch(&mi, P_PRI, "keyslots.0.af.stripes", "0"), 0);
  mem_seqid(&mi, P_PRI, 9);
  unlock_attendu(&mi, "pass.txt", FL_OK);
  mem_free(&mi);
  /* erreur d'E/S sur l'en-tête secondaire : le primaire valide suffit */
  TT_ASSERT_EQ(mem_load(&mi, "whole-luks2-4k.img"), 0);
  mi.fail_lo = HDR / 512;
  mi.fail_hi = HDR / 512 + 8;
  unlock_attendu(&mi, "pass.txt", FL_OK);
  /* primaire invalide + secondaire illisible : EIO */
  memset(mi.m, 0, 6);
  TT_ASSERT_EQ(fl_luks_open(&mi.dev, &l), FL_EIO);
  mem_free(&mi);
}

/* Un en-tête avec label et sous-système (champs de l'en-tête binaire : label à 24, sous-système à 208)
 * se déverrouille comme n'importe quel autre ; les deux copies sont re-signées. */
static void test_luks2_label(void) {
  memimg mi;
  TT_ASSERT_EQ(mem_load(&mi, "whole-luks2-4k.img"), 0);
  for (int c = 0; c < 2; c++) {
    uint8_t *h = mi.m + (c ? HDR : 0);
    memset(h + 24, 0, 48);
    memcpy(h + 24, "Étiquette FluLuks", strlen("Étiquette FluLuks"));
    memset(h + 208, 0, 48);
    memcpy(h + 208, "sous-systeme", strlen("sous-systeme"));
    resigner(h);
  }
  unlock_attendu(&mi, "pass.txt", FL_OK);
  /* label et sous-système remplissant les 48 octets sans NUL final : aucune lecture hors champ */
  for (int c = 0; c < 2; c++) {
    uint8_t *h = mi.m + (c ? HDR : 0);
    memset(h + 24, 'L', 48);
    memset(h + 208, 'S', 48);
    resigner(h);
  }
  unlock_attendu(&mi, "pass.txt", FL_OK);
  mem_free(&mi);
}

/* --- écriture : aller-retour sur une copie de l'image aux paramètres exacts du vrai disque
 * (argon2id t=7, 1 Gio, 4 fils). Seul test à les utiliser en écriture. --- */
static int copier_fichier(const char *src, const char *dst) {
  FILE *a = fopen(src, "rb"), *b = fopen(dst, "wb");
  int ok = a && b;
  static unsigned char buf[1u << 20];
  size_t n;
  while (ok && (n = fread(buf, 1, sizeof buf, a)) > 0) ok = fwrite(buf, 1, n, b) == n;
  if (a) fclose(a);
  if (b && fclose(b) != 0) ok = 0;
  return ok;
}

static void remplir(uint8_t *p, size_t n, uint32_t graine) {
  for (size_t i = 0; i < n; i++) {
    graine = graine * 1103515245u + 12345u;
    p[i] = (uint8_t)(graine >> 16);
  }
}

static fl_bdev *ouvrir_copie(const char *copie, bool lecture_seule, fl_luks **l) {
  char pass[256];
  size_t n = lire_pass(pass, sizeof pass, "pass.txt");
  fl_bdev *d = NULL, *dec = NULL;
  *l = NULL;
  if (n == 0 || fl_file_bdev_open(copie, 512, lecture_seule, &d) != FL_OK) return NULL;
  if (fl_luks_open(d, l) != FL_OK || fl_luks_unlock(*l, pass, n, &dec) != FL_OK) {
    fl_luks_close(*l);
    *l = NULL;
    fl_bdev_close(d);
    return NULL;
  }
  return dec;
}

static void aller_retour(const char *copie) {
  char orig[4096]; /* tt_fixture rend un tampon statique */
  snprintf(orig, sizeof orig, "%s", tt_fixture("whole-luks2-exact.img"));
  TT_ASSERT(copier_fichier(orig, copie));
  fl_luks *l = NULL;
  fl_bdev *dec = ouvrir_copie(copie, false, &l);
  TT_ASSERT(dec != NULL);
  fl_bdev *parent = l->dev;
  TT_ASSERT(!dec->read_only);
  uint64_t n = dec->sector_count;
  TT_ASSERT(n > 1002);
  /* trois écritures : un secteur à 0, un à 1000, trois secteurs terminant le disque (dernier - 2 .. dernier) */
  static const struct { uint64_t lba; uint32_t cnt; } ecr[3] = {{0, 1}, {1000, 1}, {0, 3}};
  uint64_t lba[3] = {ecr[0].lba, ecr[1].lba, n - 3};
  uint8_t data[3][3 * 512], avant[3][3 * 512];
  for (int i = 0; i < 3; i++) {
    TT_ASSERT_EQ(fl_bdev_read(dec, lba[i], ecr[i].cnt, avant[i]), FL_OK);
    remplir(data[i], sizeof data[i], 77u + (uint32_t)i);
    TT_ASSERT(memcmp(avant[i], data[i], ecr[i].cnt * 512u) != 0);
    TT_ASSERT_EQ(fl_bdev_write(dec, lba[i], ecr[i].cnt, data[i]), FL_OK);
  }
  TT_ASSERT_EQ(fl_bdev_flush(dec), FL_OK);
  fl_bdev_close(dec);
  fl_luks_close(l);
  fl_bdev_close(parent);

  /* le texte chiffré a changé, et seulement aux 5 secteurs écrits */
  FILE *a = fopen(orig, "rb"), *b = fopen(copie, "rb");
  TT_ASSERT(a && b);
  uint8_t sa[512], sb[512];
  uint64_t diff[16], nd = 0;
  for (uint64_t s = 0; fread(sa, 1, 512, a) == 512 && fread(sb, 1, 512, b) == 512; s++)
    if (memcmp(sa, sb, 512) != 0 && nd < 16) diff[nd++] = s;
  fclose(a);
  fclose(b);
  TT_ASSERT_EQ(nd, 5);
  uint64_t base = diff[0]; /* secteur brut du LBA 0 : début de la zone de données */
  TT_ASSERT(base > 0);
  TT_ASSERT_EQ(diff[1], base + 1000);
  TT_ASSERT_EQ(diff[2], base + n - 3);
  TT_ASSERT_EQ(diff[3], base + n - 2);
  TT_ASSERT_EQ(diff[4], base + n - 1);
  FILE *f = fopen(copie, "rb");
  TT_ASSERT(f != NULL);
  for (int i = 0; i < 3; i++) {
    uint8_t raw[3 * 512];
    int ok = fseeko(f, (off_t)((base + lba[i]) * 512), SEEK_SET) == 0 && fread(raw, 1, ecr[i].cnt * 512u, f) == ecr[i].cnt * 512u;
    if (!ok || memcmp(raw, data[i], ecr[i].cnt * 512u) == 0 || memcmp(raw, avant[i], ecr[i].cnt * 512u) == 0) {
      fprintf(stderr, "texte chiffré inattendu au LBA %llu\n", (unsigned long long)lba[i]);
      tt_failures++;
      fclose(f);
      return;
    }
  }
  fclose(f);

  /* on rouvre, on redéverrouille : les mêmes octets */
  dec = ouvrir_copie(copie, true, &l);
  TT_ASSERT(dec != NULL);
  parent = l->dev;
  TT_ASSERT_EQ(dec->sector_count, n);
  for (int i = 0; i < 3; i++) {
    uint8_t rd[3 * 512];
    TT_ASSERT_EQ(fl_bdev_read(dec, lba[i], ecr[i].cnt, rd), FL_OK);
    TT_ASSERT_MEM_EQ(rd, data[i], ecr[i].cnt * 512u);
  }
  fl_bdev_close(dec);
  fl_luks_close(l);
  fl_bdev_close(parent);
}

static void test_luks2_ecriture_aller_retour(void) {
  char copie[4096];
  snprintf(copie, sizeof copie, "%s", tt_fixture("tmp-ecriture-aller-retour.img"));
  aller_retour(copie);
  unlink(copie); /* même en cas d'échec : TT_ASSERT ne fait que quitter aller_retour */
}

/* Plus grande mémoire Argon2 parmi les slots utilisables, en Kio ; 0 pour PBKDF2 ou LUKS1. */
static void kdf_attendu(const char *nom, int partitionne, uint32_t attendu) {
  fl_bdev *d = ouvrir(nom), *s = NULL;
  TT_ASSERT(d != NULL);
  if (partitionne) {
    fl_part_table t;
    TT_ASSERT_EQ(fl_part_scan(d, &t), FL_OK);
    TT_ASSERT(t.count >= 1);
    TT_ASSERT_EQ(fl_slice_bdev_new(d, t.parts[0].first_lba, t.parts[0].sector_count, &s), FL_OK);
  }
  fl_luks *l = NULL;
  TT_ASSERT_EQ(fl_luks_open(s ? s : d, &l), FL_OK);
  uint32_t kib = 12345;
  TT_ASSERT_EQ(fl_luks_max_kdf_memory_kib(l, &kib), FL_OK);
  TT_ASSERT_EQ(kib, attendu);
  fl_luks_close(l);
  fl_bdev_close(s);
  fl_bdev_close(d);
}

static void test_luks_max_kdf_memory(void) {
  kdf_attendu("gpt-luks2.img", 1, 65536);       /* argon2id, 64 Mio */
  kdf_attendu("whole-luks2-4k.img", 0, 32768);  /* argon2i, 32 Mio */
  kdf_attendu("whole-luks2-2slots.img", 0, 0);  /* PBKDF2 seulement */
  kdf_attendu("whole-luks1-xts.img", 0, 0);     /* LUKS1 */
  kdf_attendu("mbr-luks1-essiv.img", 1, 0);
  uint32_t kib = 7;
  TT_ASSERT_EQ(fl_luks_max_kdf_memory_kib(NULL, &kib), FL_EINVAL);
  TT_ASSERT_EQ(kib, 7);
}

int main(void) {
  TT_RUN(test_b64);
  TT_RUN(test_luks2_hostile);
  TT_RUN(test_luks2_priorite_et_digest);
  TT_RUN(test_luks2_slot_non_supporte_ignore);
  TT_RUN(test_luks2_magie_abimee);
  TT_RUN(test_luks2_label);
  TT_RUN(test_luks2_choix_entete);
  TT_RUN(test_luks2_gpt_argon2id);
  TT_RUN(test_luks2_4k_argon2i);
  TT_RUN(test_luks2_deuxieme_slot_utf8);
  TT_RUN(test_luks2_mauvaise_phrase);
  TT_RUN(test_luks2_entete_secondaire);
  TT_RUN(test_luks2_tronque);
  TT_RUN(test_luks_max_kdf_memory);
  TT_RUN(test_luks2_taille_dynamique_impaire);
  TT_RUN(test_luks2_ecriture_aller_retour);
  TT_MAIN_END();
}
