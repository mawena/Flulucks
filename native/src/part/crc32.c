#include "part/crc32.h"

static uint32_t g_table[256];
static int g_ready;

static void init_table(void) {
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    g_table[i] = c;
  }
  g_ready = 1;
}

uint32_t fl_crc32(const void *data, size_t len) {
  const uint8_t *p = data;
  uint32_t c = 0xFFFFFFFFu;
  if (!g_ready) init_table(); /* idempotent : même contenu écrit par tout appelant concurrent */
  for (size_t i = 0; i < len; i++) c = g_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}
