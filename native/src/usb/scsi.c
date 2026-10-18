/* Commandes SCSI (jeu « block commands ») au-dessus de BOT, avec traduction du sens
 * (REQUEST SENSE) et politique de réessai. Réponses du périphérique hostiles. */

#include <string.h>

#include "usb/usb_internal.h"

#define CHECK 1 /* CHECK CONDITION : sens disponible */

typedef struct {
  uint8_t key, asc, ascq;
} sense_t;

static uint64_t rdbe(const uint8_t *p, int n) {
  uint64_t v = 0;
  for (int i = 0; i < n; i++) v = v << 8 | p[i];
  return v;
}
static void wrbe(uint8_t *p, uint64_t v, int n) {
  for (int i = n - 1; i >= 0; i--) {
    p[i] = (uint8_t)v;
    v >>= 8;
  }
}

static int request_sense(fl_usb_dev *u, sense_t *s) {
  uint8_t cdb[6] = {0x03, 0, 0, 0, 18, 0};
  uint8_t r[18];
  memset(r, 0, sizeof r);
  uint32_t got = 0;
  uint8_t st = 0;
  int rc = fl_bot_exec(u, cdb, sizeof cdb, FL_BOT_IN, r, sizeof r, &got, &st);
  if (rc != FL_OK) return rc;
  if (st != 0 || got < 1) return FL_EIO;
  memset(s, 0, sizeof *s);
  switch (r[0] & 0x7F) {
    case 0x70:
    case 0x71: /* format fixe */
      if (got < 8) return FL_EIO;
      s->key = r[2] & 0x0F;
      if (got >= 14) {
        s->asc = r[12];
        s->ascq = r[13];
      }
      return FL_OK;
    case 0x72:
    case 0x73: /* format descripteur */
      if (got < 4) return FL_EIO;
      s->key = r[1] & 0x0F;
      s->asc = r[2];
      s->ascq = r[3];
      return FL_OK;
    default:
      return FL_EIO;
  }
}

/* Une commande : FL_OK, CHECK (sens rempli) ou fl_err. */
static int command(fl_usb_dev *u, const uint8_t *cdb, uint8_t cdblen, int dir, void *buf, uint32_t len,
                   uint32_t *got, sense_t *s) {
  uint8_t st = 0;
  int rc = fl_bot_exec(u, cdb, cdblen, dir, buf, len, got, &st);
  if (rc != FL_OK) return rc;
  if (st == 0) return FL_OK;
  rc = request_sense(u, s);
  return rc != FL_OK ? rc : CHECK;
}

enum { A_FAIL, A_RETRY, A_WAIT, A_START };

static int classify(const sense_t *s, int *err) {
  *err = FL_EIO;
  switch (s->key) {
    case 0x00: /* NO SENSE */
    case 0x01: /* RECOVERED ERROR */
    case 0x06: /* UNIT ATTENTION (support changé, remise à zéro…) */
    case 0x0B: /* ABORTED COMMAND */
      return A_RETRY;
    case 0x02: /* NOT READY */
      if (s->asc == 0x3A) return A_FAIL; /* support absent : ne se règle pas en attendant */
      if (s->asc == 0x04 && s->ascq == 0x03) return A_FAIL; /* intervention manuelle requise : idem */
      if (s->asc == 0x04 && s->ascq == 0x02) return A_START; /* « initializing command required » */
      return A_WAIT;
    case 0x07: /* DATA PROTECT */
      *err = FL_EROFS;
      return A_FAIL;
    default: /* MEDIUM ERROR, HARDWARE ERROR, ILLEGAL REQUEST… */
      return A_FAIL;
  }
}

/* START STOP UNIT avec START=1, LOEJ=0, IMMED=0 : demande au disque de démarrer. Aucune donnée
 * n'est écrite sur le support ; la commande n'est envoyée QU'EN RÉPONSE au sens NOT READY 04/02.
 * Son issue importe peu (l'attente TEST UNIT READY tranche), sauf un débranchement. */
static int start_unit(fl_usb_dev *u) {
  uint8_t cdb[6] = {0x1B, 0x00, 0, 0, 0x01, 0};
  sense_t s;
  uint32_t got = 0;
  int rc = command(u, cdb, sizeof cdb, FL_BOT_NONE, NULL, 0, &got, &s);
  return rc == FL_ENODEV ? FL_ENODEV : FL_OK;
}

/* Exécute une commande avec réessais : immédiats (UNIT ATTENTION, transfert court…) au plus
 * FL_USB_QUICK_RETRIES fois, après attente tant que NOT READY, dans FL_USB_READY_BUDGET_MS.
 * NOT READY 04/02 : un seul START STOP UNIT par appel, puis l'attente reprend.
 * Succès seulement si au moins `need` octets de données sont arrivés. Sur un échec de commande,
 * *last (si non NULL) reçoit le dernier sens. Une écriture réessayée est rejouée entière (même
 * données, même adresse). */
static int run_s(fl_usb_dev *u, const uint8_t *cdb, uint8_t cdblen, int dir, void *buf, uint32_t len, uint32_t need,
                 uint32_t *got_out, sense_t *last) {
  uint64_t deadline = u->clk.now_ms(u->clk.ctx) + FL_USB_READY_BUDGET_MS;
  int quick = 0;
  bool started = false;
  for (;;) {
    sense_t s;
    uint32_t got = 0;
    int rc = command(u, cdb, cdblen, dir, buf, len, &got, &s);
    if (rc == FL_OK) {
      if (got >= need) {
        if (got_out) *got_out = got;
        return FL_OK;
      }
      if (++quick > FL_USB_QUICK_RETRIES) return FL_EIO; /* réponse trop courte */
      continue;
    }
    if (rc < 0) return rc == FL_ESTALL ? FL_EIO : rc; /* FL_ESTALL ne sort pas d'ici */
    if (last) *last = s;
    int err;
    switch (classify(&s, &err)) {
      case A_FAIL:
        return err;
      case A_RETRY:
        if (++quick > FL_USB_QUICK_RETRIES) return FL_EIO;
        continue;
      case A_START:
        if (!started) {
          started = true;
          rc = start_unit(u);
          if (rc != FL_OK) return rc; /* FL_ENODEV */
          continue;
        }
        /* déjà envoyé : on attend comme pour tout NOT READY */
        /* fall through */
      default: {
        uint64_t now = u->clk.now_ms(u->clk.ctx);
        if (now >= deadline) return FL_ETIMEDOUT;
        uint64_t left = deadline - now;
        u->clk.sleep_ms(u->clk.ctx, left < FL_USB_READY_POLL_MS ? (unsigned)left : FL_USB_READY_POLL_MS);
      }
    }
  }
}

static int run(fl_usb_dev *u, const uint8_t *cdb, uint8_t cdblen, int dir, void *buf, uint32_t len, uint32_t need,
               uint32_t *got_out) {
  return run_s(u, cdb, cdblen, dir, buf, len, need, got_out, NULL);
}

uint8_t fl_scsi_build_read(uint64_t lba, uint32_t n, uint8_t cdb[16]) {
  memset(cdb, 0, 16);
  if (n <= 0xFFFFu && lba <= 0xFFFFFFFFull && (n == 0 || lba + n - 1 <= 0xFFFFFFFFull)) {
    cdb[0] = 0x28; /* READ(10) */
    wrbe(cdb + 2, lba, 4);
    wrbe(cdb + 7, n, 2);
    return 10;
  }
  cdb[0] = 0x88; /* READ(16) */
  wrbe(cdb + 2, lba, 8);
  wrbe(cdb + 10, n, 4);
  return 16;
}

int fl_scsi_inquiry(fl_usb_dev *u) {
  uint8_t cdb[6] = {0x12, 0, 0, 0, 36, 0};
  uint8_t r[36];
  memset(r, 0, sizeof r);
  int rc = run(u, cdb, sizeof cdb, FL_BOT_IN, r, sizeof r, 5, NULL);
  if (rc != FL_OK) return rc;
  uint8_t qualifier = r[0] >> 5, type = r[0] & 0x1F;
  if (qualifier != 0) return FL_ENOTSUP; /* unité logique absente */
  /* Accès direct, CD/DVD, optique, accès direct simplifié (RBC). */
  if (type != 0x00 && type != 0x05 && type != 0x07 && type != 0x0E) return FL_ENOTSUP;
  return FL_OK;
}

int fl_scsi_wait_ready(fl_usb_dev *u) {
  uint8_t cdb[6] = {0x00, 0, 0, 0, 0, 0};
  return run(u, cdb, sizeof cdb, FL_BOT_NONE, NULL, 0, 0, NULL);
}

int fl_scsi_read_capacity(fl_usb_dev *u, uint64_t *blocks, uint32_t *block_size) {
  uint8_t cdb[10] = {0x25};
  uint8_t r[8];
  memset(r, 0, sizeof r);
  int rc = run(u, cdb, sizeof cdb, FL_BOT_IN, r, sizeof r, sizeof r, NULL);
  if (rc != FL_OK) return rc;
  uint64_t last = rdbe(r, 4);
  uint32_t bs = (uint32_t)rdbe(r + 4, 4);
  if (last == 0xFFFFFFFFull) {
    uint8_t c16[16] = {0x9E, 0x10}; /* SERVICE ACTION IN(16) / READ CAPACITY(16) */
    uint8_t r16[32];
    memset(r16, 0, sizeof r16);
    wrbe(c16 + 10, sizeof r16, 4);
    rc = run(u, c16, sizeof c16, FL_BOT_IN, r16, sizeof r16, 12, NULL);
    if (rc != FL_OK) return rc;
    last = rdbe(r16, 8);
    bs = (uint32_t)rdbe(r16 + 8, 4);
  }
  if (bs != 512 && bs != 1024 && bs != 2048 && bs != 4096) return FL_ENOTSUP;
  if (last == UINT64_MAX) return FL_EIO;  /* capacité nulle (dernier LBA + 1 déborde) */
  uint64_t n = last + 1;
  if (n > UINT64_MAX / bs) return FL_EIO; /* taille en octets non représentable */
  *blocks = n;
  *block_size = bs;
  return FL_OK;
}

int fl_scsi_read(fl_usb_dev *u, uint64_t lba, uint32_t n, uint32_t block_size, void *buf) {
  if (n == 0 || block_size == 0 || n > FL_USB_MAX_XFER / block_size) return FL_EINVAL;
  uint8_t cdb[16];
  uint8_t len = fl_scsi_build_read(lba, n, cdb);
  uint32_t bytes = n * block_size;
  return run(u, cdb, len, FL_BOT_IN, buf, bytes, bytes, NULL);
}

uint8_t fl_scsi_build_write(uint64_t lba, uint32_t n, uint8_t cdb[16]) {
  uint8_t len = fl_scsi_build_read(lba, n, cdb);
  cdb[0] = len == 10 ? 0x2A : 0x8A; /* WRITE(10) / WRITE(16) : même disposition que READ */
  return len;
}

int fl_scsi_write(fl_usb_dev *u, uint64_t lba, uint32_t n, uint32_t block_size, const void *buf) {
  if (n == 0 || block_size == 0 || n > FL_USB_MAX_XFER / block_size) return FL_EINVAL;
  uint8_t cdb[16];
  uint8_t len = fl_scsi_build_write(lba, n, cdb);
  uint32_t bytes = n * block_size;
  /* BOT n'écrit pas dans buf pour une phase OUT */
  return run(u, cdb, len, FL_BOT_OUT, (void *)buf, bytes, bytes, NULL);
}

int fl_scsi_mode_sense_wp(fl_usb_dev *u, bool *wp) {
  /* MODE SENSE(6), DBD=0, page 0x3F (toutes), 192 octets alloués : la réponse compte au moins
   * l'en-tête de 4 octets, dont l'octet 2 porte le bit WP (bit 7). */
  uint8_t cdb[6] = {0x1A, 0, 0x3F, 0, 192, 0};
  uint8_t r[192];
  memset(r, 0, sizeof r);
  int rc = run(u, cdb, sizeof cdb, FL_BOT_IN, r, sizeof r, 4, NULL);
  if (rc != FL_OK) return rc;
  *wp = (r[2] & 0x80) != 0;
  return FL_OK;
}

int fl_scsi_sync_cache(fl_usb_dev *u) {
  uint8_t cdb[10] = {0x35};
  sense_t s = {0, 0, 0};
  int rc = run_s(u, cdb, sizeof cdb, FL_BOT_NONE, NULL, 0, 0, NULL, &s);
  /* ILLEGAL REQUEST : commande non prise en charge, le disque n'a pas de cache à vider */
  if (rc == FL_EIO && s.key == 0x05) return FL_OK;
  return rc;
}
