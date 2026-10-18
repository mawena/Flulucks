#include "fault_bdev.h"

#include <pthread.h>
#include <stdlib.h>

#include "fluluks/error.h"

typedef struct {
  fl_bdev *parent;
  uint64_t writes_ok, writes, flushes;
  int err;
  pthread_mutex_t mu;
} fault;

static int f_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  fault *f = d->priv;
  return fl_bdev_read(f->parent, lba, count, buf);
}

static int f_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf) {
  fault *f = d->priv;
  pthread_mutex_lock(&f->mu);
  int cut = f->writes >= f->writes_ok;
  if (!cut) f->writes++; /* réservé avant l'écriture : deux fils ne passent pas tous deux la limite */
  pthread_mutex_unlock(&f->mu);
  if (cut) return f->err;
  int rc = fl_bdev_write(f->parent, lba, count, buf);
  if (rc != FL_OK) {
    pthread_mutex_lock(&f->mu);
    f->writes--;
    pthread_mutex_unlock(&f->mu);
  }
  return rc;
}

static int f_flush(fl_bdev *d) {
  fault *f = d->priv;
  pthread_mutex_lock(&f->mu);
  f->flushes++;
  pthread_mutex_unlock(&f->mu);
  return fl_bdev_flush(f->parent);
}

static void f_destroy(fl_bdev *d) {
  fault *f = d->priv;
  pthread_mutex_destroy(&f->mu);
  free(f);
  free(d);
}

static const fl_bdev_ops fault_ops = {f_read, f_write, f_flush, f_destroy};

int fl_fault_bdev_new(fl_bdev *parent, uint64_t writes_ok, int err, fl_bdev **out) {
  if (!parent || !out || err >= 0) return FL_EINVAL;
  fl_bdev *d = calloc(1, sizeof *d);
  fault *f = calloc(1, sizeof *f);
  if (!d || !f) {
    free(d);
    free(f);
    return FL_ENOMEM;
  }
  f->parent = parent;
  f->writes_ok = writes_ok;
  f->err = err;
  pthread_mutex_init(&f->mu, NULL);
  d->ops = &fault_ops;
  d->sector_size = parent->sector_size;
  d->sector_count = parent->sector_count;
  d->read_only = parent->read_only;
  d->priv = f;
  *out = d;
  return FL_OK;
}

uint64_t fl_fault_bdev_writes(const fl_bdev *d) {
  fault *f = d->priv;
  pthread_mutex_lock(&f->mu);
  uint64_t v = f->writes;
  pthread_mutex_unlock(&f->mu);
  return v;
}

uint64_t fl_fault_bdev_flushes(const fl_bdev *d) {
  fault *f = d->priv;
  pthread_mutex_lock(&f->mu);
  uint64_t v = f->flushes;
  pthread_mutex_unlock(&f->mu);
  return v;
}
