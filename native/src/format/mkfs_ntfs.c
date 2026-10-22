#include "fluluks/error.h"
#include "fluluks/format.h"
#include "format/format_internal.h"
#include "fs/fs_internal.h"

#define MIB (1024ull * 1024ull)

int fl_mkfs_ntfs(fl_bdev *part, const char *label, uint64_t hidden_sectors) {
  if (!part) return FL_EINVAL;
  if (!label) label = "";
  if (fl_fmt_check_label(FL_FMT_NTFS, label) != FL_OK) return FL_EINVAL;
  if (hidden_sectors > UINT32_MAX) return FL_EINVAL;
  if (fl_bdev_size_bytes(part) < 8 * MIB) return FL_ENOSPC;
  uint8_t rnd[24];
  int rc = fl_random(rnd, sizeof rnd);
  if (rc != FL_OK) return rc;
  return fl_ntfs_mkfs(part, label, (uint32_t)hidden_sectors, rnd);
}
