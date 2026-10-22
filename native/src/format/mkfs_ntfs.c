#include "fluluks/error.h"
#include "fluluks/format.h"
#include "format/format_internal.h"
#include "fs/fs_internal.h"

#define MIB (1024ull * 1024ull)

int fl_mkfs_ntfs(fl_bdev *part, const char *label, uint64_t hidden_sectors) {
  if (!part) return FL_EINVAL;
  if (!label) label = "";
  int n = fl_label_utf16_units(label);
  if (n < 0 || n > 32) return FL_EINVAL; /* 32 caractères : limite de Windows */
  if (hidden_sectors > UINT32_MAX) return FL_EINVAL;
  if (fl_bdev_size_bytes(part) < 8 * MIB) return FL_ENOSPC;
  uint8_t rnd[24];
  int rc = fl_random(rnd, sizeof rnd);
  if (rc != FL_OK) return rc;
  return fl_ntfs_mkfs(part, label, (uint32_t)hidden_sectors, rnd);
}
