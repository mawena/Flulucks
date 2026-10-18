#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif

#include "fluluks/blockdev.h"
#include "fluluks/error.h"

typedef struct {
  int fd;
} file_priv;

static int file_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  file_priv *p = d->priv;
  size_t total = (size_t)count * d->sector_size;
  uint64_t off = lba * d->sector_size;
  unsigned char *out = buf;
  size_t done = 0;
  while (done < total) {
    ssize_t r = pread(p->fd, out + done, total - done, (off_t)(off + done));
    if (r < 0) {
      if (errno == EINTR) continue;
      return FL_EIO;
    }
    if (r == 0) return FL_EIO; /* fin de fichier inattendue */
    done += (size_t)r;
  }
  return FL_OK;
}

static int file_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf) {
  file_priv *p = d->priv;
  size_t total = (size_t)count * d->sector_size;
  uint64_t off = lba * d->sector_size;
  const unsigned char *in = buf;
  size_t done = 0;
  while (done < total) {
    ssize_t r = pwrite(p->fd, in + done, total - done, (off_t)(off + done));
    if (r < 0) {
      if (errno == EINTR) continue;
      return FL_EIO;
    }
    if (r == 0) return FL_EIO;
    done += (size_t)r;
  }
  return FL_OK;
}

static int file_flush(fl_bdev *d) {
  file_priv *p = d->priv;
  if (d->read_only) return FL_OK;
  return fsync(p->fd) == 0 ? FL_OK : FL_EIO;
}

static void file_destroy(fl_bdev *d) {
  file_priv *p = d->priv;
  if (p) {
    close(p->fd);
    free(p);
  }
  free(d);
}

static const fl_bdev_ops file_ops = {file_read, file_write, file_flush, file_destroy};

int fl_file_bdev_open(const char *path, uint32_t sector_size, bool read_only, fl_bdev **out) {
  if (!path || !out) return FL_EINVAL;
  *out = NULL;
  int fd;
  do {
    fd = open(path, (read_only ? O_RDONLY : O_RDWR) | O_CLOEXEC);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0) return errno == ENOENT ? FL_ENOENT : FL_EIO;

  struct stat st;
  if (fstat(fd, &st) != 0) {
    close(fd);
    return FL_EIO;
  }

  uint64_t size;
  if (S_ISBLK(st.st_mode)) {
#ifdef __linux__
    int ss = 0;
    unsigned long long sz = 0;
    if (ioctl(fd, BLKSSZGET, &ss) != 0 || ioctl(fd, BLKGETSIZE64, &sz) != 0 || ss <= 0) {
      close(fd);
      return FL_EIO;
    }
    sector_size = (uint32_t)ss;
    size = sz;
#else
    close(fd);
    return FL_ENOTSUP;
#endif
  } else {
    if (sector_size == 0) {
      close(fd);
      return FL_EINVAL;
    }
    if (st.st_size < 0) {
      close(fd);
      return FL_EIO;
    }
    size = (uint64_t)st.st_size;
  }

  fl_bdev *d = calloc(1, sizeof *d);
  file_priv *p = calloc(1, sizeof *p);
  if (!d || !p) {
    free(d);
    free(p);
    close(fd);
    return FL_ENOMEM;
  }
  p->fd = fd;
  d->ops = &file_ops;
  d->sector_size = sector_size;
  d->sector_count = size / sector_size;
  d->read_only = read_only;
  d->priv = p;
  *out = d;
  return FL_OK;
}
