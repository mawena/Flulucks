#include <stdlib.h>
#include <string.h>

#include "fluluks/error.h"
#include "fluluks/format.h"
#include "part/crc32.h"

#define MIB (1024u * 1024u)
#define GPT_ENTRIES 128u
#define GPT_ENTRY_SIZE 128u

/* Types GPT, en octets sur le disque (trois premiers champs en petit-boutiste). */
static const uint8_t TYPE_LINUX[16] = {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
                                       0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4}; /* 0FC63DAF-… */
static const uint8_t TYPE_MSDATA[16] = {0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
                                        0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7}; /* EBD0A0A2-… */

static void le32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void le64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }

/* GUID aléatoire (version 4, variante RFC 4122), dans l'ordre des octets de GPT. */
static int random_guid(uint8_t g[16]) {
  int rc = fl_random(g, 16);
  if (rc != FL_OK) return rc;
  g[7] = (uint8_t)((g[7] & 0x0F) | 0x40);
  g[8] = (uint8_t)((g[8] & 0x3F) | 0x80);
  return FL_OK;
}

/* Écrit `n` secteurs de zéros à partir de `lba`, par paquets d'au plus 1 Mio. */
static int zero(fl_bdev *d, uint64_t lba, uint64_t n) {
  uint32_t per = MIB / d->sector_size;
  uint8_t *z = calloc(per, d->sector_size);
  if (!z) return FL_ENOMEM;
  int rc = FL_OK;
  while (n > 0 && rc == FL_OK) {
    uint32_t k = n < per ? (uint32_t)n : per;
    rc = fl_bdev_write(d, lba, k, z);
    lba += k;
    n -= k;
  }
  free(z);
  return rc;
}

static uint8_t mbr_type(fl_fmt_fs fs, bool encrypted) {
  if (encrypted || fs == FL_FMT_EXT4) return 0x83;
  return fs == FL_FMT_FAT32 ? 0x0C : 0x07;
}

/* Secteur 0 : une entrée (type, début, nombre), signature de disque aléatoire. */
static int write_mbr(fl_bdev *d, uint8_t type, uint64_t first, uint64_t count) {
  uint8_t *s = calloc(1, d->sector_size);
  if (!s) return FL_ENOMEM;
  int rc = fl_random(s + 440, 4);
  if (rc == FL_OK) {
    uint8_t *e = s + 446;
    e[0] = 0x00;
    e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF; /* CHS hors limites : adressage LBA */
    e[4] = type;
    e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;
    le32(e + 8, (uint32_t)first);
    le32(e + 12, (uint32_t)count);
    s[510] = 0x55;
    s[511] = 0xAA;
    rc = fl_bdev_write(d, 0, 1, s);
  }
  free(s);
  return rc;
}

/* En-tête GPT (92 octets utiles) dans un secteur ; `entries_crc` calculé par l'appelant. */
static void gpt_header(uint8_t *s, uint64_t self, uint64_t alt, uint64_t first_usable, uint64_t last_usable,
                       const uint8_t guid[16], uint64_t entries_lba, uint32_t entries_crc) {
  memcpy(s, "EFI PART", 8);
  le32(s + 8, 0x00010000);
  le32(s + 12, 92);
  le64(s + 24, self);
  le64(s + 32, alt);
  le64(s + 40, first_usable);
  le64(s + 48, last_usable);
  memcpy(s + 56, guid, 16);
  le64(s + 72, entries_lba);
  le32(s + 80, GPT_ENTRIES);
  le32(s + 84, GPT_ENTRY_SIZE);
  le32(s + 88, entries_crc);
  le32(s + 16, 0);
  le32(s + 16, fl_crc32(s, 92));
}

static int write_gpt(fl_bdev *d, const uint8_t type[16], uint64_t first, uint64_t count) {
  uint32_t ss = d->sector_size;
  uint64_t total = d->sector_count;
  uint64_t esec = (GPT_ENTRIES * GPT_ENTRY_SIZE + ss - 1) / ss;
  uint8_t disk_guid[16], part_guid[16];
  int rc = random_guid(disk_guid);
  if (rc == FL_OK) rc = random_guid(part_guid);
  if (rc != FL_OK) return rc;

  uint8_t *ent = calloc(esec, ss);
  uint8_t *s = calloc(1, ss);
  if (!ent || !s) {
    free(ent);
    free(s);
    return FL_ENOMEM;
  }
  memcpy(ent, type, 16);
  memcpy(ent + 16, part_guid, 16);
  le64(ent + 32, first);
  le64(ent + 40, first + count - 1);
  uint32_t ecrc = fl_crc32(ent, GPT_ENTRIES * GPT_ENTRY_SIZE);
  uint64_t first_usable = 2 + esec, last_usable = total - 2 - esec;

  /* MBR protecteur */
  uint8_t *e = s + 446;
  e[1] = 0x00; e[2] = 0x02; e[3] = 0x00;
  e[4] = 0xEE;
  e[5] = 0xFF; e[6] = 0xFF; e[7] = 0xFF;
  le32(e + 8, 1);
  le32(e + 12, total - 1 > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)(total - 1));
  s[510] = 0x55;
  s[511] = 0xAA;
  rc = fl_bdev_write(d, 0, 1, s);
  /* principal : en-tête en 1, entrées en 2 ; secours : entrées puis en-tête en fin de disque */
  if (rc == FL_OK) rc = fl_bdev_write(d, 2, (uint32_t)esec, ent);
  if (rc == FL_OK) rc = fl_bdev_write(d, total - 1 - esec, (uint32_t)esec, ent);
  if (rc == FL_OK) {
    memset(s, 0, ss);
    gpt_header(s, total - 1, 1, first_usable, last_usable, disk_guid, total - 1 - esec, ecrc);
    rc = fl_bdev_write(d, total - 1, 1, s);
  }
  if (rc == FL_OK) {
    memset(s, 0, ss);
    gpt_header(s, 1, total - 1, first_usable, last_usable, disk_guid, 2, ecrc);
    rc = fl_bdev_write(d, 1, 1, s);
  }
  free(ent);
  free(s);
  return rc;
}

int fl_part_write_single(fl_bdev *d, fl_fmt_fs fs, bool encrypted, uint64_t *first_lba, uint64_t *count) {
  if (!d || !first_lba || !count || d->sector_size < 512 || d->sector_size > MIB) return FL_EINVAL;
  if (fs < FL_FMT_FAT32 || fs > FL_FMT_NTFS) return FL_EINVAL;
  if (d->read_only) return FL_EROFS;
  uint64_t align = MIB / d->sector_size;
  uint64_t total = d->sector_count;
  if (total < 4 * align) return FL_ENOSPC;
  bool gpt = total * d->sector_size > 2ull * 1024 * 1024 * MIB;
  uint64_t esec = (GPT_ENTRIES * GPT_ENTRY_SIZE + d->sector_size - 1) / d->sector_size;
  uint64_t end = gpt ? total - 1 - esec : total; /* premier secteur hors partition possible */
  uint64_t first = align;
  uint64_t n = (end - first) / align * align;
  if (!gpt && n > 0xFFFFFFFFull) return FL_EINVAL; /* MBR : 2^32 secteurs au plus */

  /* anciens en-têtes (GPT, LUKS, superblocs) : premier et dernier Mio effacés */
  int rc = zero(d, 0, align);
  if (rc == FL_OK) rc = zero(d, total - align, align);
  if (rc == FL_OK) {
    rc = gpt ? write_gpt(d, (encrypted || fs == FL_FMT_EXT4) ? TYPE_LINUX : TYPE_MSDATA, first, n)
             : write_mbr(d, mbr_type(fs, encrypted), first, n);
  }
  if (rc == FL_OK) rc = fl_bdev_flush(d);
  if (rc != FL_OK) return rc;
  *first_lba = first;
  *count = n;
  return FL_OK;
}
