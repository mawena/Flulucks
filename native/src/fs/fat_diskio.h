#ifndef FL_FAT_DISKIO_H
#define FL_FAT_DISKIO_H

/* Accès disque de FatFs (disk_read, disk_write, disk_ioctl…) branché sur fl_bdev.
 *
 * FatFs désigne un disque par son numéro pdrv (0..FF_VOLUMES-1, le numéro de volume « N: »). Chaque volume
 * monté rattache son fl_bdev à un emplacement ; FatFs n'est appelé que sous le verrou de fat_glue.c, qui
 * protège aussi la table des emplacements.
 *
 * Taille de secteur : celle du système de fichiers (ss, lue dans le secteur de démarrage). Si ss est un
 * multiple de dev->sector_size, lecture et écriture directes ; si ss est plus petit, lecture seulement
 * (secteurs englobants), et toute écriture est refusée.
 *
 * Débit : FatFs envoie au plus un cluster par disk_write. Les écritures contiguës sont regroupées dans un
 * tampon (fl_wbuf, FL_WBUF_CAP octets au plus), vidé avant toute lecture, toute écriture non contiguë,
 * CTRL_SYNC, le détachement et fat_disk_drain : l'ordre des écritures sur le disque reste celui de FatFs.
 * Une erreur de vidage est collante (fat_disk_error) et rendue par l'appel en cours. */

#include <stdbool.h>
#include <stdint.h>

#include "fluluks/blockdev.h"

typedef struct fat_disk fat_disk;

/* FL_EINVAL si pdrv est hors bornes ou déjà pris, ou si ss n'est pas une puissance de 2 de 512 à 4096 ;
 * writable est ignoré (faux) si ss < dev->sector_size. */
int fat_disk_attach(uint8_t pdrv, fl_bdev *dev, uint32_t ss, bool writable, fat_disk **out);
/* NULL accepté ; libère l'emplacement. */
void fat_disk_detach(fat_disk *d);
/* Faux : disk_status rend STA_PROTECT et disk_write refuse (RES_WRPRT). */
void fat_disk_set_writable(fat_disk *d, bool writable);
/* Vide le tampon d'écriture (sans vider le cache du disque) avant une écriture directe par fl_bdev :
 * FL_OK ou l'erreur (collante). */
int fat_disk_drain(fat_disk *d);
/* Première erreur fl_err d'écriture ou de vidage, collante (FL_OK sinon). */
int fat_disk_error(const fat_disk *d);

#endif
