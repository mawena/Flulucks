#ifndef FL_NTFS_DEV_H
#define FL_NTFS_DEV_H

/* Périphérique de libntfs-3g (struct ntfs_device, opérations ntfs_device_operations) branché sur fl_bdev
 * (phase 5).
 *
 * libntfs-3g lit et écrit en octets, à des positions quelconques (elle compte sur le noyau pour les
 * secteurs partiels) : les lectures passent par fl_bdev_read_bytes, les écritures partielles relisent les
 * secteurs de bord (lecture-modification-écriture). Les erreurs fl_err deviennent errno (EIO, EROFS,
 * ENODEV, ETIMEDOUT, ENOMEM, EINVAL hors du disque). La première erreur d'écriture ou de vidage est
 * gardée (ntfs_dev_error, collante).
 *
 * Propriété : la colle possède l'état (ntfs_dev) ; le struct ntfs_device est libéré par ntfs_umount, ou
 * par ntfs_device_free après un échec de ntfs_device_mount. Appelé seulement sous le verrou de la colle. */

#include <stdbool.h>

#include "fluluks/blockdev.h"

struct ntfs_device;
typedef struct ntfs_dev ntfs_dev;

/* FL_OK ou FL_ENOMEM. writable faux : open en écriture rend EROFS, toute écriture aussi. */
int ntfs_dev_new(fl_bdev *bd, bool writable, ntfs_dev **out);
/* Le struct ntfs_device à passer à ntfs_device_mount. */
struct ntfs_device *ntfs_dev_device(ntfs_dev *d);
/* NULL accepté. device_too : libère aussi le struct ntfs_device (montage jamais réussi : sinon
 * ntfs_umount l'a déjà fait). */
void ntfs_dev_free(ntfs_dev *d, bool device_too);
/* Faux : toute écriture suivante est refusée (EROFS), sans toucher au disque (lecture seule collante). */
void ntfs_dev_set_writable(ntfs_dev *d, bool writable);
/* Première erreur fl_err d'écriture ou de vidage (FL_OK sinon). */
int ntfs_dev_error(const ntfs_dev *d);

#endif
