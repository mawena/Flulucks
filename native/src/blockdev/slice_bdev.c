#include <stdlib.h>

#include "fluluks/blockdev.h"
#include "fluluks/error.h"

typedef struct {
  fl_bdev *parent;
  uint64_t first;
} slice_priv;

static int slice_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  slice_priv *p = d->priv;
  return fl_bdev_read(p->parent, p->first + lba, count, buf);
}

static int slice_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf) {
  slice_priv *p = d->priv;
  return fl_bdev_write(p->parent, p->first + lba, count, buf);
}

static int slice_flush(fl_bdev *d) {
  slice_priv *p = d->priv;
  return fl_bdev_flush(p->parent);
}

/* Ne ferme jamais le parent. */
static void slice_destroy(fl_bdev *d) {
  free(d->priv);
  free(d);
}

static const fl_bdev_ops slice_ops = {slice_read, slice_write, slice_flush, slice_destroy};

int fl_slice_bdev_new(fl_bdev *parent, uint64_t first_lba, uint64_t count, fl_bdev **out) {
  if (!parent || !out) return FL_EINVAL;
  *out = NULL;
  if (first_lba > parent->sector_count || count > parent->sector_count - first_lba) return FL_EINVAL;
  fl_bdev *d = calloc(1, sizeof *d);
  slice_priv *p = calloc(1, sizeof *p);
  if (!d || !p) {
    free(d);
    free(p);
    return FL_ENOMEM;
  }
  p->parent = parent;
  p->first = first_lba;
  d->ops = &slice_ops;
  d->sector_size = parent->sector_size;
  d->sector_count = count;
  d->read_only = parent->read_only;
  d->priv = p;
  *out = d;
  return FL_OK;
}
