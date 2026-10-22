#include <string.h>

#include "fluluks/error.h"
#include "format/format_internal.h"

int fl_label_utf16_units(const char *s) {
  int n = 0;
  const unsigned char *p = (const unsigned char *)s;
  while (*p) {
    uint32_t c;
    int len;
    if (*p < 0x80) { c = *p; len = 1; }
    else if ((*p & 0xE0) == 0xC0) { c = *p & 0x1F; len = 2; }
    else if ((*p & 0xF0) == 0xE0) { c = *p & 0x0F; len = 3; }
    else if ((*p & 0xF8) == 0xF0) { c = *p & 0x07; len = 4; }
    else return -1;
    for (int i = 1; i < len; i++) {
      if ((p[i] & 0xC0) != 0x80) return -1;
      c = c << 6 | (p[i] & 0x3F);
    }
    if (c < 0x20 || c == 0x7F) return -1;
    n += c >= 0x10000 ? 2 : 1;
    p += len;
  }
  return n;
}

int fl_fmt_check_label(fl_fmt_fs fs, const char *label) {
  if (!label) label = "";
  int n = fl_label_utf16_units(label);
  switch (fs) {
    case FL_FMT_FAT32:
      if (strlen(label) > 11) return FL_EINVAL;
      for (const unsigned char *p = (const unsigned char *)label; *p; p++)
        if (*p < 0x20 || *p > 0x7E || strchr("\"*+,./:;<=>?[\\]|", *p)) return FL_EINVAL;
      return FL_OK;
    case FL_FMT_EXFAT: return n >= 0 && n <= 11 ? FL_OK : FL_EINVAL;
    case FL_FMT_EXT4: return n >= 0 && strlen(label) <= 16 ? FL_OK : FL_EINVAL; /* s_volume_name */
    case FL_FMT_NTFS: return n >= 0 && n <= 32 ? FL_OK : FL_EINVAL; /* limite de Windows */
  }
  return FL_EINVAL;
}
