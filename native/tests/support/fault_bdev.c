#include "fault_bdev.h"

#include <pthread.h>
#include <stdlib.h>

#include "fluluks/error.h"

typedef struct {
  fl_bdev *parent;
  uint64_t writes_ok, writes, flushes, reads;
  uint64_t count_from, counted_writes; /* fl_fault_bdev_count_from */
  int err;
  int read_err; /* fl_fault_bdev_fail_reads : 0 = lectures normales */
  pthread_mutex_t mu;
  char *trace; /* fl_fault_bdev_trace */
  size_t trace_cap, trace_len;
  uint32_t trace_bs;
} fault;

static uint32_t be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Vrai si l'écriture contient, au début d'un bloc de trace_bs octets, un en-tête de commit jbd2. */
static int has_commit(const fault *f, uint64_t lba, uint32_t count, uint32_t ss, const uint8_t *buf) {
  uint64_t start = lba * ss, end = start + (uint64_t)count * ss;
  for (uint64_t o = (start + f->trace_bs - 1) / f->trace_bs * f->trace_bs; o + 12 <= end; o += f->trace_bs) {
    const uint8_t *b = buf + (o - start);
    if (be32(b) == 0xC03B3998u && be32(b + 4) == 2) return 1;
  }
  return 0;
}

/* Sous f->mu. */
static void trace_add(fault *f, char c) {
  if (f->trace && f->trace_len + 1 < f->trace_cap) {
    f->trace[f->trace_len++] = c;
    f->trace[f->trace_len] = 0;
  }
}

static int f_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  fault *f = d->priv;
  pthread_mutex_lock(&f->mu);
  int re = f->read_err;
  f->reads++;
  pthread_mutex_unlock(&f->mu);
  if (re) return re;
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
  pthread_mutex_lock(&f->mu);
  if (rc != FL_OK) f->writes--;
  else if (lba >= f->count_from) f->counted_writes++;
  if (rc == FL_OK && f->trace) trace_add(f, has_commit(f, lba, count, d->sector_size, buf) ? 'C' : 'W');
  pthread_mutex_unlock(&f->mu);
  return rc;
}

static int f_flush(fl_bdev *d) {
  fault *f = d->priv;
  pthread_mutex_lock(&f->mu);
  f->flushes++;
  trace_add(f, 'F');
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
  f->count_from = UINT64_MAX;
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

void fl_fault_bdev_trace(fl_bdev *d, uint32_t block_size, char *buf, size_t cap) {
  fault *f = d->priv;
  pthread_mutex_lock(&f->mu);
  f->trace = cap ? buf : NULL;
  f->trace_cap = cap;
  f->trace_len = 0;
  f->trace_bs = block_size ? block_size : 4096;
  if (f->trace) f->trace[0] = 0;
  pthread_mutex_unlock(&f->mu);
}

void fl_fault_bdev_fail_reads(fl_bdev *d, int err) {
  fault *f = d->priv;
  pthread_mutex_lock(&f->mu);
  f->read_err = err < 0 ? err : 0;
  pthread_mutex_unlock(&f->mu);
}

uint64_t fl_fault_bdev_reads(const fl_bdev *d) {
  fault *f = d->priv;
  pthread_mutex_lock(&f->mu);
  uint64_t v = f->reads;
  pthread_mutex_unlock(&f->mu);
  return v;
}

void fl_fault_bdev_count_from(fl_bdev *d, uint64_t lba) {
  fault *f = d->priv;
  pthread_mutex_lock(&f->mu);
  f->count_from = lba;
  f->counted_writes = 0;
  pthread_mutex_unlock(&f->mu);
}

uint64_t fl_fault_bdev_counted_writes(const fl_bdev *d) {
  fault *f = d->priv;
  pthread_mutex_lock(&f->mu);
  uint64_t v = f->counted_writes;
  pthread_mutex_unlock(&f->mu);
  return v;
}
