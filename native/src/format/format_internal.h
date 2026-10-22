#ifndef FL_FORMAT_INTERNAL_H
#define FL_FORMAT_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "fluluks/blockdev.h"
#include "fluluks/format.h"

/* Nombre de caractères (unités UTF-16) d'une étiquette UTF-8 valide sans caractère de contrôle ; -1 sinon
 * (label.c). */
int fl_label_utf16_units(const char *s);
/* FL_OK si `label` (NULL = vide) convient au système de fichiers, FL_EINVAL sinon (label.c) : FAT32 11
 * caractères ASCII sans "*+,./:;<=>?[\]|, exFAT 11, ext4 16 octets, NTFS 32 (unités UTF-16). */
int fl_fmt_check_label(fl_fmt_fs fs, const char *label);
/* Place qu'aurait la partition de fl_part_write_single, sans rien écrire (part_write.c). */
int fl_part_layout(const fl_bdev *d, uint64_t *first_lba, uint64_t *count, bool *gpt);
/* Écrit `n` secteurs de zéros à partir de `lba`, par paquets d'au plus 1 Mio (part_write.c). */
int fl_fmt_zero(fl_bdev *d, uint64_t lba, uint64_t n);

#endif
