#include <string.h>

#include "fluluks/error.h"
#include "fluluks/format.h"
#include "fs/fs_internal.h"

/* Nombre de caractères (unités UTF-16) d'une chaîne UTF-8 valide sans caractère de contrôle ; -1 sinon. */
static int utf16_units(const char *s) {
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

int fl_mkfs_fat(fl_bdev *part, bool exfat, const char *label) {
  if (!part) return FL_EINVAL;
  if (!label) label = "";
  char up[12];
  if (exfat) {
    int n = utf16_units(label);
    if (n < 0 || n > 11) return FL_EINVAL;
  } else {
    /* FAT32 : 11 caractères ASCII imprimables, sans ceux que FAT interdit dans une étiquette */
    size_t n = strlen(label);
    if (n > 11) return FL_EINVAL;
    for (size_t i = 0; i < n; i++) {
      unsigned char c = (unsigned char)label[i];
      if (c < 0x20 || c > 0x7E || strchr("\"*+,./:;<=>?[\\]|", c)) return FL_EINVAL;
      up[i] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    up[n] = 0;
    label = up;
  }
  return fl_fat_mkfs(part, exfat, label);
}
