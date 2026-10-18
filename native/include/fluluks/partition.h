#ifndef FLULUKS_PARTITION_H
#define FLULUKS_PARTITION_H

#include <stdbool.h>
#include <stdint.h>
#include "fluluks/blockdev.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { FL_PT_NONE = 0, FL_PT_MBR = 1, FL_PT_GPT = 2 } fl_pt_kind;

typedef struct {
  uint32_t index;          /* GPT : 1..128 ; MBR : 1..4 primaires, 5.. logiques ; NONE : 0 */
  uint64_t first_lba, sector_count;
  uint8_t  mbr_type;       /* 0 en GPT */
  uint8_t  gpt_type[16];   /* zéros en MBR */
  char     name[73];       /* nom GPT en UTF-8 (converti depuis UTF-16LE), "" sinon */
} fl_partition;

typedef struct {
  fl_pt_kind kind;
  uint32_t count;
  fl_partition parts[128];
  uint32_t skipped;        /* entrées ignorées car hors bornes */
  bool used_backup_gpt;
} fl_part_table;

/* Analyse la table de partitions (GPT, MBR, MBR étendu). Sans table ou sans partition :
 * kind = FL_PT_NONE et une entrée d'index 0 couvrant tout le disque. */
int fl_part_scan(fl_bdev *d, fl_part_table *out);

#ifdef __cplusplus
}
#endif

#endif
