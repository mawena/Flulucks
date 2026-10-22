#include <string.h>

#include "fluluks/error.h"
#include "fluluks/format.h"
#include "format/format_internal.h"
#include "fs/fs_internal.h"

int fl_mkfs_fat(fl_bdev *part, bool exfat, const char *label) {
  if (!part) return FL_EINVAL;
  if (!label) label = "";
  int rc = fl_fmt_check_label(exfat ? FL_FMT_EXFAT : FL_FMT_FAT32, label);
  if (rc != FL_OK) return rc;
  char up[12];
  if (!exfat) { /* FAT32 : en majuscules */
    size_t n = strlen(label);
    for (size_t i = 0; i < n; i++) up[i] = (char)(label[i] >= 'a' && label[i] <= 'z' ? label[i] - 32 : label[i]);
    up[n] = 0;
    label = up;
  }
  return fl_fat_mkfs(part, exfat, label);
}
