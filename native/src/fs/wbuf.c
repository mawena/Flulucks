#include "fs/wbuf.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "fluluks/error.h"

void fl_wbuf_free(fl_wbuf *b) {
  free(b->data);
  b->data = NULL;
  b->len = 0;
  b->off = 0;
}

int fl_wbuf_flush(fl_wbuf *b, fl_wbuf_flush_fn fn, void *ctx) {
  if (!b->len) {
    fl_wbuf_free(b);
    return FL_OK;
  }
  int rc = fn(ctx, b->data, b->len, b->off);
  fl_wbuf_free(b);
  if (rc != FL_OK && b->err == FL_OK) b->err = rc;
  return rc;
}

uint64_t fl_wbuf_end(const fl_wbuf *b) { return b->len ? b->off + b->len : 0; }

int fl_wbuf_write(fl_wbuf *b, const void *p, size_t len, uint64_t off, fl_wbuf_flush_fn fn, void *ctx) {
  if (b->err != FL_OK) return b->err;
  if (len == 0) return FL_OK;
  /* prolonge exactement le tampon et y tient : copie ; sinon vidage d'abord. b->len ≤ cap et
   * len < cap ici : pas de débordement. */
  bool suite = b->len && len < FL_WBUF_CAP && off == b->off + b->len && b->len + len <= FL_WBUF_CAP;
  if (b->len && !suite) {
    int rc = fl_wbuf_flush(b, fn, ctx);
    if (rc != FL_OK) return rc;
  }
  if (len >= FL_WBUF_CAP) return fn(ctx, p, len, off);
  if (!b->len) {
    if (!b->data && !(b->data = malloc(FL_WBUF_CAP))) return fn(ctx, p, len, off);
    b->off = off;
  }
  memcpy(b->data + b->len, p, len);
  b->len += len;
  return FL_OK;
}
