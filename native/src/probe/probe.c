#include "fluluks/probe.h"
#include <stdint.h>
#include <string.h>
#include "fluluks/error.h"

/* Lecture à trois états : 0 = données lues, 1 = hors bornes (signature absente),
 * <0 = vraie erreur d'E/S (à propager). */
#define RD_ABSENT 1
static int rd(fl_bdev *d, uint64_t off, size_t len, void *buf) {
  int rc = fl_bdev_read_bytes(d, off, len, buf);
  if (rc == FL_OK) return 0;
  if (rc == FL_EINVAL) return RD_ABSENT;
  return rc;
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Copie un champ de largeur fixe, retire espaces et NUL finaux, termine par NUL. */
static void set_label(char *dst, size_t dstsz, const uint8_t *src, size_t n) {
  if (n > dstsz - 1) n = dstsz - 1;
  size_t len = 0;
  while (len < n && src[len] != 0) len++; /* un NUL termine le champ */
  while (len > 0 && src[len - 1] == ' ') len--;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

/* Retourne true et remplit out si b (512 premiers octets) est un secteur de démarrage FAT cohérent. */
static bool probe_fat(const uint8_t *b, fl_probe_result *out) {
  if (b[510] != 0x55 || b[511] != 0xAA) return false;
  uint32_t bps = le16(b + 11), spc = b[13], rsvd = le16(b + 14), nfats = b[16];
  uint32_t rootent = le16(b + 17);
  if (bps != 512 && bps != 1024 && bps != 2048 && bps != 4096) return false;
  if (spc == 0 || (spc & (spc - 1)) != 0) return false;
  if (rsvd == 0 || nfats == 0) return false;
  uint64_t fatsz = le16(b + 22);
  if (fatsz == 0) fatsz = le32(b + 36);
  uint64_t tot = le16(b + 19);
  if (tot == 0) tot = le32(b + 32);
  if (fatsz == 0 || tot == 0) return false;
  uint64_t rootsec = ((uint64_t)rootent * 32 + bps - 1) / bps;
  uint64_t meta = rsvd + (uint64_t)nfats * fatsz + rootsec; /* < 2^40 : pas de débordement */
  if (tot <= meta) return false;
  uint64_t clusters = (tot - meta) / spc;
  if (clusters == 0) return false;
  size_t loff;
  if (clusters < 4085) {
    out->content = FL_CONTENT_FAT12;
    loff = 43;
  } else if (clusters < 65525) {
    out->content = FL_CONTENT_FAT16;
    loff = 43;
  } else {
    out->content = FL_CONTENT_FAT32;
    loff = 71;
  }
  if (memcmp(b + loff, "NO NAME    ", 11) != 0) set_label(out->label, sizeof out->label, b + loff, 11);
  return true;
}

/* Étiquette exFAT : entrée 0x83 du dossier racine (au plus 11 caractères UTF-16LE), cherchée dans les
 * 256 premières entrées du premier cluster. Ajoute le texte UTF-8 à out ; 0, ou une erreur d'E/S. */
static int probe_exfat_label(fl_bdev *d, const uint8_t *b, fl_probe_result *out) {
  unsigned bps_shift = b[108], spc_shift = b[109];
  uint64_t heap = le32(b + 88), root = le32(b + 96);
  if (bps_shift < 9 || bps_shift > 12 || spc_shift > 25 - bps_shift || root < 2) return 0;
  uint64_t off = (heap + ((root - 2) << spc_shift)) << bps_shift;
  uint64_t clus = 1ull << (bps_shift + spc_shift);
  uint8_t e[32 * 256];
  size_t len = clus < sizeof e ? (size_t)clus : sizeof e;
  int r = rd(d, off, len, e);
  if (r != 0) return r < 0 ? r : 0;
  for (size_t i = 0; i < len; i += 32) {
    if (e[i] == 0x00) return 0; /* fin du dossier */
    if (e[i] != 0x83) continue;
    unsigned n = e[i + 1] < 11 ? e[i + 1] : 11;
    size_t k = 0;
    for (unsigned j = 0; j < n; j++) {
      uint32_t c = le16(e + i + 2 + 2 * j);
      if (c >= 0xD800 && c <= 0xDBFF && j + 1 < n) { /* paire de substitution */
        uint32_t c2 = le16(e + i + 4 + 2 * j);
        if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
          c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
          j++;
        }
      }
      if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
      if (c < 0x80) {
        out->label[k++] = (char)c;
      } else if (c < 0x800) {
        out->label[k++] = (char)(0xC0 | c >> 6);
        out->label[k++] = (char)(0x80 | (c & 0x3F));
      } else if (c < 0x10000) {
        out->label[k++] = (char)(0xE0 | c >> 12);
        out->label[k++] = (char)(0x80 | ((c >> 6) & 0x3F));
        out->label[k++] = (char)(0x80 | (c & 0x3F));
      } else {
        out->label[k++] = (char)(0xF0 | c >> 18);
        out->label[k++] = (char)(0x80 | ((c >> 12) & 0x3F));
        out->label[k++] = (char)(0x80 | ((c >> 6) & 0x3F));
        out->label[k++] = (char)(0x80 | (c & 0x3F));
      }
    }
    out->label[k] = '\0'; /* 11 caractères de 4 octets au plus : 44 < sizeof label */
    return 0;
  }
  return 0;
}

int fl_probe(fl_bdev *d, fl_probe_result *out) {
  if (!d || !out) return FL_EINVAL;
  memset(out, 0, sizeof *out);
  out->content = FL_CONTENT_UNKNOWN;

  uint8_t b[512];
  int r = rd(d, 0, sizeof b, b);
  if (r < 0) return r;
  if (r == 0) {
    if (memcmp(b, "LUKS\xBA\xBE", 6) == 0) {
      unsigned ver = ((unsigned)b[6] << 8) | b[7];
      if (ver == 1 || ver == 2) {
        out->content = ver == 1 ? FL_CONTENT_LUKS1 : FL_CONTENT_LUKS2;
        size_t n = 0;
        while (n < 39 && b[168 + n] != 0) n++;
        memcpy(out->uuid, b + 168, n);
        out->uuid[n] = '\0';
        if (ver == 2) set_label(out->label, sizeof out->label, b + 24, 48);
        return FL_OK;
      }
    }
    if (memcmp(b + 3, "-FVE-FS-", 8) == 0) { out->content = FL_CONTENT_BITLOCKER; return FL_OK; }
    if (memcmp(b + 3, "NTFS    ", 8) == 0) { out->content = FL_CONTENT_NTFS; return FL_OK; }
    if (memcmp(b + 3, "EXFAT   ", 8) == 0) {
      out->content = FL_CONTENT_EXFAT;
      r = probe_exfat_label(d, b, out);
      return r < 0 ? r : FL_OK;
    }
    if (probe_fat(b, out)) return FL_OK;
  }

  uint8_t sb[136]; /* superbloc ext à 1024, champs jusqu'à 1160 */
  r = rd(d, 1024, sizeof sb, sb);
  if (r < 0) return r;
  if (r == 0) {
    const uint8_t *s = sb; /* s[i] = octet 1024+i */
    if (le16(s + 56) == 0xEF53) {
      static const char hex[] = "0123456789abcdef";
      out->content = FL_CONTENT_EXT4;
      const uint8_t *u = s + 104; /* 1128 */
      size_t k = 0;
      for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out->uuid[k++] = '-';
        out->uuid[k++] = hex[u[i] >> 4];
        out->uuid[k++] = hex[u[i] & 15];
      }
      out->uuid[k] = '\0';
      set_label(out->label, sizeof out->label, s + 120, 16); /* 1144 */
      return FL_OK;
    }
  }

  r = rd(d, 512, 32, b);
  if (r < 0) return r;
  if (r == 0 && memcmp(b, "LABELONE", 8) == 0 && memcmp(b + 24, "LVM2 001", 8) == 0) {
    out->content = FL_CONTENT_LVM2;
    return FL_OK;
  }
  r = rd(d, 0, 4, b);
  if (r < 0) return r;
  if (r == 0 && memcmp(b, "XFSB", 4) == 0) { out->content = FL_CONTENT_XFS; return FL_OK; }
  r = rd(d, 65600, 8, b);
  if (r < 0) return r;
  if (r == 0 && memcmp(b, "_BHRfS_M", 8) == 0) { out->content = FL_CONTENT_BTRFS; return FL_OK; }
  r = rd(d, 4086, 10, b);
  if (r < 0) return r;
  if (r == 0 && memcmp(b, "SWAPSPACE2", 10) == 0) { out->content = FL_CONTENT_SWAP; return FL_OK; }
  return FL_OK;
}

const char *fl_content_name(fl_content c) {
  switch (c) {
    case FL_CONTENT_LUKS1: return "LUKS1";
    case FL_CONTENT_LUKS2: return "LUKS2";
    case FL_CONTENT_EXT4: return "ext4";
    case FL_CONTENT_FAT12: return "FAT12";
    case FL_CONTENT_FAT16: return "FAT16";
    case FL_CONTENT_FAT32: return "FAT32";
    case FL_CONTENT_EXFAT: return "exFAT";
    case FL_CONTENT_NTFS: return "NTFS";
    case FL_CONTENT_LVM2: return "LVM2";
    case FL_CONTENT_BITLOCKER: return "BitLocker";
    case FL_CONTENT_BTRFS: return "btrfs";
    case FL_CONTENT_XFS: return "XFS";
    case FL_CONTENT_SWAP: return "swap";
    case FL_CONTENT_UNKNOWN: break;
  }
  return "inconnu";
}

bool fl_content_supported(fl_content c) {
  return c == FL_CONTENT_LUKS1 || c == FL_CONTENT_LUKS2 || c == FL_CONTENT_EXT4 || c == FL_CONTENT_FAT12 ||
         c == FL_CONTENT_FAT16 || c == FL_CONTENT_FAT32 || c == FL_CONTENT_EXFAT || c == FL_CONTENT_NTFS;
}
