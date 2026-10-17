#ifndef FLULUKS_USB_H
#define FLULUKS_USB_H

/* Disque USB de stockage de masse (Bulk-Only Transport + SCSI) vu comme un fl_bdev.
 *
 * Le transport est abstrait : sur Linux et Android, fl_usbdevfs_transport_new l'implémente
 * sur un descripteur usbdevfs ; les tests utilisent un faux périphérique.
 *
 * Phase 2 : lecture seule. fl_bdev_write renvoie toujours FL_EROFS et aucune commande
 * WRITE n'est jamais envoyée. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fluluks/blockdev.h"
#include "fluluks/error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void *ctx;
  /* Octets transférés (>= 0) ou fl_err (< 0) : FL_ENODEV débranché, FL_ETIMEDOUT délai
   * dépassé, FL_ESTALL point d'accès bloqué, FL_EIO autre. */
  int (*bulk_out)(void *ctx, const void *buf, size_t len, unsigned timeout_ms);
  int (*bulk_in)(void *ctx, void *buf, size_t len, unsigned timeout_ms);
  /* Débloque le point d'accès bulk IN (in_endpoint vrai) ou OUT. FL_OK ou fl_err. */
  int (*clear_halt)(void *ctx, bool in_endpoint);
  /* Requête de classe 0xFF (Bulk-Only Mass Storage Reset) sur l'interface, puis les
   * deux clear_halt. FL_OK ou fl_err. */
  int (*bot_reset)(void *ctx);
} fl_usb_transport;

/* Ouvre l'unité logique `lun` (0..15) : INQUIRY, TEST UNIT READY (20 s au plus le temps
 * que le disque soit prêt), READ CAPACITY(10) puis (16) si besoin.
 * La structure *t est copiée ; t->ctx doit rester valide jusqu'à la fermeture du bdev,
 * qui ne libère pas le transport. Taille de bloc hors {512, 1024, 2048, 4096} → FL_ENOTSUP.
 * Après un FL_ENODEV, toute lecture renvoie FL_ENODEV sans rien envoyer.
 * Sûr entre fils : les commandes sont sérialisées par un verrou propre au périphérique. */
int fl_usb_bdev_open(const fl_usb_transport *t, uint8_t lun, bool read_only, fl_bdev **out);

/* Transport usbdevfs (Linux, Android). `fd` vient de UsbDeviceConnection.getFileDescriptor() ;
 * l'interface doit déjà être réclamée. Le transport ne possède pas `fd` et ne le ferme pas.
 * ep_in doit avoir le bit 0x80, ep_out non. */
int fl_usbdevfs_transport_new(int fd, uint8_t iface, uint8_t ep_in, uint8_t ep_out, fl_usb_transport **out);
/* NULL accepté. */
void fl_usbdevfs_transport_free(fl_usb_transport *t);

#ifdef __cplusplus
}
#endif

#endif
