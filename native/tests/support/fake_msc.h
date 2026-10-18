#ifndef FL_FAKE_MSC_H
#define FL_FAKE_MSC_H

/* Faux périphérique de stockage de masse USB (tests seulement). Il implémente
 * fl_usb_transport avec une vraie machine d'états Bulk-Only : il analyse les CBW de
 * 31 octets et répond par de vrais CSW de 13 octets. Les données viennent d'une image :
 * ouverte en lecture seule (fake_msc_new : disque protégé en écriture, MODE SENSE le dit et
 * WRITE est refusé par le sens DATA PROTECT 07/27/00), ou en écriture (fake_msc_new_rw : WRITE
 * écrit dans l'image). Pannes scriptables, compteurs de commandes. */

#include <stdbool.h>
#include <stdint.h>

#include "fluluks/usb.h"

typedef struct fake_msc fake_msc;

typedef struct {
  unsigned op[256];        /* commandes SCSI reçues, par code d'opération */
  unsigned commands;       /* CBW valides reçus */
  unsigned writes;         /* WRITE(10) + WRITE(16) reçus (refusés compris) */
  unsigned writes_ok;      /* WRITE dont les données ont été acceptées (écrites, ou hors image) */
  uint64_t written_bytes;  /* octets écrits dans l'image */
  uint32_t max_write_xfer; /* plus grande longueur de données d'un WRITE */
  unsigned resets;         /* bot_reset */
  unsigned clear_halts;    /* clear_halt */
  unsigned calls;          /* appels de transport, quels qu'ils soient */
  uint32_t max_read_xfer;  /* plus grande longueur de données d'un READ */
  unsigned violations;     /* appels concurrents détectés (verrou absent côté hôte) */
  unsigned zlps;           /* paquets de longueur nulle envoyés à la place du CSW */
  uint8_t start_stop[6];   /* dernier CDB START STOP UNIT reçu */
  uint8_t last_cdb[16];    /* dernier CDB reçu (complété de zéros) */
  uint8_t last_cdb_len;
  /* Délai (ms) du dernier transfert de chaque phase, par code d'opération de la commande. */
  unsigned tmo_cbw[256], tmo_data[256], tmo_csw[256];
} fake_msc_stats;

/* Image ouverte en lecture seule, blocs de 512 octets : disque protégé en écriture. */
int fake_msc_new(const char *image_path, fake_msc **out);
/* Image ouverte en écriture : WRITE(10)/(16) y écrivent (au-delà de l'image et dans la capacité
 * annoncée : accepté, rien n'est gardé). */
int fake_msc_new_rw(const char *image_path, fake_msc **out);
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
/* Idem avec un ASC/ASCQ choisi (sens 02/asc/ascq), sans effet de START STOP UNIT. */
void fake_msc_not_ready_sense(fake_msc *m, unsigned k, uint8_t asc, uint8_t ascq);
/* Disque arrêté : les commandes d'accès au support échouent avec 02/04/02 (« initializing
 * command required ») jusqu'à un START STOP UNIT avec START=1 et LOEJ=0. */
void fake_msc_require_start(fake_msc *m);
/* Les k prochains CSW sont précédés d'un paquet de longueur nulle (pont bavard). */
void fake_msc_zlp_before_csw(fake_msc *m, unsigned k);
/* Pont au résidu fantaisiste : tout CSW d'une commande avec phase de données annonce ce résidu,
 * alors que toutes les données sont envoyées. */
void fake_msc_bogus_residue(fake_msc *m, uint32_t residue);
/* La prochaine commande d'accès au support échoue avec ce sens. */
void fake_msc_sense_next(fake_msc *m, uint8_t key, uint8_t asc, uint8_t ascq);
/* Les k prochaines commandes de code d'opération `op` (quel qu'il soit : WRITE, SYNCHRONIZE CACHE,
 * MODE SENSE…) échouent avec ce sens, sans rien exécuter (un WRITE n'écrit rien). */
void fake_msc_sense_op(fake_msc *m, uint8_t op, uint8_t key, uint8_t asc, uint8_t ascq, unsigned k);
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
/* Protection en écriture (image ouverte en écriture) : signalée par MODE SENSE (wp_mode_sense),
 * et WRITE refusé par le sens 07/27/00 (toujours, si on). */
void fake_msc_set_write_protect(fake_msc *m, bool on, bool wp_mode_sense);
/* MODE SENSE(6) refusé (ILLEGAL REQUEST), comme sur certains ponts. */
void fake_msc_mode_sense_unsupported(fake_msc *m);
/* SYNCHRONIZE CACHE refusé (ILLEGAL REQUEST 05/20/00). */
void fake_msc_sync_unsupported(fake_msc *m);
/* Au n-ième WRITE (1 = le premier), tout appel de transport renvoie err (durablement). */
void fake_msc_fail_at_write(fake_msc *m, unsigned n, int err);

#endif
