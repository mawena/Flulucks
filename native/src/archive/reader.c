#define _GNU_SOURCE
/* Lecture d'archives sur libarchive (phase 9). Formats : ZIP (répertoire central, accès aléatoire), 7z, RAR4,
 * RAR5, TAR, et « brut » derrière un filtre gzip, xz ou lzma (un .gz ou un .xz seul). Les noms sont rendus
 * octets pour octets ; libarchive est appelé dans une locale UTF-8 propre au fil (sous « C », il refuse les
 * noms UTF-8 d'un ZIP). Une erreur de la source (fl_arc_in) est rendue telle quelle. */
#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <locale.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "archive/arc_internal.h"
#include "fluluks/archive.h"
#include "fluluks/error.h"

#define BUF (64 * 1024)

struct fl_arc_reader {
  struct archive *a;
  const fl_arc_in *in;
  uint64_t pos;
  int io_err;               /* dernière erreur de la source, 0 sinon */
  bool pending;             /* premier en-tête lu par fl_arc_open, pas encore rendu */
  int pending_rc;           /* son code (ARCHIVE_OK, ARCHIVE_EOF…) */
  struct archive_entry *pending_e;
  bool encrypted;           /* entrée courante chiffrée */
  uint8_t buf[BUF];
};

/* ---- locale UTF-8 du fil, le temps d'un appel à libarchive (lecteur et écrivain) ---- */

static locale_t g_utf8;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static void init_locale(void) {
  g_utf8 = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0);
  if (!g_utf8) g_utf8 = newlocale(LC_CTYPE_MASK, "en_US.UTF-8", (locale_t)0);
}

locale_t fl_arc_utf8_begin(void) {
  pthread_once(&g_once, init_locale);
  return g_utf8 ? uselocale(g_utf8) : (locale_t)0;
}

void fl_arc_utf8_end(locale_t old) {
  if (g_utf8) uselocale(old);
}

/* ---- rappels de libarchive sur fl_arc_in ---- */

static la_ssize_t cb_read(struct archive *a, void *data, const void **out) {
  fl_arc_reader *r = data;
  *out = r->buf;
  if (r->in->size >= 0 && r->pos >= (uint64_t)r->in->size) return 0;
  int64_t n = r->in->pread(r->in->ctx, r->buf, BUF, r->pos);
  if (n < 0) {
    r->io_err = (int)n;
    archive_set_error(a, EIO, "lecture de la source : %d", (int)n);
    return -1;
  }
  r->pos += (uint64_t)n;
  return (la_ssize_t)n;
}

static la_int64_t cb_seek(struct archive *a, void *data, la_int64_t off, int whence) {
  fl_arc_reader *r = data;
  int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (int64_t)r->pos : r->in->size;
  int64_t to = base + off;
  if (to < 0 || to > r->in->size) {
    archive_set_error(a, EINVAL, "position hors de l'archive");
    return ARCHIVE_FATAL;
  }
  r->pos = (uint64_t)to;
  return to;
}

static la_int64_t cb_skip(struct archive *a, void *data, la_int64_t req) {
  (void)a;
  fl_arc_reader *r = data;
  int64_t rest = r->in->size - (int64_t)r->pos;
  int64_t n = req < rest ? req : rest;
  if (n < 0) n = 0;
  r->pos += (uint64_t)n;
  return n;
}

/* Code d'une erreur de libarchive : celle de la source s'il y en a une, sinon archive abîmée. */
static int err_code(fl_arc_reader *r) { return r->io_err ? r->io_err : FL_ECORRUPT; }

/* ---- API ---- */

int fl_arc_open(const fl_arc_in *in, fl_arc_reader **out) {
  if (!in || !in->pread || in->size < 0 || !out) return FL_EINVAL;
  *out = NULL;
  fl_arc_reader *r = calloc(1, sizeof *r);
  if (!r) return FL_ENOMEM;
  r->in = in;
  r->a = archive_read_new();
  if (!r->a) {
    free(r);
    return FL_ENOMEM;
  }
  struct archive *a = r->a;
  archive_read_support_filter_gzip(a);
  archive_read_support_filter_xz(a);
  archive_read_support_filter_lzma(a);
  archive_read_support_format_zip_seekable(a);
  archive_read_support_format_7zip(a);
  archive_read_support_format_rar(a);
  archive_read_support_format_rar5(a);
  archive_read_support_format_tar(a);
  archive_read_support_format_raw(a);
  archive_read_set_callback_data(a, r);
  archive_read_set_read_callback(a, cb_read);
  archive_read_set_seek_callback(a, cb_seek);
  archive_read_set_skip_callback(a, cb_skip);
  locale_t old = fl_arc_utf8_begin();
  int rc = archive_read_open1(a);
  if (rc == ARCHIVE_OK) {
    r->pending_rc = archive_read_next_header(a, &r->pending_e);
    r->pending = true;
    rc = r->pending_rc;
  }
  fl_arc_utf8_end(old);
  int code = FL_OK;
  if (rc < ARCHIVE_WARN && rc != ARCHIVE_EOF) {
    code = r->io_err ? r->io_err : FL_ENOTSUP; /* aucun format reconnu, ou en-tête illisible */
  } else if (archive_format(a) == ARCHIVE_FORMAT_RAW && archive_filter_count(a) <= 1) {
    code = FL_ENOTSUP; /* « brut » sans filtre de compression : pas une archive */
  }
  if (code != FL_OK) {
    archive_read_free(a);
    free(r);
    return code;
  }
  *out = r;
  return FL_OK;
}

static fl_arc_type type_of(struct archive_entry *e) {
  if (archive_entry_hardlink(e)) return FL_ARC_HARDLINK;
  switch (archive_entry_filetype(e)) {
    case AE_IFREG: return FL_ARC_FILE;
    case AE_IFDIR: return FL_ARC_DIR;
    case AE_IFLNK: return FL_ARC_SYMLINK;
    default: return FL_ARC_OTHER;
  }
}

int fl_arc_next(fl_arc_reader *r, fl_arc_entry *e) {
  if (!r || !e) return FL_EINVAL;
  memset(e, 0, sizeof *e);
  locale_t old = fl_arc_utf8_begin();
  struct archive_entry *ae = NULL;
  int rc;
  if (r->pending) {
    r->pending = false;
    rc = r->pending_rc;
    ae = r->pending_e;
  } else {
    rc = archive_read_next_header(r->a, &ae);
  }
  int out;
  if (rc == ARCHIVE_EOF) {
    out = 0;
  } else if (rc < ARCHIVE_WARN) {
    out = err_code(r);
  } else {
    const char *n = archive_entry_pathname(ae);
    size_t len = n ? strlen(n) : 0;
    e->type = type_of(ae);
    if (len == 0 || len >= sizeof e->name) {
      e->type = FL_ARC_OTHER; /* nom absent ou trop long : l'appelant l'ignore */
    } else {
      memcpy(e->name, n, len + 1);
      e->name_len = len;
    }
    e->size = archive_entry_size_is_set(ae) ? archive_entry_size(ae) : -1;
    e->mtime = archive_entry_mtime_is_set(ae) ? (int64_t)archive_entry_mtime(ae) : 0;
    e->encrypted = archive_entry_is_encrypted(ae) != 0;
    r->encrypted = e->encrypted;
    out = 1;
  }
  fl_arc_utf8_end(old);
  return out;
}

int64_t fl_arc_read(fl_arc_reader *r, void *buf, size_t len) {
  if (!r || (!buf && len)) return FL_EINVAL;
  if (r->encrypted) return FL_ENOTSUP;
  if (len == 0) return 0;
  la_ssize_t n = archive_read_data(r->a, buf, len);
  if (n >= 0) return n;
  return err_code(r);
}

const char *fl_arc_format(const fl_arc_reader *r) {
  return r ? archive_format_name(r->a) : "";
}

void fl_arc_close(fl_arc_reader *r) {
  if (!r) return;
  archive_read_free(r->a);
  free(r);
}
