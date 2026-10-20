#ifndef FL_NTFS_DEV_H
#define FL_NTFS_DEV_H

/* Périphérique de libntfs-3g (struct ntfs_device, opérations ntfs_device_operations) branché sur fl_bdev
 * (phase 5).
 *
 * libntfs-3g lit et écrit en octets, à des positions quelconques (elle compte sur le noyau pour les
 * secteurs partiels) : les lectures passent par fl_bdev_read_bytes, les écritures partielles relisent les
 * secteurs de bord (lecture-modification-écriture). Les erreurs fl_err deviennent errno (EIO, EROFS,
 * ENODEV, ETIMEDOUT, ENOMEM ; EINVAL pour une lecture hors du disque, EIO pour une écriture). La première
 * erreur d'écriture ou de vidage est gardée (ntfs_dev_error, collante) et rendue par l'appel en cours puis
 * par toute écriture suivante.
 *
 * Écriture différée (débit) : libntfs-3g écrit $Bitmap et l'enregistrement MFT entre deux morceaux de
 * données ; sur un disque dur, chaque aller-retour coûte une recherche de tête. Les écritures alignées d'au
 * moins 64 Kio passent par un tampon contigu (fl_wbuf, 4 Mio) ; les autres attendent dans une table de
 * secteurs (4 Mio au plus, triée, écrite dans l'ordre des secteurs quand elle est pleine). Les lectures
 * voient les deux. Tout est écrit par l'opération sync de libntfs-3g (puis cache du disque vidé), par
 * ntfs_dev_drain et au passage en mode direct. L'ordre des écritures entre deux synchronisations n'est pas
 * celui de libntfs-3g : sans journal, la sûreté vient du drapeau « sale » de la colle, posé et effacé en mode
 * direct, chaque fois suivi d'un vidage du cache du disque.
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
/* Faux : ce qui attend est abandonné, et toute écriture suivante est refusée (EROFS), sans toucher au disque
 * (lecture seule collante). */
void ntfs_dev_set_writable(ntfs_dev *d, bool writable);
/* Vrai : écrit tout ce qui attend (sans vider le cache du disque), puis chaque écriture va au disque aussitôt
 * (drapeau « sale »). FL_OK ou l'erreur de l'écriture. */
int ntfs_dev_set_direct(ntfs_dev *d, bool direct);
/* Écrit tout ce qui attend, sans vider le cache du disque : FL_OK ou l'erreur (collante). */
int ntfs_dev_drain(ntfs_dev *d);
/* Première erreur fl_err d'écriture ou de vidage (FL_OK sinon). */
int ntfs_dev_error(const ntfs_dev *d);

#endif
