#include <string.h>
#include "fluluks/error.h"
#include "fluluks/partition.h"
#include "part/part_internal.h"

static void make_none(fl_bdev *d, fl_part_table *out) {
  uint32_t skipped = out->skipped;
  memset(out, 0, sizeof *out);
  out->skipped = skipped;
  out->kind = FL_PT_NONE;
  out->count = 1;
  out->parts[0].index = 0;
  out->parts[0].first_lba = 0;
  out->parts[0].sector_count = d->sector_count;
}

int fl_part_scan(fl_bdev *d, fl_part_table *out) {
  uint8_t s0[512];
  int rc;
  if (!d || !out) return FL_EINVAL;
  memset(out, 0, sizeof *out);

  if (fl_bdev_size_bytes(d) < sizeof s0) {
    make_none(d, out);
    return FL_OK;
  }
  rc = fl_bdev_read_bytes(d, 0, sizeof s0, s0);
  if (rc != FL_OK) return rc;

  /* Un en-tête LUKS (sel LUKS1 sur 496-535) n'est jamais une table MBR. */
  if (memcmp(s0, "LUKS\xBA\xBE", 6) == 0) {
    make_none(d, out);
    return FL_OK;
  }

  if (s0[510] == 0x55 && s0[511] == 0xAA) {
    int protective = 0;
    for (int i = 0; i < 4; i++)
      if (s0[446 + 16 * i + 4] == 0xEE) protective = 1;
    if (protective) {
      rc = fl_gpt_read(d, out);
      if (rc == FL_OK && out->count > 0) return FL_OK;
      if (rc != FL_OK && rc != FL_ECORRUPT) return rc;
      uint32_t skipped = out->skipped; /* GPT inutilisable : repli sur les entrées MBR */
      memset(out, 0, sizeof *out);
      out->skipped = skipped;
    }
    rc = fl_mbr_read(d, s0, out);
    if (rc != FL_OK) return rc;
    if (out->count > 0) {
      out->kind = FL_PT_MBR;
      return FL_OK;
    }
  }
  make_none(d, out);
  return FL_OK;
}
