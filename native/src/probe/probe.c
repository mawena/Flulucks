#include "fluluks/probe.h"
#include <stdint.h>
#include <string.h>
#include "fluluks/error.h"

/* Lecture tolérante : hors bornes ou erreur = signature absente. */
static bool rd(fl_bdev *d, uint64_t off, size_t len, void *buf) {
  return fl_bdev_read_bytes(d, off, len, buf) == FL_OK;
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

int fl_probe(fl_bdev *d, fl_probe_result *out) {
  if (!d || !out) return FL_EINVAL;
  memset(out, 0, sizeof *out);
  out->content = FL_CONTENT_UNKNOWN;

  uint8_t b[512];
  if (rd(d, 0, sizeof b, b)) {
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
    if (memcmp(b + 3, "EXFAT   ", 8) == 0) { out->content = FL_CONTENT_EXFAT; return FL_OK; }
    if (probe_fat(b, out)) return FL_OK;
  }

  uint8_t sb[136]; /* superbloc ext à 1024, champs jusqu'à 1160 */
  if (rd(d, 1024, sizeof sb, sb)) {
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

  if (rd(d, 512, 32, b) && memcmp(b, "LABELONE", 8) == 0 && memcmp(b + 24, "LVM2 001", 8) == 0) {
    out->content = FL_CONTENT_LVM2;
    return FL_OK;
  }
  if (rd(d, 0, 4, b) && memcmp(b, "XFSB", 4) == 0) { out->content = FL_CONTENT_XFS; return FL_OK; }
  if (rd(d, 65600, 8, b) && memcmp(b, "_BHRfS_M", 8) == 0) { out->content = FL_CONTENT_BTRFS; return FL_OK; }
  if (rd(d, 4086, 10, b) && memcmp(b, "SWAPSPACE2", 10) == 0) { out->content = FL_CONTENT_SWAP; return FL_OK; }
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
  return c == FL_CONTENT_LUKS1 || c == FL_CONTENT_LUKS2 || c == FL_CONTENT_EXT4;
}
