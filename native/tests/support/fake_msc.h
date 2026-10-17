#ifndef FL_FAKE_MSC_H
#define FL_FAKE_MSC_H

/* Faux périphérique de stockage de masse USB (tests seulement). Il implémente
 * fl_usb_transport avec une vraie machine d'états Bulk-Only : il analyse les CBW de
 * 31 octets et répond par de vrais CSW de 13 octets. Les données viennent d'une image
 * ouverte en lecture seule. Pannes scriptables, compteurs de commandes. */

#include <stdbool.h>
#include <stdint.h>

#include "fluluks/usb.h"

typedef struct fake_msc fake_msc;

typedef struct {
  unsigned op[256];        /* commandes SCSI reçues, par code d'opération */
  unsigned commands;       /* CBW valides reçus */
  unsigned writes;         /* WRITE(10) + WRITE(16) */
  unsigned resets;         /* bot_reset */
  unsigned clear_halts;    /* clear_halt */
  unsigned calls;          /* appels de transport, quels qu'ils soient */
  uint32_t max_read_xfer;  /* plus grande longueur de données d'un READ */
  unsigned violations;     /* appels concurrents détectés (verrou absent côté hôte) */
} fake_msc_stats;

/* Image ouverte en lecture seule, blocs de 512 octets. */
int fake_msc_new(const char *image_path, fake_msc **out);
void fake_msc_free(fake_msc *m);
const fl_usb_transport *fake_msc_transport(fake_msc *m);
fake_msc_stats *fake_msc_stats_get(fake_msc *m);
void fake_msc_reset_stats(fake_msc *m);

/* Après n CBW acceptés, tout appel de transport renvoie err (durablement). */
void fake_msc_fail_after(fake_msc *m, unsigned n, int err);
/* La prochaine phase de données IN est bloquée (STALL) ; le CSW annonce un résidu complet. */
void fake_msc_stall_next_data(fake_msc *m);
/* Le prochain CSW a une signature fausse (cumulable : deux appels = deux CSW). */
void fake_msc_bad_csw_next(fake_msc *m);
/* Les k prochaines commandes d'accès au support (TUR, READ CAPACITY, READ) échouent
 * avec NOT READY / « becoming ready » (02/04/01). */
void fake_msc_not_ready(fake_msc *m, unsigned k);
/* La prochaine commande d'accès au support échoue avec ce sens. */
void fake_msc_sense_next(fake_msc *m, uint8_t key, uint8_t asc, uint8_t ascq);
/* Taille de bloc logique (toute valeur non nulle : les valeurs hostiles sont permises). */
int fake_msc_set_block_size(fake_msc *m, uint32_t bs);
/* Nombre de blocs annoncé (0 → dernier LBA 0xFFFFFFFFFFFFFFFF en READ CAPACITY(16)). */
void fake_msc_set_capacity(fake_msc *m, uint64_t blocks);
/* READ CAPACITY(10) renvoie 0xFFFFFFFF pour forcer READ CAPACITY(16). */
void fake_msc_force_rc16(fake_msc *m);
/* Longueur de la réponse INQUIRY (36 par défaut). */
void fake_msc_set_inquiry_len(fake_msc *m, uint32_t n);
/* Octet 0 de la réponse INQUIRY (qualificatif + type de périphérique ; 0x00 par défaut). */
void fake_msc_set_peripheral(fake_msc *m, uint8_t b0);

#endif
