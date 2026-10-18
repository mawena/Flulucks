#define _DEFAULT_SOURCE
#include "fake_msc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CBW_SIG 0x43425355u
#define CSW_SIG 0x53425355u
#define MAX_DATA (1u << 20)

enum { ST_CBW, ST_DATA_IN, ST_DATA_OUT, ST_CSW };

struct fake_msc {
  fl_usb_transport t;
  int fd;
  uint64_t img_size;
  uint32_t bs;
  uint64_t blocks;
  bool cap_set, force_rc16;
  uint32_t inquiry_len;
  uint8_t peripheral;
  bool rw, wp, wp_ms, ms_illegal, sync_illegal;
  unsigned fail_write; /* 0 : jamais */
  int fail_write_err;
  bool w_pending; /* WRITE accepté : les données de la phase OUT vont dans l'image */
  uint64_t w_off, w_bytes;

  /* pannes */
  bool fail_armed, failing;
  unsigned fail_left;
  int fail_err;
  unsigned stall_data, bad_csw, not_ready, zlp_csw;
  uint8_t nr_asc, nr_ascq;
  bool need_start, zlp_pending, bogus_on;
  uint32_t bogus_residue;
  bool sense_armed;
  uint8_t next_key, next_asc, next_ascq;

  /* machine d'états BOT */
  int st;
  bool halt_in, halt_out, need_reset, stall_this;
  uint8_t *data;
  uint32_t data_len, data_pos, expect;
  uint32_t tag;
  uint8_t op; /* code d'opération de la commande en cours */
  uint8_t status;
  uint8_t key, asc, ascq; /* sens courant */

  atomic_int busy;
  fake_msc_stats s;
};

static uint32_t rd32le(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void wr32le(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
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

static void enter(fake_msc *m) {
  if (atomic_fetch_add(&m->busy, 1) != 0) m->s.violations++;
  m->s.calls++;
}
static void leave(fake_msc *m) { atomic_fetch_sub(&m->busy, 1); }

static void set_sense(fake_msc *m, uint8_t key, uint8_t asc, uint8_t ascq) {
  m->key = key;
  m->asc = asc;
  m->ascq = ascq;
}

static void check_condition(fake_msc *m, uint8_t key, uint8_t asc, uint8_t ascq) {
  m->status = 1;
  m->data_len = 0;
  set_sense(m, key, asc, ascq);
}

/* Réponse de données : copie (tronquée à la longueur attendue par l'hôte). */
static void respond(fake_msc *m, const void *p, uint32_t n) {
  if (n > m->expect) n = m->expect;
  memcpy(m->data, p, n);
  m->data_len = n;
}

/* Pannes des commandes d'accès au support. Vrai si la commande est rejetée. */
static bool media_fault(fake_msc *m) {
  if (m->sense_armed) {
    m->sense_armed = false;
    check_condition(m, m->next_key, m->next_asc, m->next_ascq);
    return true;
  }
  if (m->need_start) {
    check_condition(m, 0x02, 0x04, 0x02);
    return true;
  }
  if (m->not_ready) {
    m->not_ready--;
    check_condition(m, 0x02, m->nr_asc, m->nr_ascq);
    return true;
  }
  return false;
}

static void exec_read(fake_msc *m, uint64_t lba, uint64_t count) {
  if (m->expect > m->s.max_read_xfer) m->s.max_read_xfer = m->expect;
  if (media_fault(m)) return;
  uint64_t img_blocks = m->img_size / m->bs;
  if (lba > img_blocks || count > img_blocks - lba) {
    check_condition(m, 0x05, 0x21, 0x00);
    return;
  }
  uint64_t bytes = count * m->bs;
  if (bytes > m->expect) { /* Hi < Di : erreur de phase */
    m->status = 2;
    m->data_len = 0;
    return;
  }
  uint64_t off = lba * m->bs;
  size_t done = 0;
  while (done < bytes) {
    ssize_t r = pread(m->fd, m->data + done, (size_t)bytes - done, (off_t)(off + done));
    if (r <= 0) {
      if (r < 0 && errno == EINTR) continue;
      check_condition(m, 0x03, 0x11, 0x00);
      return;
    }
    done += (size_t)r;
  }
  m->data_len = (uint32_t)bytes;
}

static void exec_cdb(fake_msc *m, const uint8_t *cdb, uint8_t cdblen, bool dir_in) {
  m->status = 0;
  m->data_len = 0;
  m->s.op[cdb[0]]++;
  uint8_t r[64];
  memset(r, 0, sizeof r);
  bool wants_in = false, wants_out = false;
  switch (cdb[0]) {
    case 0x00: /* TEST UNIT READY */
      media_fault(m);
      break;
    case 0x03: { /* REQUEST SENSE */
      wants_in = true;
      r[0] = 0x70;
      r[2] = m->key;
      r[7] = 10;
      r[12] = m->asc;
      r[13] = m->ascq;
      uint32_t n = cdb[4] < 18 ? cdb[4] : 18;
      respond(m, r, n);
      set_sense(m, 0, 0, 0);
      break;
    }
    case 0x12: { /* INQUIRY */
      wants_in = true;
      r[0] = m->peripheral;
      r[1] = 0x80; /* amovible */
      r[2] = 0x06;
      r[3] = 0x02;
      r[4] = 31;
      memcpy(r + 8, "FluLuks ", 8);
      memcpy(r + 16, "Faux disque     ", 16);
      memcpy(r + 32, "0001", 4);
      uint32_t n = (uint32_t)rdbe(cdb + 3, 2);
      if (n > m->inquiry_len) n = m->inquiry_len;
      respond(m, r, n);
      break;
    }
    case 0x25: { /* READ CAPACITY(10) */
      wants_in = true;
      if (media_fault(m)) break;
      uint64_t last = m->blocks - 1; /* 0 bloc → 0xFFFF... */
      wrbe(r, (m->force_rc16 || last > 0xFFFFFFFEu) ? 0xFFFFFFFFu : last, 4);
      wrbe(r + 4, m->bs, 4);
      respond(m, r, 8);
      break;
    }
    case 0x9E: /* SERVICE ACTION IN(16) */
      wants_in = true;
      if ((cdb[1] & 0x1F) != 0x10) {
        check_condition(m, 0x05, 0x24, 0x00);
        break;
      }
      if (media_fault(m)) break;
      wrbe(r, m->blocks - 1, 8);
      wrbe(r + 8, m->bs, 4);
      respond(m, r, (uint32_t)rdbe(cdb + 10, 4) < 32 ? (uint32_t)rdbe(cdb + 10, 4) : 32);
      break;
    case 0x28: /* READ(10) */
      wants_in = true;
      if (cdblen < 10) goto illegal;
      exec_read(m, rdbe(cdb + 2, 4), rdbe(cdb + 7, 2));
      break;
    case 0x88: /* READ(16) */
      wants_in = true;
      if (cdblen < 16) goto illegal;
      exec_read(m, rdbe(cdb + 2, 8), rdbe(cdb + 10, 4));
      break;
    case 0x2A: /* WRITE(10) */
    case 0x8A: { /* WRITE(16) */
      wants_out = true;
      m->s.writes++;
      if (m->expect > m->s.max_write_xfer) m->s.max_write_xfer = m->expect;
      if (!m->rw || m->wp) {
        check_condition(m, 0x07, 0x27, 0x00); /* protégé en écriture : rien n'est écrit */
        break;
      }
      if (media_fault(m)) break;
      bool c16 = cdb[0] == 0x8A;
      if (cdblen < (c16 ? 16 : 10)) goto illegal;
      uint64_t lba = c16 ? rdbe(cdb + 2, 8) : rdbe(cdb + 2, 4);
      uint64_t count = c16 ? rdbe(cdb + 10, 4) : rdbe(cdb + 7, 2);
      if (lba > m->blocks || count > m->blocks - lba) {
        check_condition(m, 0x05, 0x21, 0x00);
        break;
      }
      if (count * m->bs != m->expect) { /* Ho ≠ Do : erreur de phase */
        m->status = 2;
        break;
      }
      m->w_pending = true;
      m->w_off = lba * m->bs;
      m->w_bytes = count * m->bs;
      break;
    }
    case 0x1A: /* MODE SENSE(6) */
      wants_in = true;
      if (m->ms_illegal) goto illegal;
      r[0] = 3;                               /* longueur des données qui suivent */
      r[2] = (!m->rw || m->wp_ms) ? 0x80 : 0; /* bit WP */
      respond(m, r, 4);
      break;
    case 0x35: /* SYNCHRONIZE CACHE(10) */
      if (m->sync_illegal) goto illegal;
      /* l'image n'a pas de cache à vider : seul le compte (s.op[0x35]) importe */
      break;
    case 0x1E: /* PREVENT ALLOW MEDIUM REMOVAL */
      break;
    case 0x1B: /* START STOP UNIT */
      memcpy(m->s.start_stop, cdb, cdblen < 6 ? cdblen : 6);
      if ((cdb[4] & 0x03) == 0x01) m->need_start = false; /* START=1, LOEJ=0 */
      break;
    default:
    illegal:
      check_condition(m, 0x05, 0x20, 0x00);
      break;
  }
  /* Sens de transfert incohérent avec la commande : erreur de phase. */
  if (m->expect && ((wants_in && !dir_in) || (wants_out && dir_in))) {
    m->status = 2;
    m->data_len = 0;
  }
}

static int fake_bulk_out(void *ctx, const void *buf, size_t len, unsigned timeout_ms) {
  fake_msc *m = ctx;
  enter(m);
  int rc;
  if (m->failing) {
    rc = m->fail_err;
  } else if (m->need_reset || m->halt_out) {
    rc = FL_ESTALL;
  } else if (m->st == ST_CBW) {
    const uint8_t *c = buf;
    if (len != 31 || rd32le(c) != CBW_SIG || (c[13] & 0xF0) || c[14] < 1 || c[14] > 16) {
      /* CBW invalide : blocage des deux points d'accès jusqu'à la reprise. */
      m->need_reset = m->halt_in = m->halt_out = true;
      rc = (int)len;
    } else if (m->fail_armed && m->fail_left == 0) {
      m->failing = true;
      rc = m->fail_err;
    } else if (m->fail_write && (c[15] == 0x2A || c[15] == 0x8A) && --m->fail_write == 0) {
      m->failing = true; /* débranché au n-ième WRITE : rien n'est reçu */
      m->fail_err = m->fail_write_err;
      rc = m->fail_err;
    } else {
      if (m->fail_armed) m->fail_left--;
      m->s.commands++;
      m->op = c[15];
      m->w_pending = false;
      m->s.tmo_cbw[m->op] = timeout_ms;
      if (m->zlp_csw) {
        m->zlp_csw--;
        m->zlp_pending = true;
      }
      m->tag = rd32le(c + 4);
      m->expect = rd32le(c + 8);
      bool dir_in = (c[12] & 0x80) != 0;
      m->data_pos = 0;
      if (m->expect > MAX_DATA) {
        m->status = 2;
        m->data_len = 0;
      } else {
        exec_cdb(m, c + 15, c[14], dir_in);
      }
      m->stall_this = false;
      if (m->expect == 0 || m->status == 2) {
        m->st = ST_CSW;
      } else if (dir_in) {
        m->st = ST_DATA_IN;
        if (m->stall_data && m->status == 0 && m->data_len > 0) {
          m->stall_data--;
          m->stall_this = true;
        }
      } else {
        m->st = ST_DATA_OUT;
      }
      rc = 31;
    }
  } else if (m->st == ST_DATA_OUT) {
    m->s.tmo_data[m->op] = timeout_ms;
    uint32_t left = m->expect - m->data_pos;
    uint32_t n = len < left ? (uint32_t)len : left;
    if (m->w_pending) memcpy(m->data + m->data_pos, buf, n);
    m->data_pos += n; /* sans WRITE accepté : absorbé, jamais écrit */
    if (m->data_pos == m->expect || n < len) m->st = ST_CSW;
    if (m->w_pending && m->data_pos == m->expect) {
      m->w_pending = false;
      /* au-delà de l'image (capacité simulée) : accepté, rien n'est gardé */
      uint64_t keep = m->w_off >= m->img_size ? 0 : m->img_size - m->w_off;
      if (keep > m->w_bytes) keep = m->w_bytes;
      if (keep && pwrite(m->fd, m->data, (size_t)keep, (off_t)m->w_off) != (ssize_t)keep)
        check_condition(m, 0x03, 0x0C, 0x00);
      else
        m->s.written_bytes += m->w_bytes;
    }
    rc = (int)n;
  } else {
    m->halt_out = true;
    rc = FL_ESTALL;
  }
  leave(m);
  return rc;
}

static int fake_bulk_in(void *ctx, void *buf, size_t len, unsigned timeout_ms) {
  fake_msc *m = ctx;
  enter(m);
  int rc;
  if (m->failing) {
    rc = m->fail_err;
  } else if (m->need_reset || m->halt_in) {
    rc = FL_ESTALL;
  } else if (m->st == ST_DATA_IN) {
    m->s.tmo_data[m->op] = timeout_ms;
    if (m->stall_this) {
      m->stall_this = false;
      m->halt_in = true; /* rien envoyé : le CSW annoncera un résidu complet */
      m->st = ST_CSW;
      rc = FL_ESTALL;
    } else {
      uint32_t left = m->data_len - m->data_pos;
      uint32_t n = len < left ? (uint32_t)len : left;
      memcpy(buf, m->data + m->data_pos, n);
      m->data_pos += n;
      if (n < len || m->data_pos == m->data_len) m->st = ST_CSW;
      rc = (int)n;
    }
  } else if (m->st == ST_CSW) {
    m->s.tmo_csw[m->op] = timeout_ms;
    if (m->zlp_pending) {
      m->zlp_pending = false; /* paquet de longueur nulle : le CSW suit */
      m->s.zlps++;
      rc = 0;
    } else if (len < 13) {
      rc = FL_EIO; /* débordement */
    } else {
      uint8_t *c = buf;
      wr32le(c, m->bad_csw ? 0xDEADBEEFu : CSW_SIG);
      if (m->bad_csw) m->bad_csw--;
      wr32le(c + 4, m->tag);
      wr32le(c + 8, m->bogus_on && m->expect ? m->bogus_residue : m->expect - m->data_pos);
      c[12] = m->status;
      m->st = ST_CBW;
      rc = 13;
    }
  } else {
    m->halt_in = true;
    rc = FL_ESTALL;
  }
  leave(m);
  return rc;
}

static int fake_clear_halt(void *ctx, bool in_endpoint) {
  fake_msc *m = ctx;
  enter(m);
  int rc = FL_OK;
  if (m->failing) {
    rc = m->fail_err;
  } else {
    m->s.clear_halts++;
    if (!m->need_reset) {
      if (in_endpoint)
        m->halt_in = false;
      else
        m->halt_out = false;
    }
  }
  leave(m);
  return rc;
}

static int fake_bot_reset(void *ctx) {
  fake_msc *m = ctx;
  enter(m);
  int rc = FL_OK;
  if (m->failing) {
    rc = m->fail_err;
  } else {
    m->s.resets++;
    m->need_reset = m->halt_in = m->halt_out = m->stall_this = m->zlp_pending = false;
    m->st = ST_CBW;
    m->data_len = m->data_pos = m->expect = 0;
  }
  leave(m);
  return rc;
}

static int nouveau(const char *image_path, bool rw, fake_msc **out) {
  if (!image_path || !out) return FL_EINVAL;
  *out = NULL;
  fake_msc *m = calloc(1, sizeof *m);
  if (!m) return FL_ENOMEM;
  m->data = malloc(MAX_DATA);
  m->rw = rw;
  m->fd = open(image_path, (rw ? O_RDWR : O_RDONLY) | O_CLOEXEC);
  struct stat st;
  if (!m->data || m->fd < 0 || fstat(m->fd, &st) != 0 || st.st_size <= 0) {
    if (m->fd >= 0) close(m->fd);
    free(m->data);
    free(m);
    return FL_EIO;
  }
  m->img_size = (uint64_t)st.st_size;
  m->bs = 512;
  m->blocks = m->img_size / 512;
  m->inquiry_len = 36;
  m->st = ST_CBW;
  atomic_init(&m->busy, 0);
  m->t.ctx = m;
  m->t.bulk_out = fake_bulk_out;
  m->t.bulk_in = fake_bulk_in;
  m->t.clear_halt = fake_clear_halt;
  m->t.bot_reset = fake_bot_reset;
  *out = m;
  return FL_OK;
}

int fake_msc_new(const char *image_path, fake_msc **out) { return nouveau(image_path, false, out); }
int fake_msc_new_rw(const char *image_path, fake_msc **out) { return nouveau(image_path, true, out); }

void fake_msc_free(fake_msc *m) {
  if (!m) return;
  close(m->fd);
  free(m->data);
  free(m);
}

const fl_usb_transport *fake_msc_transport(fake_msc *m) { return &m->t; }
fake_msc_stats *fake_msc_stats_get(fake_msc *m) { return &m->s; }
void fake_msc_reset_stats(fake_msc *m) { memset(&m->s, 0, sizeof m->s); }

void fake_msc_fail_after(fake_msc *m, unsigned n, int err) {
  m->fail_armed = true;
  m->fail_left = n;
  m->fail_err = err;
}
void fake_msc_stall_next_data(fake_msc *m) { m->stall_data++; }
void fake_msc_bad_csw_next(fake_msc *m) { m->bad_csw++; }
void fake_msc_not_ready(fake_msc *m, unsigned k) { fake_msc_not_ready_sense(m, k, 0x04, 0x01); }
void fake_msc_not_ready_sense(fake_msc *m, unsigned k, uint8_t asc, uint8_t ascq) {
  m->not_ready = k;
  m->nr_asc = asc;
  m->nr_ascq = ascq;
}
void fake_msc_require_start(fake_msc *m) { m->need_start = true; }
void fake_msc_zlp_before_csw(fake_msc *m, unsigned k) { m->zlp_csw = k; }
void fake_msc_bogus_residue(fake_msc *m, uint32_t residue) {
  m->bogus_on = true;
  m->bogus_residue = residue;
}
void fake_msc_sense_next(fake_msc *m, uint8_t key, uint8_t asc, uint8_t ascq) {
  m->sense_armed = true;
  m->next_key = key;
  m->next_asc = asc;
  m->next_ascq = ascq;
}
int fake_msc_set_block_size(fake_msc *m, uint32_t bs) {
  if (bs == 0) return FL_EINVAL;
  m->bs = bs;
  if (!m->cap_set) m->blocks = m->img_size / bs;
  return FL_OK;
}
void fake_msc_set_capacity(fake_msc *m, uint64_t blocks) {
  m->cap_set = true;
  m->blocks = blocks;
}
void fake_msc_force_rc16(fake_msc *m) { m->force_rc16 = true; }
void fake_msc_set_inquiry_len(fake_msc *m, uint32_t n) { m->inquiry_len = n > 36 ? 36 : n; }
void fake_msc_set_peripheral(fake_msc *m, uint8_t b0) { m->peripheral = b0; }
void fake_msc_set_write_protect(fake_msc *m, bool on, bool wp_mode_sense) {
  m->wp = on;
  m->wp_ms = on && wp_mode_sense;
}
void fake_msc_mode_sense_unsupported(fake_msc *m) { m->ms_illegal = true; }
void fake_msc_sync_unsupported(fake_msc *m) { m->sync_illegal = true; }
void fake_msc_fail_at_write(fake_msc *m, unsigned n, int err) {
  m->fail_write = n;
  m->fail_write_err = err;
}
