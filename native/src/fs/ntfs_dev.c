/* Périphérique de libntfs-3g sur fl_bdev (voir ntfs_dev.h). */

#define _GNU_SOURCE /* struct stat complet (st_mtime) pour les en-têtes de libntfs-3g */

#include "fs/ntfs_dev.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "fluluks/error.h"
#include "ntfs-3g/device.h"

struct ntfs_dev {
  fl_bdev *bd;
  struct ntfs_device *dev;
  bool writable;
  int err;  /* première erreur d'écriture ou de vidage */
  s64 pos;  /* curseur de seek/read/write */
};

static int to_errno(int rc) {
  switch (rc) {
    case FL_EROFS: return EROFS;
    case FL_ENODEV: return ENODEV;
    case FL_ETIMEDOUT: return ETIMEDOUT;
    case FL_ENOMEM: return ENOMEM;
    case FL_EINVAL: return EINVAL;
    default: return EIO;
  }
}

static ntfs_dev *st(struct ntfs_device *dev) { return dev->d_private; }

static int d_open(struct ntfs_device *dev, int flags) {
  ntfs_dev *d = st(dev);
  if ((flags & O_RDWR) == O_RDWR) {
    if (!d->writable) {
      errno = EROFS;
      return -1;
    }
  } else {
    NDevSetReadOnly(dev);
  }
  d->pos = 0;
  NDevSetOpen(dev);
  return 0;
}

static int d_sync(struct ntfs_device *dev) {
  ntfs_dev *d = st(dev);
  if (NDevReadOnly(dev) || !NDevDirty(dev)) return 0;
  int rc = fl_bdev_flush(d->bd);
  if (rc != FL_OK) {
    if (d->err == FL_OK) d->err = rc;
    errno = to_errno(rc);
    return -1;
  }
  NDevClearDirty(dev);
  return 0;
}

static int d_close(struct ntfs_device *dev) {
  int rc = d_sync(dev);
  NDevClearOpen(dev);
  return rc;
}

static s64 d_pread(struct ntfs_device *dev, void *buf, s64 count, s64 off) {
  ntfs_dev *d = st(dev);
  if (count < 0 || off < 0) {
    errno = EINVAL;
    return -1;
  }
  uint64_t size = fl_bdev_size_bytes(d->bd);
  if ((uint64_t)off >= size) return 0;
  if ((uint64_t)count > size - (uint64_t)off) count = (s64)(size - (uint64_t)off);
  int rc = fl_bdev_read_bytes(d->bd, (uint64_t)off, (size_t)count, buf);
  if (rc != FL_OK) {
    errno = to_errno(rc);
    return -1;
  }
  return count;
}

/* Écrit [p, p + n) à off ; secteurs de bord relus puis réécrits. FL_OK ou fl_err. */
static int write_bytes(ntfs_dev *d, const uint8_t *p, uint64_t n, uint64_t off) {
  uint32_t ss = d->bd->sector_size;
  uint64_t lba = off / ss;
  uint32_t head = (uint32_t)(off % ss);
  uint8_t *b = NULL;
  int rc = FL_OK;
  if (head || n < ss) {
    if (!(b = malloc(ss))) return FL_ENOMEM;
    rc = fl_bdev_read(d->bd, lba, 1, b);
    uint64_t k = n < ss - head ? n : ss - head;
    if (rc == FL_OK) {
      memcpy(b + head, p, (size_t)k);
      rc = fl_bdev_write(d->bd, lba, 1, b);
    }
    p += k;
    n -= k;
    lba++;
  }
  while (rc == FL_OK && n >= ss) {
    uint64_t sec = n / ss;
    uint32_t c = sec > (1u << 16) ? (1u << 16) : (uint32_t)sec;
    rc = fl_bdev_write(d->bd, lba, c, p);
    p += (uint64_t)c * ss;
    n -= (uint64_t)c * ss;
    lba += c;
  }
  if (rc == FL_OK && n) {
    if (!b && !(b = malloc(ss))) return FL_ENOMEM;
    rc = fl_bdev_read(d->bd, lba, 1, b);
    if (rc == FL_OK) {
      memcpy(b, p, (size_t)n);
      rc = fl_bdev_write(d->bd, lba, 1, b);
    }
  }
  free(b);
  return rc;
}

static s64 d_pwrite(struct ntfs_device *dev, const void *buf, s64 count, s64 off) {
  ntfs_dev *d = st(dev);
  if (count < 0 || off < 0) {
    errno = EINVAL;
    return -1;
  }
  if (!d->writable || NDevReadOnly(dev)) {
    errno = EROFS;
    return -1;
  }
  if (d->err != FL_OK) {
    errno = to_errno(d->err);
    return -1;
  }
  uint64_t size = fl_bdev_size_bytes(d->bd);
  if ((uint64_t)off > size || (uint64_t)count > size - (uint64_t)off) {
    errno = EINVAL;
    return -1;
  }
  if (count == 0) return 0;
  NDevSetDirty(dev);
  int rc = write_bytes(d, buf, (uint64_t)count, (uint64_t)off);
  if (rc != FL_OK) {
    if (rc != FL_ENOMEM && d->err == FL_OK) d->err = rc;
    errno = to_errno(rc);
    return -1;
  }
  return count;
}

static s64 d_seek(struct ntfs_device *dev, s64 off, int whence) {
  ntfs_dev *d = st(dev);
  s64 base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? d->pos : (s64)fl_bdev_size_bytes(d->bd);
  if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
    errno = EINVAL;
    return -1;
  }
  if (base + off < 0) {
    errno = EINVAL;
    return -1;
  }
  d->pos = base + off;
  return d->pos;
}

static s64 d_read(struct ntfs_device *dev, void *buf, s64 count) {
  s64 r = d_pread(dev, buf, count, st(dev)->pos);
  if (r > 0) st(dev)->pos += r;
  return r;
}

static s64 d_write(struct ntfs_device *dev, const void *buf, s64 count) {
  s64 r = d_pwrite(dev, buf, count, st(dev)->pos);
  if (r > 0) st(dev)->pos += r;
  return r;
}

static int d_stat(struct ntfs_device *dev, struct stat *sb) {
  memset(sb, 0, sizeof *sb);
  sb->st_mode = S_IFBLK | 0600;
  sb->st_size = (off_t)fl_bdev_size_bytes(st(dev)->bd);
  sb->st_blksize = st(dev)->bd->sector_size;
  return 0;
}

static int d_ioctl(struct ntfs_device *dev, unsigned long req, void *arg) {
  ntfs_dev *d = st(dev);
  if (req == BLKGETSIZE64) {
    *(uint64_t *)arg = fl_bdev_size_bytes(d->bd);
    return 0;
  }
  if (req == BLKSSZGET) {
    *(int *)arg = (int)d->bd->sector_size;
    return 0;
  }
  errno = ENOTTY;
  return -1;
}

static struct ntfs_device_operations k_ops = {
    .open = d_open, .close = d_close, .seek = d_seek, .read = d_read, .write = d_write,
    .pread = d_pread, .pwrite = d_pwrite, .sync = d_sync, .stat = d_stat, .ioctl = d_ioctl,
};

int ntfs_dev_new(fl_bdev *bd, bool writable, ntfs_dev **out) {
  *out = NULL;
  ntfs_dev *d = calloc(1, sizeof *d);
  if (!d) return FL_ENOMEM;
  d->bd = bd;
  d->writable = writable && !bd->read_only;
  d->dev = ntfs_device_alloc("fluluks", 0, &k_ops, d);
  if (!d->dev) {
    free(d);
    return FL_ENOMEM;
  }
  *out = d;
  return FL_OK;
}

struct ntfs_device *ntfs_dev_device(ntfs_dev *d) { return d->dev; }

void ntfs_dev_free(ntfs_dev *d, bool device_too) {
  if (!d) return;
  if (device_too) ntfs_device_free(d->dev);
  free(d);
}

void ntfs_dev_set_writable(ntfs_dev *d, bool writable) { d->writable = writable && !d->bd->read_only; }

int ntfs_dev_error(const ntfs_dev *d) { return d->err; }
