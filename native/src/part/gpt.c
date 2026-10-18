#include <stdlib.h>
#include <string.h>
#include "fluluks/error.h"
#include "part/crc32.h"
#include "part/part_internal.h"

#define GPT_MAX_ENTRIES 1024u
#define GPT_MAX_ESIZE 4096u

static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }

static void put_utf8(char *dst, size_t cap, size_t *pos, uint32_t cp, int *full) {
  char tmp[4];
  size_t n;
  if (cp < 0x80) { tmp[0] = (char)cp; n = 1; }
  else if (cp < 0x800) { tmp[0] = (char)(0xC0 | cp >> 6); tmp[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
  else if (cp < 0x10000) {
    tmp[0] = (char)(0xE0 | cp >> 12); tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    tmp[2] = (char)(0x80 | (cp & 0x3F)); n = 3;
  } else {
    tmp[0] = (char)(0xF0 | cp >> 18); tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); tmp[3] = (char)(0x80 | (cp & 0x3F)); n = 4;
  }
  if (*pos + n > cap) { *full = 1; return; }
  memcpy(dst + *pos, tmp, n);
  *pos += n;
}

/* UTF-16LE (36 unités) -> UTF-8, NUL-terminé, tronqué sur une frontière de caractère. */
static void name_to_utf8(const uint8_t *src, char *dst, size_t dstsz) {
  size_t pos = 0;
  int full = 0;
  for (int i = 0; i < 36 && !full; i++) {
    uint32_t u = (uint32_t)src[2 * i] | (uint32_t)src[2 * i + 1] << 8;
    if (u == 0) break;
    if (u >= 0xD800 && u <= 0xDBFF) {
      uint32_t lo = 0;
      if (i + 1 < 36) lo = (uint32_t)src[2 * i + 2] | (uint32_t)src[2 * i + 3] << 8;
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
        i++;
      } else u = 0xFFFD;
    } else if (u >= 0xDC00 && u <= 0xDFFF) u = 0xFFFD;
    put_utf8(dst, dstsz - 1, &pos, u, &full);
  }
  dst[pos] = '\0';
}

/* Lit et valide l'en-tête à `hdr_lba`, puis le tableau d'entrées. */
static int try_header(fl_bdev *d, uint64_t hdr_lba, fl_part_table *out) {
  uint64_t total = d->sector_count, ss = d->sector_size;
  uint8_t h[512];
  uint8_t hb[512];
  if (ss < 512 || hdr_lba >= total) return FL_ECORRUPT;
  int rc = fl_bdev_read_bytes(d, hdr_lba * ss, sizeof h, h);
  if (rc != FL_OK) return rc;
  if (memcmp(h, "EFI PART", 8) != 0) return FL_ECORRUPT;

  uint32_t hsize = rd32(h + 12);
  if (hsize < 92 || hsize > sizeof h || hsize > ss) return FL_ECORRUPT;
  memcpy(hb, h, hsize);
  memset(hb + 16, 0, 4);
  if (fl_crc32(hb, hsize) != rd32(h + 16)) return FL_ECORRUPT;
  if (rd64(h + 24) != hdr_lba) return FL_ECORRUPT;

  uint64_t ent_lba = rd64(h + 72);
  uint32_t nent = rd32(h + 80), esize = rd32(h + 84), ecrc = rd32(h + 88);
  if (nent == 0 || nent > GPT_MAX_ENTRIES) return FL_ECORRUPT;
  if (esize < 128 || esize > GPT_MAX_ESIZE || (esize & 7) != 0) return FL_ECORRUPT;

  size_t bytes = (size_t)nent * esize;
  uint64_t disk = fl_bdev_size_bytes(d);
  if (ent_lba > UINT64_MAX / ss) return FL_ECORRUPT;
  if (!fl_range_ok(ent_lba * ss, bytes, disk)) return FL_ECORRUPT;

  uint8_t *ents = malloc(bytes);
  if (!ents) return FL_ENOMEM;
  rc = fl_bdev_read_bytes(d, ent_lba * ss, bytes, ents);
  if (rc != FL_OK) { free(ents); return rc; }
  if (fl_crc32(ents, bytes) != ecrc) { free(ents); return FL_ECORRUPT; }

  memset(out, 0, sizeof *out);
  out->kind = FL_PT_GPT;
  for (uint32_t i = 0; i < nent; i++) {
    const uint8_t *e = ents + (size_t)i * esize;
    static const uint8_t zero[16];
    if (memcmp(e, zero, 16) == 0) continue;
    uint64_t first = rd64(e + 32), last = rd64(e + 40);
    if (first > last || last >= total) { out->skipped++; continue; }
    if (out->count >= FL_PART_MAX) { out->skipped++; continue; }
    fl_partition *p = &out->parts[out->count++];
    memset(p, 0, sizeof *p);
    p->index = i + 1;
    p->first_lba = first;
    p->sector_count = last - first + 1;
    memcpy(p->gpt_type, e, 16);
    name_to_utf8(e + 56, p->name, sizeof p->name);
  }
  free(ents);
  return FL_OK;
}

int fl_gpt_read(fl_bdev *d, fl_part_table *out) {
  int rc = try_header(d, 1, out);
  if (rc == FL_OK) return FL_OK;
  if (rc != FL_ECORRUPT) return rc;
  if (d->sector_count < 2) return FL_ECORRUPT;
  rc = try_header(d, d->sector_count - 1, out);
  if (rc == FL_OK) out->used_backup_gpt = true;
  return rc;
}
