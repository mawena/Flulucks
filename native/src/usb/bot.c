/* USB Mass Storage Bulk-Only Transport (BOT) : CBW de 31 octets, données, CSW de 13 octets.
 * Toute réponse du périphérique est hostile : longueurs, signature, tag et résidu vérifiés. */

#include <string.h>

#include "usb/usb_internal.h"

#define CBW_SIG 0x43425355u
#define CSW_SIG 0x53425355u
#define CBW_LEN 31
#define CSW_LEN 13

/* Issue d'une transaction : FL_OK, erreur fatale (< 0), ou NEED_RESET (reprise requise). */
#define NEED_RESET 1

static void wr32le(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t rd32le(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Normalise le résultat d'un appel de transport : FL_ENODEV rend le périphérique mort,
 * une longueur impossible (> demandée) devient FL_EIO. */
static int chk(fl_usb_dev *u, int rc, size_t asked) {
  if (rc == FL_ENODEV) u->dead = true;
  if (rc >= 0 && (size_t)rc > asked) return FL_EIO;
  return rc;
}

static int t_out(fl_usb_dev *u, const void *buf, size_t len) {
  return chk(u, u->t.bulk_out(u->t.ctx, buf, len, FL_USB_XFER_TIMEOUT_MS), len);
}
static int t_in(fl_usb_dev *u, void *buf, size_t len) {
  return chk(u, u->t.bulk_in(u->t.ctx, buf, len, FL_USB_XFER_TIMEOUT_MS), len);
}
static int t_clear(fl_usb_dev *u, bool in) { return chk(u, u->t.clear_halt(u->t.ctx, in), 0); }

/* Erreur de transfert non fatale → reprise ; mémorise la cause pour l'échec final. */
static int soft(int rc, int *cause) {
  if (rc == FL_ENODEV) return FL_ENODEV;
  *cause = rc == FL_ETIMEDOUT ? FL_ETIMEDOUT : FL_EIO;
  return NEED_RESET;
}

static int bot_once(fl_usb_dev *u, const uint8_t *cdb, uint8_t cdblen, int dir, void *buf, uint32_t len,
                    uint32_t *done, uint8_t *status, int *cause) {
  uint8_t cbw[CBW_LEN];
  memset(cbw, 0, sizeof cbw);
  uint32_t tag = ++u->tag;
  wr32le(cbw, CBW_SIG);
  wr32le(cbw + 4, tag);
  wr32le(cbw + 8, len);
  cbw[12] = (dir == FL_BOT_IN) ? 0x80 : 0x00;
  cbw[13] = u->lun & 0x0F;
  cbw[14] = cdblen;
  memcpy(cbw + 15, cdb, cdblen);

  int rc = t_out(u, cbw, sizeof cbw);
  if (rc != CBW_LEN) return rc >= 0 ? soft(FL_EIO, cause) : soft(rc, cause);

  /* Phase de données. */
  uint32_t n = 0;
  if (len > 0) {
    rc = dir == FL_BOT_IN ? t_in(u, buf, len) : t_out(u, buf, len);
    if (rc >= 0) {
      n = (uint32_t)rc;
    } else if (rc == FL_ESTALL) {
      /* Blocage : on débloque ce point d'accès, puis on lit quand même le CSW. */
      rc = t_clear(u, dir == FL_BOT_IN);
      if (rc != FL_OK) return soft(rc, cause);
    } else {
      return soft(rc, cause);
    }
  }

  /* CSW ; un blocage à ce stade se lève une fois, puis on relit. */
  uint8_t csw[CSW_LEN];
  rc = t_in(u, csw, CSW_LEN);
  if (rc == FL_ESTALL) {
    rc = t_clear(u, true);
    if (rc != FL_OK) return soft(rc, cause);
    rc = t_in(u, csw, CSW_LEN);
  }
  if (rc < 0) return soft(rc, cause);
  if (rc != CSW_LEN) return soft(FL_EIO, cause);

  uint32_t residue = rd32le(csw + 8);
  uint8_t st = csw[12];
  if (rd32le(csw) != CSW_SIG || rd32le(csw + 4) != tag || st > 2 || residue > len) return soft(FL_EIO, cause);
  if (st == 2) return soft(FL_EIO, cause); /* erreur de phase */

  /* Données utiles : ce qui a été reçu, borné par ce que le résidu annonce. */
  uint32_t claimed = len - residue;
  *done = n < claimed ? n : claimed;
  *status = st;
  return FL_OK;
}

int fl_bot_exec(fl_usb_dev *u, const uint8_t *cdb, uint8_t cdblen, int dir, void *buf, uint32_t len,
                uint32_t *done, uint8_t *status) {
  if (!u || !cdb || cdblen < 1 || cdblen > 16 || !done || !status || (len && !buf)) return FL_EINVAL;
  if ((dir == FL_BOT_NONE) != (len == 0) || (dir != FL_BOT_NONE && dir != FL_BOT_IN && dir != FL_BOT_OUT))
    return FL_EINVAL;
  *done = 0;
  *status = 0;
  if (u->dead) return FL_ENODEV;
  int cause = FL_EIO;
  for (int attempt = 0; attempt < 2; attempt++) {
    int rc = bot_once(u, cdb, cdblen, dir, buf, len, done, status, &cause);
    if (rc != NEED_RESET) return rc; /* FL_OK ou erreur fatale (FL_ENODEV) */
    /* Reprise : Bulk-Only Mass Storage Reset + clear_halt des deux points d'accès. */
    rc = chk(u, u->t.bot_reset(u->t.ctx), 0);
    if (rc == FL_ENODEV) return FL_ENODEV;
    if (rc != FL_OK) return cause;
  }
  *done = 0;
  return cause;
}
