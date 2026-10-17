/* Écriture JSON minimale (voir json_out.h). Sans jni.h : compilé aussi dans les tests PC. */
#include "json_out.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fluluks/error.h"

void fl_json_init(fl_json *j) { memset(j, 0, sizeof *j); }

void fl_json_free(fl_json *j) {
  free(j->buf);
  memset(j, 0, sizeof *j);
}

void fl_json_reset(fl_json *j) {
  j->len = 0;
  if (j->buf) j->buf[0] = 0;
  j->err = FL_OK;
  j->comma = false;
}

/* Réserve n octets (+ NUL final). */
static bool reserve(fl_json *j, size_t n) {
  if (j->err) return false;
  if (n > FL_JSON_MAX || j->len > FL_JSON_MAX - n) {
    j->err = FL_ENOMEM;
    return false;
  }
  size_t need = j->len + n + 1;
  if (need <= j->cap) return true;
  size_t cap = j->cap ? j->cap : 256;
  while (cap < need) cap = cap > (SIZE_MAX / 2) ? need : cap * 2;
  char *b = realloc(j->buf, cap);
  if (!b) {
    j->err = FL_ENOMEM;
    return false;
  }
  j->buf = b;
  j->cap = cap;
  return true;
}

static void put(fl_json *j, const char *s, size_t n) {
  if (!reserve(j, n)) return;
  memcpy(j->buf + j->len, s, n);
  j->len += n;
  j->buf[j->len] = 0;
}

static void putc_(fl_json *j, char c) { put(j, &c, 1); }

/* Avant un élément (valeur ou clé). */
static void pre(fl_json *j) {
  if (j->comma) putc_(j, ',');
  j->comma = false;
}

void fl_json_obj_begin(fl_json *j) {
  pre(j);
  putc_(j, '{');
}

void fl_json_obj_end(fl_json *j) {
  putc_(j, '}');
  j->comma = true;
}

void fl_json_arr_begin(fl_json *j) {
  pre(j);
  putc_(j, '[');
}

void fl_json_arr_end(fl_json *j) {
  putc_(j, ']');
  j->comma = true;
}

/* Longueur de la séquence UTF-8 valide qui commence en s[0] (1..4), ou 0 si invalide : dans ce cas
 * *skip reçoit la longueur de la sous-partie maximale à remplacer par un seul U+FFFD. */
static size_t utf8_seq(const unsigned char *s, size_t n, size_t *skip) {
  unsigned char c = s[0];
  size_t len;
  unsigned char lo = 0x80, hi = 0xBF; /* bornes du 2e octet */
  if (c < 0x80) return 1;
  if (c >= 0xC2 && c <= 0xDF) {
    len = 2;
  } else if (c >= 0xE0 && c <= 0xEF) {
    len = 3;
    if (c == 0xE0) lo = 0xA0;      /* pas de forme trop longue */
    else if (c == 0xED) hi = 0x9F; /* pas de surrogate */
  } else if (c >= 0xF0 && c <= 0xF4) {
    len = 4;
    if (c == 0xF0) lo = 0x90;      /* pas de forme trop longue */
    else if (c == 0xF4) hi = 0x8F; /* <= U+10FFFF */
  } else {
    *skip = 1; /* 80..C1, F5..FF */
    return 0;
  }
  size_t i = 1;
  for (; i < len; i++) {
    if (i >= n) break;
    unsigned char b = s[i];
    unsigned char l = i == 1 ? lo : 0x80, h = i == 1 ? hi : 0xBF;
    if (b < l || b > h) break;
  }
  if (i == len) return len;
  *skip = i; /* préfixe valide mais incomplet : un seul U+FFFD */
  return 0;
}

void fl_json_str(fl_json *j, const char *s, size_t n) {
  pre(j);
  putc_(j, '"');
  const unsigned char *p = (const unsigned char *)s;
  size_t i = 0;
  while (i < n && !j->err) {
    /* plage de caractères sans échappement, copiée d'un bloc */
    size_t start = i;
    while (i < n) {
      unsigned char c = p[i];
      if (c < 0x20 || c == '"' || c == '\\') break;
      if (c < 0x80) {
        i++;
        continue;
      }
      size_t skip = 0, l = utf8_seq(p + i, n - i, &skip);
      if (!l) break;
      i += l;
    }
    if (i > start) put(j, (const char *)p + start, i - start);
    if (i >= n) break;
    unsigned char c = p[i];
    if (c == '"' || c == '\\') {
      char e[2] = {'\\', (char)c};
      put(j, e, 2);
      i++;
    } else if (c < 0x20) {
      char e[8];
      snprintf(e, sizeof e, "\\u%04x", (unsigned)c);
      put(j, e, 6);
      i++;
    } else {
      size_t skip = 1;
      (void)utf8_seq(p + i, n - i, &skip);
      put(j, "\xEF\xBF\xBD", 3); /* U+FFFD */
      i += skip;
    }
  }
  putc_(j, '"');
  j->comma = true;
}

void fl_json_cstr(fl_json *j, const char *s) { fl_json_str(j, s ? s : "", s ? strlen(s) : 0); }

void fl_json_key(fl_json *j, const char *k) {
  fl_json_cstr(j, k);
  putc_(j, ':');
  j->comma = false;
}

void fl_json_u64(fl_json *j, uint64_t v) {
  char b[24];
  int n = snprintf(b, sizeof b, "%" PRIu64, v);
  pre(j);
  put(j, b, (size_t)n);
  j->comma = true;
}

void fl_json_i64(fl_json *j, int64_t v) {
  char b[24];
  int n = snprintf(b, sizeof b, "%" PRId64, v);
  pre(j);
  put(j, b, (size_t)n);
  j->comma = true;
}

void fl_json_bool(fl_json *j, bool v) {
  pre(j);
  if (v) put(j, "true", 4);
  else put(j, "false", 5);
  j->comma = true;
}

void fl_json_error(fl_json *j, int code) {
  fl_json_reset(j);
  fl_json_obj_begin(j);
  fl_json_key(j, "err");
  fl_json_i64(j, code);
  fl_json_obj_end(j);
}
