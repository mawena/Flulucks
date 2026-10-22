#ifndef FLULUKS_FORMAT_H
#define FLULUKS_FORMAT_H

#include <stdbool.h>
#include <stdint.h>
#include "fluluks/blockdev.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Formatage d'un disque USB (phase 8). Tout écrit sur `d` : jamais d'appel sur un disque monté. */

typedef enum { FL_FMT_FAT32 = 1, FL_FMT_EXFAT, FL_FMT_EXT4, FL_FMT_NTFS } fl_fmt_fs;

/* Table à une seule partition alignée sur 1 Mio, jusqu'à la fin du disque (au Mio près) : MBR jusqu'à
 * 2 Tio, GPT au-delà. Type selon `fs` (LUKS : partition Linux). Le Mio de tête et le dernier Mio sont
 * effacés (anciens GPT). Rend la place de la partition, en secteurs de `d`. */
int fl_part_write_single(fl_bdev *d, fl_fmt_fs fs, bool encrypted, uint64_t *first_lba, uint64_t *count);

/* `len` octets d'aléa de /dev/urandom ; FL_EIO si la lecture échoue (jamais d'aléa faible). */
int fl_random(void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif
