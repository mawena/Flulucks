#define _GNU_SOURCE
/* crc32c rapide de lwext4 (phase 3, T7, correctif 0006) : résultat identique au bit près à
 * l'algorithme d'origine (octet par octet), pour toute longueur, tout alignement et toute valeur
 * initiale, par le chemin choisi à l'exécution (instruction matérielle si le processeur l'a) comme
 * par le chemin portable par tranches de 8.
 *
 * « test_crc32c debit » (test CTest test_crc32c_debit, label lent) affiche les débits, sans seuil. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ext4_crc32.h"
#include "tt.h"

/* L'algorithme d'origine de lwext4 (table de 256 entrées, un octet à la fois), table recalculée. */
static uint32_t ref_tab[256];

static void ref_init(void) {
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0x82F63B78u : c >> 1;
    ref_tab[i] = c;
  }
}

static uint32_t ref_crc32c(uint32_t crc, const void *buf, uint32_t size) {
  const uint8_t *p = buf;
  while (size--) crc = ref_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
  return crc;
}

/* crc32c standard (init et xor final), comme le font les appelants de lwext4. */
static uint32_t std_crc(uint32_t (*f)(uint32_t, const void *, uint32_t), const void *b, uint32_t n) {
  return ~f(0xFFFFFFFFu, b, n);
}

static void test_crc32c_vecteurs(void) {
  uint8_t z[32], ff[32], inc[32];
  memset(z, 0, sizeof z);
  memset(ff, 0xFF, sizeof ff);
  for (int i = 0; i < 32; i++) inc[i] = (uint8_t)i;
  uint32_t (*const fs[])(uint32_t, const void *, uint32_t) = {ext4_crc32c, ext4_crc32c_sw, ref_crc32c};
  for (size_t i = 0; i < sizeof fs / sizeof *fs; i++) {
    TT_ASSERT_EQ(std_crc(fs[i], "123456789", 9), 0xE3069283u);
    /* RFC 3720, B.4 */
    TT_ASSERT_EQ(std_crc(fs[i], z, sizeof z), 0x8A9136AAu);
    TT_ASSERT_EQ(std_crc(fs[i], ff, sizeof ff), 0x62A8AB43u);
    TT_ASSERT_EQ(std_crc(fs[i], inc, sizeof inc), 0x46DD794Eu);
    TT_ASSERT_EQ(fs[i](0x12345678u, "", 0), 0x12345678u);
  }
  fprintf(stderr, "crc32c : chemin « %s »\n", ext4_crc32c_impl());
}

static uint64_t g_x = 0x2545F4914F6CDD1Dull;
static uint64_t alea(void) {
  g_x ^= g_x << 13;
  g_x ^= g_x >> 7;
  g_x ^= g_x << 17;
  return g_x;
}

static void test_crc32c_equivalence(void) {
  enum { MAXLEN = 9000 };
  uint8_t *buf = malloc(MAXLEN + 16);
  TT_ASSERT(buf != NULL);
  int ecarts = 0;
  for (int k = 0; k < 10000; k++) {
    uint32_t len = (uint32_t)(alea() % (MAXLEN + 1)), off = (uint32_t)(alea() % 8);
    uint32_t init = (uint32_t)alea();
    for (uint32_t i = 0; i < len; i++) buf[off + i] = (uint8_t)alea();
    uint32_t r = ref_crc32c(init, buf + off, len);
    if (ext4_crc32c(init, buf + off, len) != r || ext4_crc32c_sw(init, buf + off, len) != r) {
      if (ecarts++ < 5) fprintf(stderr, "écart : longueur %u, décalage %u\n", len, off);
    }
  }
  /* découpage : la somme en plusieurs appels est celle d'un seul */
  for (uint32_t i = 0; i < 4096; i++) buf[i] = (uint8_t)alea();
  uint32_t tout = ext4_crc32c(7, buf, 4096);
  for (uint32_t cut = 0; cut <= 4096; cut += 37)
    if (ext4_crc32c(ext4_crc32c(7, buf, cut), buf + cut, 4096 - cut) != tout) ecarts++;
  free(buf);
  TT_ASSERT_EQ(ecarts, 0);
}

static double mo_par_s(uint32_t (*f)(uint32_t, const void *, uint32_t), const uint8_t *b, size_t n, int tours) {
  struct timespec t0, t1;
  volatile uint32_t sink = 0;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (int i = 0; i < tours; i++) sink ^= f(sink, b, (uint32_t)n);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  double s = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
  return (double)n * tours / 1e6 / (s > 0 ? s : 1e-9);
}

static void test_crc32c_debit(void) {
  size_t n = 4096;
  uint8_t *b = malloc(n);
  TT_ASSERT(b != NULL);
  for (size_t i = 0; i < n; i++) b[i] = (uint8_t)alea();
  printf("crc32c sur blocs de 4 Kio (chemin « %s ») :\n", ext4_crc32c_impl());
  printf("  ext4_crc32c    : %8.0f Mo/s\n", mo_par_s(ext4_crc32c, b, n, 100000));
  printf("  tranches de 8  : %8.0f Mo/s\n", mo_par_s(ext4_crc32c_sw, b, n, 100000));
  printf("  octet à octet  : %8.0f Mo/s\n", mo_par_s(ref_crc32c, b, n, 20000));
  free(b);
}

int main(int argc, char **argv) {
  ref_init();
  if (argc > 1 && !strcmp(argv[1], "debit")) {
    TT_RUN(test_crc32c_debit);
    TT_MAIN_END();
  }
  TT_RUN(test_crc32c_vecteurs);
  TT_RUN(test_crc32c_equivalence);
  TT_MAIN_END();
}
