/* Règles de lecture seule du § 4.2 (voir ext4_policy.h). Le disque est hostile : tout ce qui est
 * lu ici est borné (taille de bloc, nombre de groupes, profondeur d'extents) ; le superbloc reçu a
 * déjà été validé par la colle (sb_validate). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ext4_block_group.h"
#include "ext4_crc32.h"
#include "ext4_types.h"
#include "fluluks/error.h"
#include "fs/ext4_policy.h"
#include "fs/ro_reasons.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

/* Options connues en écriture (lwext4 corrigé) ; RECOVER relève de la règle 4. */
#define W_INCOMPAT                                                                                  \
  (EXT4_FINCOM_FILETYPE | EXT4_FINCOM_EXTENTS | EXT4_FINCOM_64BIT | EXT4_FINCOM_FLEX_BG |           \
   EXT4_FINCOM_CSUM_SEED | EXT4_FINCOM_RECOVER)
#define W_RO_COMPAT                                                                                    \
  (EXT4_FRO_COM_SPARSE_SUPER | EXT4_FRO_COM_LARGE_FILE | EXT4_FRO_COM_HUGE_FILE | EXT4_FRO_COM_DIR_NLINK | \
   EXT4_FRO_COM_EXTRA_ISIZE | EXT4_FRO_COM_METADATA_CSUM | EXT4_FRO_COM_GDT_CSUM)
#define W_JOURNAL_INCOMPAT (JBD_FEATURE_INCOMPAT_REVOKE | JBD_FEATURE_INCOMPAT_64BIT | JBD_FEATURE_INCOMPAT_CSUM_V3)

#define EXTENTS_FL 0x80000u
#define MAX_EXT_DEPTH 5

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

typedef struct {
  fl_bdev *dev;
  const uint8_t *sb;
  uint32_t bs, desc_size, groups;
  uint64_t blocks;
  bool csum, gdt_csum; /* metadata_csum, uninit_bg */
  FILE *trace;         /* diagnostic (fl_ext4_policy_explain) : détail de chaque contrôle, ou NULL */
} ctx;

static int read_block(const ctx *c, uint64_t blk, void *buf) {
  if (blk >= c->blocks) return FL_ECORRUPT;
  return fl_bdev_read_bytes(c->dev, blk * c->bs, c->bs, buf);
}

/* Graine des sommes metadata_csum. */
static uint32_t csum_seed(const uint8_t *sb) {
  if (le32(sb + 0x60) & EXT4_FINCOM_CSUM_SEED) return le32(sb + 0x270);
  return ext4_crc32c(EXT4_CRC32_INIT, sb + 0x68, 16);
}

/* Somme d'un descripteur de groupe (champ bg_checksum en 0x1E exclu), comme le noyau. */
static uint16_t desc_csum(const ctx *c, uint32_t g, const uint8_t *d) {
  uint8_t le_g[4] = {(uint8_t)g, (uint8_t)(g >> 8), (uint8_t)(g >> 16), (uint8_t)(g >> 24)};
  static const uint8_t zero[2];
  if (c->csum) {
    uint32_t crc = ext4_crc32c(csum_seed(c->sb), le_g, 4);
    crc = ext4_crc32c(crc, d, 0x1E);
    crc = ext4_crc32c(crc, zero, 2);
    if (c->desc_size > 0x20) crc = ext4_crc32c(crc, d + 0x20, c->desc_size - 0x20);
    return (uint16_t)crc;
  }
  uint16_t crc = ext4_bg_crc16(0xFFFF, c->sb + 0x68, 16);
  crc = ext4_bg_crc16(crc, le_g, 4);
  crc = ext4_bg_crc16(crc, d, 0x1E);
  if (c->desc_size > 0x20) crc = ext4_bg_crc16(crc, d + 0x20, c->desc_size - 0x20);
  return crc;
}

/* Vérifie tous les descripteurs ; *ok = false au premier faux. Copie celui du groupe 0 dans d0. */
static int check_descs(const ctx *c, bool *ok, uint8_t *d0) {
  uint64_t gdt = (uint64_t)le32(c->sb + 0x14) + 1; /* bloc suivant celui du superbloc */
  uint32_t per = c->bs / c->desc_size;
  uint8_t *b = malloc(c->bs);
  if (!b) return FL_ENOMEM;
  int rc = FL_OK;
  *ok = true;
  uint32_t bad = 0;
  /* En diagnostic, tous les groupes sont vérifiés (compte et premiers fautifs). */
  for (uint32_t g = 0; g < c->groups && (*ok || c->trace); g++) {
    if (g % per == 0 && (rc = read_block(c, gdt + g / per, b)) != FL_OK) break;
    const uint8_t *d = b + (size_t)(g % per) * c->desc_size;
    if (g == 0) memcpy(d0, d, c->desc_size);
    if (!(c->csum || c->gdt_csum)) continue;
    uint16_t calc = desc_csum(c, g, d), lu = le16(d + 0x1E);
    if (calc != lu) {
      *ok = false;
      if (c->trace && bad < 10)
        fprintf(c->trace, "  groupe %u : somme calculée 0x%04x, lue 0x%04x, drapeaux 0x%04x\n", g, calc, lu,
                le16(d + 0x12));
      bad++;
    }
  }
  if (c->trace)
    fprintf(c->trace, "descripteurs de groupe : %u groupes, %u octets chacun, table au bloc %llu, %u somme(s) fausse(s)%s\n",
            c->groups, c->desc_size, (unsigned long long)gdt, bad, (c->csum || c->gdt_csum) ? "" : " (pas de sommes)");
  free(b);
  return rc;
}

/* Bloc physique du bloc logique 0 d'un inode (i_block en 60 octets) ; 0 si absent. */
static int first_block(const ctx *c, uint32_t flags, const uint8_t *iblock, uint64_t *phys) {
  *phys = 0;
  if (!(flags & EXTENTS_FL)) {
    *phys = le32(iblock);
    return FL_OK;
  }
  uint8_t *b = malloc(c->bs);
  if (!b) return FL_ENOMEM;
  const uint8_t *node = iblock;
  uint32_t cap = 60;
  int rc = FL_OK;
  for (int lvl = 0; lvl <= MAX_EXT_DEPTH; lvl++) {
    uint16_t n = le16(node + 2), depth = le16(node + 6);
    if (le16(node) != 0xF30A || n == 0 || 12u + 12u * n > cap) {
      rc = FL_ECORRUPT;
      break;
    }
    const uint8_t *e = node + 12; /* entrées triées : la première couvre le bloc 0, s'il existe */
    if (depth == 0) {
      /* ee_len : jusqu'à 32768 (0x8000 compris), extent initialisé ; au-delà, non écrit, longueur
       * ee_len - 32768 (ext4_ext_get_actual_len). Un journal de 1 Gio est fait d'extents de 32768. */
      uint16_t raw = le16(e + 4);
      uint32_t len = raw <= 0x8000 ? raw : raw - 0x8000u;
      if (le32(e) == 0 && len) *phys = (uint64_t)le16(e + 6) << 32 | le32(e + 8);
      break;
    }
    if (le32(e) != 0) break;
    uint64_t child = (uint64_t)le16(e + 8) << 32 | le32(e + 4);
    if ((rc = read_block(c, child, b)) != FL_OK) break;
    node = b;
    cap = c->bs;
  }
  if (rc == FL_OK && *phys == 0) rc = FL_ECORRUPT;
  free(b);
  return rc;
}

/* Superbloc du journal (inode 8, bloc logique 0) dans jsb[1024]. */
static int read_journal_sb(const ctx *c, const uint8_t *d0, uint8_t jsb[1024]) {
  uint16_t isz = le16(c->sb + 0x58);
  uint64_t itable = le32(d0 + 0x08);
  if (c->desc_size >= 64) itable |= (uint64_t)le32(d0 + 0x28) << 32;
  uint64_t off = (uint64_t)(8 - 1) * isz;
  uint8_t *b = malloc(c->bs);
  if (!b) return FL_ENOMEM;
  int rc = read_block(c, itable + off / c->bs, b);
  uint64_t phys = 0;
  if (rc == FL_OK) {
    const uint8_t *ino = b + off % c->bs;
    uint8_t iblock[60];
    memcpy(iblock, ino + 0x28, sizeof iblock);
    rc = first_block(c, le32(ino + 0x20), iblock, &phys);
  }
  if (rc == FL_OK) rc = read_block(c, phys, b);
  if (rc == FL_OK) memcpy(jsb, b, 1024);
  free(b);
  return rc;
}

static void set(bool *rw, char reason[128], const char *r) {
  *rw = r == NULL;
  snprintf(reason, 128, "%s", r ? r : "");
}

static int policy(fl_bdev *dev, const uint8_t sb[1024], bool ignore_wp, FILE *trace, bool *rw,
                  char reason[128]) {
  if (dev->read_only && !ignore_wp) {
    set(rw, reason, FL_RO_WP);
    return FL_OK;
  }
  uint32_t compat = le32(sb + 0x5C), incompat = le32(sb + 0x60), ro_compat = le32(sb + 0x64);
  ctx c = {.dev = dev, .sb = sb, .bs = 1024u << le32(sb + 0x18), .trace = trace};
  c.desc_size = (incompat & EXT4_FINCOM_64BIT) ? le16(sb + 0xFE) : 32;
  c.blocks = le32(sb + 0x04);
  if (incompat & EXT4_FINCOM_64BIT) c.blocks |= (uint64_t)le32(sb + 0x150) << 32;
  c.groups = (uint32_t)((c.blocks - le32(sb + 0x14) + le32(sb + 0x20) - 1) / le32(sb + 0x20));
  c.csum = ro_compat & EXT4_FRO_COM_METADATA_CSUM;
  c.gdt_csum = !c.csum && (ro_compat & EXT4_FRO_COM_GDT_CSUM);
  bool journal = compat & EXT4_FCOM_HAS_JOURNAL;
  if (trace)
    fprintf(trace, "superbloc : blocs de %u octets, %llu blocs, compat 0x%x, incompat 0x%x, ro_compat 0x%x, état 0x%x, "
            "erreurs %u\n", c.bs, (unsigned long long)c.blocks, compat, incompat, ro_compat, le16(sb + 0x3A),
            le32(sb + 0x194));

  /* 2. Options. Avant toute lecture qui en dépend (meta_bg déplace les descripteurs). */
  if ((incompat & ~(uint32_t)W_INCOMPAT) || !(incompat & EXT4_FINCOM_EXTENTS) ||
      (ro_compat & ~(uint32_t)W_RO_COMPAT) || (journal && le32(sb + 0xE0) != 8)) {
    set(rw, reason, FL_RO_FEATURES);
    return FL_OK;
  }
  /* Taille de bloc lisible par la suite : 1 à 64 Kio (sb_validate) ; règle 7 plus bas. */
  bool descs_ok = true;
  uint8_t d0[1024];
  int rc = check_descs(&c, &descs_ok, d0);
  if (rc != FL_OK) return rc;
  uint8_t jsb[1024];
  bool jsb_ok = true;
  if (journal) {
    rc = read_journal_sb(&c, d0, jsb);
    if (rc == FL_ECORRUPT) jsb_ok = false;
    else if (rc != FL_OK) return rc;
    if (jsb_ok && (be32(jsb) != JBD_MAGIC_NUMBER || (be32(jsb + 4) != 3 && be32(jsb + 4) != 4))) jsb_ok = false;
    if (trace) {
      if (rc == FL_ECORRUPT) fputs("journal : superbloc du journal introuvable (inode 8 illisible)\n", trace);
      else
        fprintf(trace, "journal : magic 0x%08x, type %u, compat 0x%x, incompat 0x%x, ro_compat 0x%x, début %u, séquence %u\n",
                be32(jsb), be32(jsb + 4), be32(jsb + 0x24), be32(jsb + 0x28), be32(jsb + 0x2C), be32(jsb + 0x1C),
                be32(jsb + 0x18));
    }
    /* Journal v2 : options incompat hors de celles écrites correctement, ou toute option compat
     * (JBD_FEATURE_COMPAT_CHECKSUM, sommes v1 : lwext4 ne les écrit pas comme jbd2) ou ro_compat. */
    if (jsb_ok && be32(jsb + 4) == 4 &&
        ((be32(jsb + 0x28) & ~(uint32_t)W_JOURNAL_INCOMPAT) || be32(jsb + 0x24) || be32(jsb + 0x2C))) {
      set(rw, reason, FL_RO_FEATURES);
      return FL_OK;
    }
  }
  /* 3 à 5. */
  if (!journal) {
    set(rw, reason, FL_RO_NOJOURNAL);
    return FL_OK;
  }
  if (incompat & EXT4_FINCOM_RECOVER) {
    set(rw, reason, FL_RO_RECOVERY);
    return FL_OK;
  }
  uint16_t state = le16(sb + 0x3A);
  if (!(state & EXT4_SUPERBLOCK_STATE_VALID_FS) || (state & EXT4_SUPERBLOCK_STATE_ERROR_FS) ||
      le32(sb + 0x194) != 0) {
    set(rw, reason, FL_RO_ERRORS);
    return FL_OK;
  }
  /* 6. Sommes : superbloc, descripteurs, superbloc du journal (v3 : crc32c sur 1024 octets, champ
   * s_checksum en 0xFC à 0). Un superbloc de journal illisible compte comme une somme fausse. */
  uint32_t sb_calc = ext4_crc32c(EXT4_CRC32_INIT, sb, 0x3FC);
  bool sb_ok = !c.csum || sb_calc == le32(sb + 0x3FC);
  if (trace && c.csum)
    fprintf(trace, "somme du superbloc : calculée 0x%08x, lue 0x%08x\n", sb_calc, le32(sb + 0x3FC));
  if (jsb_ok && (be32(jsb + 0x28) & JBD_FEATURE_INCOMPAT_CSUM_V3)) {
    uint8_t t[1024];
    memcpy(t, jsb, sizeof t);
    memset(t + 0xFC, 0, 4);
    uint32_t j_calc = ext4_crc32c(EXT4_CRC32_INIT, t, sizeof t);
    jsb_ok = j_calc == be32(jsb + 0xFC);
    if (trace) fprintf(trace, "somme du superbloc du journal : calculée 0x%08x, lue 0x%08x\n", j_calc, be32(jsb + 0xFC));
  }
  if (!sb_ok || !descs_ok || !jsb_ok) {
    set(rw, reason, FL_RO_CSUM);
    return FL_OK;
  }
  /* 7. */
  if (c.bs > 4096) {
    set(rw, reason, FL_RO_BLOCKSIZE);
    return FL_OK;
  }
  set(rw, reason, NULL);
  return FL_OK;
}

int fl_ext4_write_policy(fl_bdev *dev, const uint8_t sb[1024], bool *rw, char reason[128]) {
  return policy(dev, sb, false, NULL, rw, reason);
}

int fl_ext4_policy_explain(fl_bdev *dev, FILE *out) {
  uint8_t sb[1024];
  int rc = fl_bdev_read_bytes(dev, 1024, sizeof sb, sb);
  if (rc != FL_OK) return rc;
  uint64_t nblocks = le32(sb + 0x04);
  if (le32(sb + 0x60) & EXT4_FINCOM_64BIT) nblocks |= (uint64_t)le32(sb + 0x150) << 32;
  if (le16(sb + 0x38) != 0xEF53 || le32(sb + 0x18) > 6 || le32(sb + 0x20) == 0 || le32(sb + 0x14) >= nblocks ||
      ((le32(sb + 0x60) & EXT4_FINCOM_64BIT) && (le16(sb + 0xFE) < 32 || le16(sb + 0xFE) > 1024 ||
                                                 (le16(sb + 0xFE) & (le16(sb + 0xFE) - 1)))))
    return FL_ECORRUPT;
  bool rw = false;
  char reason[128];
  rc = policy(dev, sb, true, out, &rw, reason);
  if (rc != FL_OK) return rc;
  fprintf(out, "verdict (protection en écriture du disque ignorée) : %s\n",
          rw ? "écriture autorisée" : reason);
  return FL_OK;
}

void fl_ext4_policy_log(fl_bdev *dev) {
#ifdef __ANDROID__
  char *buf = NULL;
  size_t len = 0;
  FILE *m = open_memstream(&buf, &len);
  if (!m) return;
  int rc = fl_ext4_policy_explain(dev, m);
  fclose(m);
  if (rc != FL_OK) __android_log_print(ANDROID_LOG_WARN, "FluLuksPolicy", "diagnostic : %s", fl_strerror(rc));
  for (char *line = buf, *nl; line && *line; line = nl ? nl + 1 : NULL) {
    nl = strchr(line, '\n');
    if (nl) *nl = 0;
    __android_log_write(ANDROID_LOG_WARN, "FluLuksPolicy", line);
  }
  free(buf);
#else
  (void)dev;
#endif
}
