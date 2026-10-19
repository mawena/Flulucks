/* Accès disque de FatFs sur fl_bdev (voir fat_diskio.h). Appelé par FatFs, toujours sous le verrou de
 * fat_glue.c. */

#include "fs/fat_diskio.h"

#include <stdlib.h>

#include "ff.h"     /* avant diskio.h, qui en utilise les types */
#include "diskio.h"
#include "fluluks/error.h"

struct fat_disk {
  uint8_t pdrv;
  fl_bdev *dev;
  uint32_t ss;     /* taille de secteur du système de fichiers */
  uint32_t ratio;  /* ss / dev->sector_size, 0 si ss est plus petit (lecture seule) */
  bool writable;
  int err;         /* première erreur d'écriture ou de vidage (collante) */
};

static fat_disk *g_disks[FF_VOLUMES]; /* sous le verrou de fat_glue.c */

int fat_disk_attach(uint8_t pdrv, fl_bdev *dev, uint32_t ss, bool writable, fat_disk **out) {
  *out = NULL;
  if (pdrv >= FF_VOLUMES || g_disks[pdrv] || !dev || ss < 512 || ss > 4096 || (ss & (ss - 1))) return FL_EINVAL;
  fat_disk *d = calloc(1, sizeof *d);
  if (!d) return FL_ENOMEM;
  d->pdrv = pdrv;
  d->dev = dev;
  d->ss = ss;
  d->ratio = ss % dev->sector_size == 0 ? ss / dev->sector_size : 0;
  d->writable = writable && d->ratio != 0 && !dev->read_only;
  g_disks[pdrv] = d;
  *out = d;
  return FL_OK;
}

void fat_disk_detach(fat_disk *d) {
  if (!d) return;
  g_disks[d->pdrv] = NULL;
  free(d);
}

void fat_disk_set_writable(fat_disk *d, bool writable) {
  d->writable = writable && d->ratio != 0 && !d->dev->read_only;
}

int fat_disk_error(const fat_disk *d) { return d->err; }

static fat_disk *disk(BYTE pdrv) { return pdrv < FF_VOLUMES ? g_disks[pdrv] : NULL; }

/* Nombre de secteurs FatFs du disque. */
static uint64_t nsect(const fat_disk *d) { return fl_bdev_size_bytes(d->dev) / d->ss; }

static bool in_range(const fat_disk *d, LBA_t sector, UINT count) {
  uint64_t n = nsect(d);
  return count > 0 && sector < n && count <= n - sector;
}

DSTATUS disk_initialize(BYTE pdrv) { return disk_status(pdrv); }

DSTATUS disk_status(BYTE pdrv) {
  fat_disk *d = disk(pdrv);
  if (!d) return STA_NOINIT;
  return d->writable ? 0 : STA_PROTECT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
  fat_disk *d = disk(pdrv);
  if (!d) return RES_NOTRDY;
  if (!in_range(d, sector, count)) return RES_PARERR;
  int rc = d->ratio ? fl_bdev_read(d->dev, (uint64_t)sector * d->ratio, count * d->ratio, buff)
                    : fl_bdev_read_bytes(d->dev, (uint64_t)sector * d->ss, (size_t)count * d->ss, buff);
  return rc == FL_OK ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
  fat_disk *d = disk(pdrv);
  if (!d) return RES_NOTRDY;
  if (!d->writable) return RES_WRPRT;
  if (d->err != FL_OK) return RES_ERROR;
  if (!in_range(d, sector, count)) return RES_PARERR;
  int rc = fl_bdev_write(d->dev, (uint64_t)sector * d->ratio, count * d->ratio, buff);
  if (rc != FL_OK) {
    d->err = rc;
    return RES_ERROR;
  }
  return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
  fat_disk *d = disk(pdrv);
  if (!d) return RES_NOTRDY;
  switch (cmd) {
    case CTRL_SYNC: {
      if (!d->writable) return RES_OK;
      if (d->err != FL_OK) return RES_ERROR;
      int rc = fl_bdev_flush(d->dev);
      if (rc != FL_OK) {
        d->err = rc;
        return RES_ERROR;
      }
      return RES_OK;
    }
    case GET_SECTOR_SIZE:
      *(WORD *)buff = (WORD)d->ss;
      return RES_OK;
    case GET_SECTOR_COUNT:
      *(LBA_t *)buff = (LBA_t)nsect(d);
      return RES_OK;
    case GET_BLOCK_SIZE:
      *(DWORD *)buff = 1;
      return RES_OK;
    default:
      return RES_PARERR;
  }
}
