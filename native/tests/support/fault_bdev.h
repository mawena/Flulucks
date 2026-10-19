#ifndef FL_TT_FAULT_BDEV_H
#define FL_TT_FAULT_BDEV_H
/* Périphérique bloc de test qui enveloppe un parent et simule une panne d'écriture. */

#include <stddef.h>
#include <stdint.h>

#include "fluluks/blockdev.h"

/* Enveloppe un parent : après `writes_ok` écritures réussies, toute écriture rend `err`
 * (FL_EIO ou FL_ENODEV) sans rien écrire ; les lectures continuent. writes_ok = UINT64_MAX : jamais.
 * Le parent n'est pas possédé (le détruire est à la charge de l'appelant, après le wrapper). */
int fl_fault_bdev_new(fl_bdev *parent, uint64_t writes_ok, int err, fl_bdev **out);
uint64_t fl_fault_bdev_writes(const fl_bdev *d);   /* écritures réussies à ce jour */
uint64_t fl_fault_bdev_flushes(const fl_bdev *d);  /* appels flush */
uint64_t fl_fault_bdev_reads(const fl_bdev *d);    /* appels de lecture (réussis ou non) */
/* Compte à part les écritures réussies qui commencent à `lba` ou au-delà (remis à zéro) : zone de données. */
void fl_fault_bdev_count_from(fl_bdev *d, uint64_t lba);
uint64_t fl_fault_bdev_counted_writes(const fl_bdev *d);
/* Trace des écritures réussies et des vidages dans buf (chaîne terminée par NUL, tronquée à cap - 1) :
 * 'C' écriture contenant un bloc de commit jbd2 (en-tête au début d'un bloc de block_size octets),
 * 'W' autre écriture, 'F' vidage. cap = 0 : arrêt. buf doit rester valide tant que la trace est active. */
/* err < 0 : toute lecture rend err (sans rien lire) ; 0 : lectures normales. */
void fl_fault_bdev_fail_reads(fl_bdev *d, int err);
void fl_fault_bdev_trace(fl_bdev *d, uint32_t block_size, char *buf, size_t cap);

#endif
