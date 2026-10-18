/* Disque USB de stockage de masse vu comme un fl_bdev, en lecture ou en écriture. */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#include "usb/usb_internal.h"

static int usb_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf);
static int usb_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf);
static int usb_flush(fl_bdev *d);
static void usb_destroy(fl_bdev *d);
static const fl_bdev_ops usb_ops = {usb_read, usb_write, usb_flush, usb_destroy};

static uint64_t real_now(void *ctx) {
  (void)ctx;
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void real_sleep(void *ctx, unsigned ms) {
  (void)ctx;
  struct timespec ts = {(time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L};
  while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
  }
}

const fl_usb_clock fl_usb_real_clock = {real_now, real_sleep, NULL};

typedef struct {
  fl_usb_dev u;
  pthread_mutex_t lock; /* sérialise les commandes (CBW → données → CSW, reprise comprise) */
} usb_priv;

static int usb_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  usb_priv *p = d->priv;
#if SIZE_MAX < UINT64_MAX
  if ((uint64_t)count * d->sector_size > SIZE_MAX) return FL_EINVAL;
#endif
  uint32_t per = FL_USB_MAX_XFER / d->sector_size; /* >= 16 : sector_size <= 4096 */
  uint8_t *out = buf;
  int rc = FL_OK;
  pthread_mutex_lock(&p->lock);
  while (count) {
    if (p->u.dead) {
      rc = FL_ENODEV;
      break;
    }
    uint32_t n = count < per ? count : per;
    rc = fl_scsi_read(&p->u, lba, n, d->sector_size, out);
    if (rc != FL_OK) break;
    lba += n;
    count -= n;
    out += (size_t)n * d->sector_size;
  }
  pthread_mutex_unlock(&p->lock);
  return rc;
}

/* fl_bdev_write a déjà refusé un périphérique ouvert en lecture seule (FL_EROFS, rien n'est
 * envoyé). 64 Kio au plus par WRITE. DATA PROTECT → FL_EROFS et protection collante ; après un
 * FL_ENODEV (débranchement, support changé), plus rien n'est envoyé. */
static int usb_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf) {
  usb_priv *p = d->priv;
#if SIZE_MAX < UINT64_MAX
  if ((uint64_t)count * d->sector_size > SIZE_MAX) return FL_EINVAL;
#endif
  uint32_t per = FL_USB_MAX_XFER / d->sector_size;
  const uint8_t *in = buf;
  int rc = FL_OK;
  pthread_mutex_lock(&p->lock);
  while (count) {
    if (p->u.dead) {
      rc = FL_ENODEV;
      break;
    }
    if (p->u.wp) {
      rc = FL_EROFS;
      break;
    }
    uint32_t n = count < per ? count : per;
    rc = fl_scsi_write(&p->u, lba, n, d->sector_size, in);
    if (rc == FL_EROFS) p->u.wp = true;
    if (rc != FL_OK) break;
    lba += n;
    count -= n;
    in += (size_t)n * d->sector_size;
  }
  pthread_mutex_unlock(&p->lock);
  return rc;
}

/* Après une UNIT ATTENTION de remise à zéro vue depuis l'ouverture (u.ua_reset), le cache volatil du
 * disque a pu perdre des écritures déjà acquittées : un SYNCHRONIZE CACHE réussi mentirait, le flush
 * rend FL_EIO (durablement) sans rien envoyer. DATA PROTECT → protection collante. */
static int usb_flush(fl_bdev *d) {
  if (d->read_only) return FL_OK;
  usb_priv *p = d->priv;
  pthread_mutex_lock(&p->lock);
  int rc;
  if (p->u.dead) {
    rc = FL_ENODEV;
  } else if (p->u.ua_reset) {
    rc = FL_EIO;
  } else {
    rc = fl_scsi_sync_cache(&p->u);
    if (rc == FL_EROFS) p->u.wp = true;
  }
  pthread_mutex_unlock(&p->lock);
  return rc;
}

bool fl_usb_bdev_write_protected(const fl_bdev *d) {
  if (!d || d->ops != &usb_ops) return false;
  usb_priv *p = d->priv;
  pthread_mutex_lock(&p->lock);
  bool wp = p->u.wp;
  pthread_mutex_unlock(&p->lock);
  return wp;
}

bool fl_usb_bdev_sync_unsupported(const fl_bdev *d) {
  if (!d || d->ops != &usb_ops) return false;
  usb_priv *p = d->priv;
  pthread_mutex_lock(&p->lock);
  bool r = p->u.sync_unsupported;
  pthread_mutex_unlock(&p->lock);
  return r;
}

static void usb_destroy(fl_bdev *d) {
  usb_priv *p = d->priv;
  if (p) {
    pthread_mutex_destroy(&p->lock);
    free(p);
  }
  free(d);
}

int fl_usb_bdev_open_ex(const fl_usb_transport *t, uint8_t lun, bool read_only, const fl_usb_clock *clk,
                        fl_bdev **out) {
  if (!out) return FL_EINVAL;
  *out = NULL;
  if (!t || !t->bulk_out || !t->bulk_in || !t->clear_halt || !t->bot_reset || lun > 15) return FL_EINVAL;
  if (clk && (!clk->now_ms || !clk->sleep_ms)) return FL_EINVAL;

  fl_bdev *d = calloc(1, sizeof *d);
  usb_priv *p = calloc(1, sizeof *p);
  if (!d || !p) {
    free(d);
    free(p);
    return FL_ENOMEM;
  }
  if (pthread_mutex_init(&p->lock, NULL) != 0) {
    free(d);
    free(p);
    return FL_ENOMEM;
  }
  p->u.t = *t;
  p->u.clk = clk ? *clk : fl_usb_real_clock;
  p->u.lun = lun;
  d->priv = p;

  /* Pas encore partagé : pas besoin du verrou pendant l'ouverture. */
  uint64_t blocks = 0;
  uint32_t bs = 0;
  int rc = fl_scsi_inquiry(&p->u);
  if (rc == FL_OK) rc = fl_scsi_wait_ready(&p->u);
  if (rc == FL_OK) rc = fl_scsi_read_capacity(&p->u, &blocks, &bs);
  /* Écriture demandée : MODE SENSE dit si le disque est protégé (verrou d'une carte SD, clé en
   * lecture seule) ; son échec n'empêche pas l'ouverture et ne force rien, sauf un refus DATA PROTECT
   * qui vaut protection. */
  if (rc == FL_OK && !read_only) {
    bool wp = false;
    int ms = fl_scsi_mode_sense_wp(&p->u, &wp);
    if (ms == FL_ENODEV) rc = ms;
    else if (ms == FL_EROFS || (ms == FL_OK && wp)) p->u.wp = read_only = true;
  }
  if (rc != FL_OK) {
    usb_destroy(d);
    return rc;
  }
  d->ops = &usb_ops;
  d->sector_size = bs;
  d->sector_count = blocks;
  d->read_only = read_only;
  p->u.opened = true; /* désormais, UNIT ATTENTION n'est plus réessayée à l'aveugle (scsi.c) */
  *out = d;
  return FL_OK;
}

int fl_usb_bdev_open(const fl_usb_transport *t, uint8_t lun, bool read_only, fl_bdev **out) {
  return fl_usb_bdev_open_ex(t, lun, read_only, NULL, out);
}
