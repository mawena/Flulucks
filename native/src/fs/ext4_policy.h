#ifndef FL_EXT4_POLICY_H
#define FL_EXT4_POLICY_H

/* Règles de lecture seule du § 4.2 de la spec pour ext4 (phase 3, T6). */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "fluluks/blockdev.h"

/* Décide, d'après le superbloc validé (1024 octets), le superbloc du journal et les descripteurs
 * de groupe, si l'écriture est permise. Renvoie FL_OK et remplit *rw et reason (≤ 127 octets, "" si
 * rw) ; une erreur de lecture du périphérique est rendue telle quelle. Règles, dans cet ordre (la
 * première qui s'applique donne la raison) :
 *   1. périphérique en lecture seule → FL_RO_WP ;
 *   2. option incompat, ro_compat ou du journal inconnue en écriture (pour le journal : toute option
 *      compat, dont les sommes v1, ou ro_compat), pas d'extents, journal externe → FL_RO_FEATURES ;
 *   3. pas de journal → FL_RO_NOJOURNAL ;
 *   4. journal à rejouer → FL_RO_RECOVERY ;
 *   5. état non propre, erreurs signalées → FL_RO_ERRORS ;
 *   6. somme fausse : superbloc, un descripteur de groupe, superbloc du journal → FL_RO_CSUM ;
 *   7. taille de bloc hors {1, 2, 4} Kio → FL_RO_BLOCKSIZE. */
int fl_ext4_write_policy(fl_bdev *dev, const uint8_t sb[1024], bool *rw, char reason[128]);

/* Diagnostic, lecture seule : applique les mêmes règles sans tenir compte de la protection en écriture
 * du périphérique (règle 1), et écrit dans out le détail de chaque contrôle (options, sommes calculées
 * et lues, premiers descripteurs fautifs) puis le verdict. validated_sb : le superbloc déjà validé au
 * montage, ou NULL pour le relire (il est alors borné comme par sb_validate). FL_ECORRUPT si le
 * superbloc n'est pas un superbloc ext4 plausible. */
int fl_ext4_policy_explain(fl_bdev *dev, const uint8_t *validated_sb, FILE *out);

/* Android : écrit ce même détail dans logcat (étiquette FluLuksPolicy), une ligne par contrôle ; ne
 * contient que des sommes, des options et des numéros de groupe. Sans effet ailleurs. */
void fl_ext4_policy_log(fl_bdev *dev, const uint8_t sb[1024]);

#endif
