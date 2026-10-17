#include <string.h>
#include "fluluks/error.h"
#include "part/part_internal.h"

#define MAX_EBR 128

static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int is_extended(uint8_t t) { return t == 0x05 || t == 0x0F || t == 0x85; }

static void add(fl_part_table *out, uint32_t index, uint64_t first, uint64_t count, uint8_t type) {
  if (out->count >= FL_PART_MAX) {
    out->skipped++;
    return;
  }
  fl_partition *p = &out->parts[out->count++];
  memset(p, 0, sizeof *p);
  p->index = index;
  p->first_lba = first;
  p->sector_count = count;
  p->mbr_type = type;
}

/* Suit la chaîne des EBR. Les décalages « suivant » sont relatifs au début de l'étendue. */
static int walk_extended(fl_bdev *d, uint64_t ext_start, uint64_t ext_len, fl_part_table *out) {
  uint64_t total = d->sector_count;
  uint64_t rel = 0; /* EBR courant, relatif à ext_start */
  uint32_t next_index = 5;
  uint64_t seen[MAX_EBR];
  uint8_t ebr[512];

  for (int n = 0; n < MAX_EBR; n++) {
    uint64_t ebr_lba;
    if (rel >= ext_len || ext_start > UINT64_MAX - rel) {
      out->skipped++;
      return FL_OK;
    }
    ebr_lba = ext_start + rel;
    if (ebr_lba >= total) {
      out->skipped++;
      return FL_OK;
    }
    for (int i = 0; i < n; i++)
      if (seen[i] == ebr_lba) return FL_OK; /* boucle */
    seen[n] = ebr_lba;

    if (fl_bdev_read_bytes(d, ebr_lba * d->sector_size, sizeof ebr, ebr) != FL_OK) return FL_OK;
    if (ebr[510] != 0x55 || ebr[511] != 0xAA) return FL_OK;

    const uint8_t *e1 = ebr + 446, *e2 = ebr + 462;
    uint8_t t1 = e1[4];
    uint64_t c1 = rd32(e1 + 12);
    if (t1 != 0 && c1 != 0 && !is_extended(t1)) {
      uint64_t first = ebr_lba + rd32(e1 + 8);
      if (fl_range_ok(first, c1, total)) add(out, next_index++, first, c1, t1);
      else out->skipped++;
    }
    if (!is_extended(e2[4]) || rd32(e2 + 12) == 0) return FL_OK;
    rel = rd32(e2 + 8);
    if (rel == 0) return FL_OK;
  }
  return FL_OK;
}

int fl_mbr_read(fl_bdev *d, const uint8_t *s0, fl_part_table *out) {
  uint64_t total = d->sector_count;
  uint64_t ext_start = 0, ext_len = 0;
  int have_ext = 0;

  for (int i = 0; i < 4; i++) {
    const uint8_t *e = s0 + 446 + 16 * i;
    uint8_t type = e[4];
    uint64_t first = rd32(e + 8), count = rd32(e + 12);
    if (type == 0 || type == 0xEE || count == 0) continue;
    if (!fl_range_ok(first, count, total)) {
      out->skipped++;
      continue;
    }
    if (is_extended(type)) {
      if (!have_ext) {
        have_ext = 1;
        ext_start = first;
        ext_len = count;
      }
      continue;
    }
    add(out, (uint32_t)i + 1, first, count, type);
  }
  if (have_ext) return walk_extended(d, ext_start, ext_len, out);
  return FL_OK;
}
