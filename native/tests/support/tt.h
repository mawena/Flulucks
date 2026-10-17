#ifndef FL_TT_H
#define FL_TT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tt_failures = 0;

#define TT_ASSERT(cond)                                                      \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "%s:%d: ECHEC: %s\n", __FILE__, __LINE__, #cond);      \
      tt_failures++;                                                         \
      return;                                                                \
    }                                                                        \
  } while (0)

#define TT_ASSERT_EQ(a, b)                                                   \
  do {                                                                       \
    long long tt_a_ = (long long)(a), tt_b_ = (long long)(b);                \
    if (tt_a_ != tt_b_) {                                                    \
      fprintf(stderr, "%s:%d: ECHEC: %s == %s (%lld != %lld)\n", __FILE__,   \
              __LINE__, #a, #b, tt_a_, tt_b_);                               \
      tt_failures++;                                                         \
      return;                                                                \
    }                                                                        \
  } while (0)

#define TT_ASSERT_STR_EQ(a, b)                                               \
  do {                                                                       \
    const char *tt_a_ = (a), *tt_b_ = (b);                                   \
    if (!tt_a_ || !tt_b_ || strcmp(tt_a_, tt_b_) != 0) {                     \
      fprintf(stderr, "%s:%d: ECHEC: %s == %s (\"%s\" != \"%s\")\n",         \
              __FILE__, __LINE__, #a, #b, tt_a_ ? tt_a_ : "(null)",         \
              tt_b_ ? tt_b_ : "(null)");                                     \
      tt_failures++;                                                         \
      return;                                                                \
    }                                                                        \
  } while (0)

#define TT_ASSERT_MEM_EQ(a, b, n)                                            \
  do {                                                                       \
    if (memcmp((a), (b), (n)) != 0) {                                        \
      fprintf(stderr, "%s:%d: ECHEC: memoire %s != %s (%zu octets)\n",       \
              __FILE__, __LINE__, #a, #b, (size_t)(n));                      \
      tt_failures++;                                                         \
      return;                                                                \
    }                                                                        \
  } while (0)

#define TT_RUN(fn)                                                           \
  do {                                                                       \
    int tt_before_ = tt_failures;                                            \
    fn();                                                                    \
    fprintf(stderr, "[%s] %s\n", tt_failures == tt_before_ ? "OK" : "KO", #fn); \
  } while (0)

#define TT_MAIN_END() return tt_failures ? 1 : 0

/* Chemin d'une fixture : $FL_FIXTURES/<nom> (tampon statique). */
static inline const char *tt_fixture(const char *nom) {
  static char buf[4096];
  const char *dir = getenv("FL_FIXTURES");
  snprintf(buf, sizeof buf, "%s/%s", dir ? dir : ".", nom);
  return buf;
}

#endif
