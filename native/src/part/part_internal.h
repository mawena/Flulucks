#ifndef FL_PART_INTERNAL_H
#define FL_PART_INTERNAL_H

#include "fluluks/partition.h"

#define FL_PART_MAX 128

/* Lit le GPT (primaire puis secours). FL_OK si un en-tête valide a été lu (count peut être 0),
 * FL_ECORRUPT si aucun n'est valide, autre code en cas d'erreur d'E/S. */
int fl_gpt_read(fl_bdev *d, fl_part_table *out);

/* Lit les partitions du MBR (secteur 0 déjà lu, 512 octets, signature vérifiée), y compris
 * les logiques. Les entrées 0xEE sont ignorées. */
int fl_mbr_read(fl_bdev *d, const uint8_t *sector0, fl_part_table *out);

/* a + b <= limit sans dépassement. */
static inline int fl_range_ok(uint64_t a, uint64_t b, uint64_t limit) {
  return a <= limit && b <= limit - a;
}

#endif
