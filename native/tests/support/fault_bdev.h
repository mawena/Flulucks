#ifndef FL_TT_FAULT_BDEV_H
#define FL_TT_FAULT_BDEV_H
/* Périphérique bloc de test qui enveloppe un parent et simule une panne d'écriture. */

#include <stdint.h>

#include "fluluks/blockdev.h"

/* Enveloppe un parent : après `writes_ok` écritures réussies, toute écriture rend `err`
 * (FL_EIO ou FL_ENODEV) sans rien écrire ; les lectures continuent. writes_ok = UINT64_MAX : jamais.
 * Le parent n'est pas possédé (le détruire est à la charge de l'appelant, après le wrapper). */
int fl_fault_bdev_new(fl_bdev *parent, uint64_t writes_ok, int err, fl_bdev **out);
uint64_t fl_fault_bdev_writes(const fl_bdev *d);   /* écritures réussies à ce jour */
uint64_t fl_fault_bdev_flushes(const fl_bdev *d);  /* appels flush */

#endif
