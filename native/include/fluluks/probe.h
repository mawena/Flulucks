#ifndef FLULUKS_PROBE_H
#define FLULUKS_PROBE_H

#include <stdbool.h>
#include "fluluks/blockdev.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  FL_CONTENT_UNKNOWN = 0, FL_CONTENT_LUKS1, FL_CONTENT_LUKS2, FL_CONTENT_EXT4, FL_CONTENT_FAT12,
  FL_CONTENT_FAT16, FL_CONTENT_FAT32, FL_CONTENT_EXFAT, FL_CONTENT_NTFS, FL_CONTENT_LVM2,
  FL_CONTENT_BITLOCKER, FL_CONTENT_BTRFS, FL_CONTENT_XFS, FL_CONTENT_SWAP
} fl_content;

typedef struct {
  fl_content content;
  char label[64]; /* toujours terminé par NUL ; "" si indisponible */
  char uuid[40];  /* toujours terminé par NUL ; "" si indisponible */
} fl_probe_result;

/* Identifie le contenu de d. FL_OK même si UNKNOWN ; une lecture hors bornes = signature absente. */
int fl_probe(fl_bdev *d, fl_probe_result *out);
/* Nom lisible, jamais NULL ("inconnu" pour UNKNOWN ou valeur invalide). */
const char *fl_content_name(fl_content c);
/* Vrai pour LUKS1, LUKS2, ext4 (phase 1). */
bool fl_content_supported(fl_content c);

#ifdef __cplusplus
}
#endif

#endif
