/* Transport USB sur usbdevfs (Linux et Android) : uniquement <linux/usbdevice_fs.h> + POSIX.
 * Le descripteur appartient à l'appelant (Kotlin, UsbDeviceConnection) ; il n'est jamais fermé ici. */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include <linux/usbdevice_fs.h>

#include "fluluks/usb.h"

#define DEFAULT_TIMEOUT_MS 5000u
#define CHUNK (16u * 1024u) /* repli des noyaux qui refusent les gros transferts bulk */

typedef struct {
  fl_usb_transport t; /* premier membre : fl_usb_transport* ↔ usbdevfs_ctx* */
  int fd;
  uint8_t iface, ep_in, ep_out;
} usbdevfs_ctx;

static int map_errno(int e) {
  switch (e) {
    case ENODEV:
    case ESHUTDOWN:
    case EPROTO: /* vu après débranchement */
      return FL_ENODEV;
    case ETIMEDOUT:
      return FL_ETIMEDOUT;
    case EPIPE:
      return FL_ESTALL;
    default:
      return FL_EIO;
  }
}

/* Un ioctl BULK : octets transférés, ou -errno. */
static int bulk_once(int fd, unsigned ep, void *buf, size_t len, unsigned timeout_ms) {
  struct usbdevfs_bulktransfer bt;
  bt.ep = ep;
  bt.len = (unsigned)len;
  bt.timeout = timeout_ms;
  bt.data = buf;
  int r;
  do {
    r = ioctl(fd, USBDEVFS_BULK, &bt);
  } while (r < 0 && errno == EINTR);
  if (r < 0) return -errno;
  return r;
}

static int bulk(usbdevfs_ctx *c, unsigned ep, void *buf, size_t len, unsigned timeout_ms) {
  if (len > INT_MAX || (len && !buf)) return FL_EINVAL;
  if (timeout_ms == 0) timeout_ms = DEFAULT_TIMEOUT_MS;
  int r = bulk_once(c->fd, ep, buf, len, timeout_ms);
  if (r >= 0) return r <= (int)len ? r : FL_EIO;
  if ((r == -EINVAL || r == -ENOMEM) && len > CHUNK) {
    /* Relance en morceaux de 16 Kio ; un morceau court termine le transfert. */
    unsigned char *p = buf;
    size_t done = 0;
    while (done < len) {
      size_t n = len - done < CHUNK ? len - done : CHUNK;
      r = bulk_once(c->fd, ep, p + done, n, timeout_ms);
      if (r < 0) return map_errno(-r);
      if ((size_t)r > n) return FL_EIO;
      done += (size_t)r;
      if ((size_t)r < n) break;
    }
    return (int)done;
  }
  return map_errno(-r);
}

static int t_bulk_out(void *ctx, const void *buf, size_t len, unsigned timeout_ms) {
  usbdevfs_ctx *c = ctx;
  /* L'ioctl ne modifie pas un tampon OUT : la conversion ôte seulement const. */
  return bulk(c, c->ep_out, (void *)(uintptr_t)buf, len, timeout_ms);
}

static int t_bulk_in(void *ctx, void *buf, size_t len, unsigned timeout_ms) {
  usbdevfs_ctx *c = ctx;
  return bulk(c, c->ep_in, buf, len, timeout_ms);
}

static int t_clear_halt(void *ctx, bool in_endpoint) {
  usbdevfs_ctx *c = ctx;
  unsigned ep = in_endpoint ? c->ep_in : c->ep_out;
  int r;
  do {
    r = ioctl(c->fd, USBDEVFS_CLEAR_HALT, &ep);
  } while (r < 0 && errno == EINTR);
  return r < 0 ? map_errno(errno) : FL_OK;
}

static int t_bot_reset(void *ctx) {
  usbdevfs_ctx *c = ctx;
  struct usbdevfs_ctrltransfer ct;
  ct.bRequestType = 0x21; /* classe, interface, hôte → périphérique */
  ct.bRequest = 0xFF;     /* Bulk-Only Mass Storage Reset */
  ct.wValue = 0;
  ct.wIndex = c->iface;
  ct.wLength = 0;
  ct.timeout = DEFAULT_TIMEOUT_MS;
  ct.data = NULL;
  int r;
  do {
    r = ioctl(c->fd, USBDEVFS_CONTROL, &ct);
  } while (r < 0 && errno == EINTR);
  if (r < 0) return map_errno(errno);
  int a = t_clear_halt(ctx, true);
  if (a == FL_ENODEV) return a;
  int b = t_clear_halt(ctx, false);
  return a != FL_OK ? a : b;
}

int fl_usbdevfs_transport_new(int fd, uint8_t iface, uint8_t ep_in, uint8_t ep_out, fl_usb_transport **out) {
  if (!out) return FL_EINVAL;
  *out = NULL;
  if (fd < 0 || !(ep_in & 0x80) || (ep_out & 0x80)) return FL_EINVAL;
  usbdevfs_ctx *c = calloc(1, sizeof *c);
  if (!c) return FL_ENOMEM;
  c->fd = fd;
  c->iface = iface;
  c->ep_in = ep_in;
  c->ep_out = ep_out;
  c->t.ctx = c;
  c->t.bulk_out = t_bulk_out;
  c->t.bulk_in = t_bulk_in;
  c->t.clear_halt = t_clear_halt;
  c->t.bot_reset = t_bot_reset;
  *out = &c->t;
  return FL_OK;
}

void fl_usbdevfs_transport_free(fl_usb_transport *t) {
  if (!t) return;
  free(t->ctx); /* ctx == conteneur ; fd n'est pas fermé */
}
