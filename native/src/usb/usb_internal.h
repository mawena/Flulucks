#ifndef FLULUKS_USB_INTERNAL_H
#define FLULUKS_USB_INTERNAL_H

/* Interne : couches BOT (bot.c) et SCSI (scsi.c) du disque USB. Appelées sous le verrou
 * du périphérique (usb_bdev.c). */

#include <stdbool.h>
#include <stdint.h>

#include "fluluks/usb.h"

/* Horloge injectable (les tests accélèrent l'attente du disque). */
typedef struct {
  uint64_t (*now_ms)(void *ctx);
  void (*sleep_ms)(void *ctx, unsigned ms);
  void *ctx;
} fl_usb_clock;

/* Horloge réelle (CLOCK_MONOTONIC, nanosleep). */
extern const fl_usb_clock fl_usb_real_clock;

#define FL_USB_XFER_TIMEOUT_MS 5000u   /* délai par transfert bulk (CBW, commandes courtes) */
#define FL_USB_MEDIA_TIMEOUT_MS 30000u /* données et CSW de READ, WRITE, SYNCHRONIZE CACHE et START STOP UNIT */
#define FL_USB_READY_BUDGET_MS 20000u  /* attente maximale d'un disque « pas prêt » */
#define FL_USB_READY_POLL_MS 200u      /* intervalle entre deux essais */
#define FL_USB_MAX_XFER 65536u         /* au plus 64 Kio de données par commande */
#define FL_USB_QUICK_RETRIES 3         /* réessais immédiats (UNIT ATTENTION, transfert court…) */

typedef struct {
  fl_usb_transport t;
  fl_usb_clock clk;
  uint8_t lun;
  uint32_t tag;
  bool dead;           /* FL_ENODEV vu : plus rien n'est envoyé */
  bool ignore_residue; /* résidu du CSW fantaisiste (heuristique de Linux) : on compte les octets reçus */
  bool wp;             /* protection en écriture signalée (MODE SENSE ou DATA PROTECT) : collante */
  bool opened;         /* ouverture réussie : la politique UNIT ATTENTION change (voir scsi.c) */
  bool ua_reset;       /* UNIT ATTENTION autre que « support changé » vue après l'ouverture : collant,
                        * tout flush rend FL_EIO (le cache volatil du disque a pu être perdu) */
  bool sync_unsupported; /* SYNCHRONIZE CACHE refusé (ILLEGAL REQUEST) */
} fl_usb_dev;

enum { FL_BOT_NONE = 0, FL_BOT_IN = 1, FL_BOT_OUT = 2 };

/* Une commande BOT complète (CBW → données → CSW) avec reprise (bot_reset) et un réessai.
 * FL_OK : *status = 0 (succès) ou 1 (échec de commande : REQUEST SENSE à faire),
 * *done = octets de données réellement transférés (<= len, résidu pris en compte sauf si
 * u->ignore_residue). Délais : CBW FL_USB_XFER_TIMEOUT_MS ; données et CSW
 * FL_USB_MEDIA_TIMEOUT_MS pour READ(10)/(16), WRITE(10)/(16), SYNCHRONIZE CACHE(10) et START STOP
 * UNIT, FL_USB_XFER_TIMEOUT_MS sinon.
 * Sinon fl_err (FL_ENODEV marque le périphérique mort). */
int fl_bot_exec(fl_usb_dev *u, const uint8_t *cdb, uint8_t cdblen, int dir, void *buf, uint32_t len,
                uint32_t *done, uint8_t *status);

/* Construit READ(10) ou READ(16) selon que lba / lba+n-1 tient sur 32 bits ; renvoie la longueur. */
uint8_t fl_scsi_build_read(uint64_t lba, uint32_t n, uint8_t cdb[16]);

/* INQUIRY ; FL_ENOTSUP si ce n'est pas un périphérique à accès direct. */
int fl_scsi_inquiry(fl_usb_dev *u);
/* TEST UNIT READY, réessayé tant que le disque n'est pas prêt (budget FL_USB_READY_BUDGET_MS).
 * NOT READY 04/02 (« initializing command required ») : un seul START STOP UNIT (START=1), puis
 * l'attente reprend. NOT READY 04/03 (intervention manuelle) ou 3A (support absent) : échec immédiat. */
int fl_scsi_wait_ready(fl_usb_dev *u);
/* READ CAPACITY(10), puis (16) si 0xFFFFFFFF. Taille de bloc vérifiée. */
int fl_scsi_read_capacity(fl_usb_dev *u, uint64_t *blocks, uint32_t *block_size);
/* Lecture de n blocs (n * block_size <= FL_USB_MAX_XFER). */
int fl_scsi_read(fl_usb_dev *u, uint64_t lba, uint32_t n, uint32_t block_size, void *buf);
/* WRITE(10), ou WRITE(16) au-delà de 32 bits ; renvoie la longueur. */
uint8_t fl_scsi_build_write(uint64_t lba, uint32_t n, uint8_t cdb[16]);
/* Écriture de n blocs (n * block_size <= FL_USB_MAX_XFER). Sens DATA PROTECT → FL_EROFS. Après
 * l'ouverture, UNIT ATTENTION : FL_ENODEV (support changé, périphérique mort) ou FL_EIO, sans réessai. */
int fl_scsi_write(fl_usb_dev *u, uint64_t lba, uint32_t n, uint32_t block_size, const void *buf);
/* MODE SENSE(6) : *wp = bit de protection en écriture de l'en-tête. */
int fl_scsi_mode_sense_wp(fl_usb_dev *u, bool *wp);
/* SYNCHRONIZE CACHE(10) ; ILLEGAL REQUEST (non prise en charge) vaut FL_OK et pose
 * u->sync_unsupported. */
int fl_scsi_sync_cache(fl_usb_dev *u);

/* fl_usb_bdev_open avec une horloge choisie (NULL = horloge réelle). */
int fl_usb_bdev_open_ex(const fl_usb_transport *t, uint8_t lun, bool read_only, const fl_usb_clock *clk,
                        fl_bdev **out);

#endif
