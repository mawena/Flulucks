#include <stdlib.h>
#include <string.h>

#include "fluluks/blockdev.h"
#include "fluluks/error.h"

/* Vrai si [lba, lba+count) tient dans le périphérique, sans dépassement d'entier. */
static bool in_bounds(const fl_bdev *d, uint64_t lba, uint64_t count) {
  return lba <= d->sector_count && count <= d->sector_count - lba;
}

int fl_bdev_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  if (!d || !d->ops || !d->ops->read || (!buf && count)) return FL_EINVAL;
  if (!in_bounds(d, lba, count)) return FL_EINVAL;
  if (count == 0) return FL_OK;
  return d->ops->read(d, lba, count, buf);
}

int fl_bdev_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf) {
  if (!d || !d->ops) return FL_EINVAL;
  if (d->read_only) return FL_EROFS;
  if (!d->ops->write || (!buf && count)) return FL_EINVAL;
  if (!in_bounds(d, lba, count)) return FL_EINVAL;
  if (count == 0) return FL_OK;
  return d->ops->write(d, lba, count, buf);
}

int fl_bdev_flush(fl_bdev *d) {
  if (!d || !d->ops) return FL_EINVAL;
  if (!d->ops->flush) return FL_OK;
  return d->ops->flush(d);
}

void fl_bdev_close(fl_bdev *d) {
  if (!d) return;
  if (d->ops && d->ops->destroy) d->ops->destroy(d);
}

uint64_t fl_bdev_size_bytes(const fl_bdev *d) {
  return d ? d->sector_count * (uint64_t)d->sector_size : 0;
}

int fl_bdev_read_bytes(fl_bdev *d, uint64_t offset, size_t len, void *buf) {
  if (!d || d->sector_size == 0 || (!buf && len)) return FL_EINVAL;
  uint64_t size = fl_bdev_size_bytes(d);
  if (offset > size || (uint64_t)len > size - offset) return FL_EINVAL;
  if (len == 0) return FL_OK;

  uint64_t ss = d->sector_size;
  uint64_t first = offset / ss;
  uint64_t head = offset % ss;
  uint64_t last = (offset + len - 1) / ss; /* offset+len <= size : pas de dépassement */
  uint64_t nsec = last - first + 1;

  /* Cas aligné : lecture directe, par tranches pour tenir dans uint32_t. */
  if (head == 0 && len % ss == 0) {
    unsigned char *out = buf;
    while (nsec) {
      uint32_t n = nsec > (1u << 20) ? (1u << 20) : (uint32_t)nsec;
      int rc = fl_bdev_read(d, first, n, out);
      if (rc != FL_OK) return rc;
      first += n;
      nsec -= n;
      out += (size_t)n * ss;
    }
    return FL_OK;
  }

  /* Cas non aligné : on lit les secteurs couvrants par blocs bornés (<= 64 Kio+2 secteurs). */
  size_t chunk_secs = 128;
  size_t cap = (size_t)((chunk_secs > nsec ? nsec : chunk_secs) * ss);
  unsigned char *tmp = malloc(cap);
  if (!tmp) return FL_ENOMEM;
  unsigned char *out = buf;
  size_t remaining = len;
  uint64_t lba = first;
  uint64_t skip = head;
  int rc = FL_OK;
  while (remaining) {
    uint64_t left = last - lba + 1;
    uint32_t n = (uint32_t)(left > chunk_secs ? chunk_secs : left);
    rc = fl_bdev_read(d, lba, n, tmp);
    if (rc != FL_OK) break;
    size_t avail = (size_t)((uint64_t)n * ss - skip);
    size_t take = avail < remaining ? avail : remaining;
    memcpy(out, tmp + skip, take);
    out += take;
    remaining -= take;
    lba += n;
    skip = 0;
  }
  free(tmp);
  return rc;
}
