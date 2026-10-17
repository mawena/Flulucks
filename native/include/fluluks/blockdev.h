#ifndef FLULUKS_BLOCKDEV_H
#define FLULUKS_BLOCKDEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct fl_bdev fl_bdev;

typedef struct {
  int (*read)(fl_bdev *d, uint64_t lba, uint32_t count, void *buf);
  int (*write)(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf);
  int (*flush)(fl_bdev *d);
  void (*destroy)(fl_bdev *d);
} fl_bdev_ops;

struct fl_bdev {
  const fl_bdev_ops *ops;
  uint32_t sector_size;
  uint64_t sector_count;
  bool read_only;
  void *priv;
};

/* Lecture de `count` secteurs depuis `lba`. FL_EINVAL hors bornes (sans dépassement d'entier). */
int fl_bdev_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf);
/* FL_EROFS si le périphérique est en lecture seule, FL_EINVAL hors bornes. */
int fl_bdev_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf);
int fl_bdev_flush(fl_bdev *d);
/* Appelle ops->destroy ; NULL accepté. */
void fl_bdev_close(fl_bdev *d);
/* Lecture non alignée en octets. FL_EINVAL hors bornes. */
int fl_bdev_read_bytes(fl_bdev *d, uint64_t offset, size_t len, void *buf);
uint64_t fl_bdev_size_bytes(const fl_bdev *d);

/* Fichier image ou périphérique bloc. Pour un périphérique bloc, sector_size est ignoré. */
int fl_file_bdev_open(const char *path, uint32_t sector_size, bool read_only, fl_bdev **out);
/* Tranche [first_lba, first_lba+count) du parent ; ne possède pas le parent. */
int fl_slice_bdev_new(fl_bdev *parent, uint64_t first_lba, uint64_t count, fl_bdev **out);

#endif
