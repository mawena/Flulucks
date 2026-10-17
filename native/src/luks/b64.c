#include "fluluks/error.h"
#include "luks/luks_internal.h"

static int val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

/* Base64 standard strict : alphabet standard, remplissage '=' obligatoire, aucun espace. */
int fl_b64_decode(const char *in, uint8_t *out, size_t outmax, size_t *outlen) {
  if (!in || !out || !outlen) return FL_EINVAL;
  size_t n = 0;
  while (in[n]) n++;
  if (n % 4 != 0) return FL_ECORRUPT;
  size_t o = 0;
  for (size_t i = 0; i < n; i += 4) {
    int v[4];
    int pad = 0;
    for (int j = 0; j < 4; j++) {
      char c = in[i + j];
      if (c == '=') {
        /* le remplissage n'est permis que dans le dernier quartet, en fin de quartet */
        if (i + 4 != n || j < 2) return FL_ECORRUPT;
        pad++;
        v[j] = 0;
      } else {
        if (pad) return FL_ECORRUPT;
        v[j] = val(c);
        if (v[j] < 0) return FL_ECORRUPT;
      }
    }
    uint32_t w = ((uint32_t)v[0] << 18) | ((uint32_t)v[1] << 12) | ((uint32_t)v[2] << 6) | (uint32_t)v[3];
    int nb = 3 - pad;
    if (o + (size_t)nb > outmax) return FL_ECORRUPT;
    out[o++] = (uint8_t)(w >> 16);
    if (nb > 1) out[o++] = (uint8_t)(w >> 8);
    if (nb > 2) out[o++] = (uint8_t)w;
  }
  *outlen = o;
  return FL_OK;
}
