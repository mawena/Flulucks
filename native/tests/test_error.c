#include "fluluks/error.h"
#include "fluluks/secure.h"
#include "tt.h"

#include <string.h>

static void test_strerror(void) {
  TT_ASSERT_STR_EQ(fl_strerror(FL_EBADPASS), "phrase de passe incorrecte");
  TT_ASSERT_STR_EQ(fl_strerror(12345), "erreur inconnue");
  TT_ASSERT_STR_EQ(fl_strerror(FL_OK), "succès");
  TT_ASSERT_STR_EQ(fl_strerror(FL_ENOSPC), "plus d'espace disponible");
  TT_ASSERT_STR_EQ(fl_strerror(FL_ENODEV), "disque débranché");
  TT_ASSERT_STR_EQ(fl_strerror(FL_ETIMEDOUT), "le disque ne répond pas");
  TT_ASSERT_STR_EQ(fl_strerror(FL_ESTALL), "transfert USB bloqué");
  TT_ASSERT_STR_EQ(fl_strerror(FL_EBUSY), "volume occupé");
}

static void test_secure(void) {
  unsigned char *k = fl_secure_alloc(64);
  TT_ASSERT(k != NULL);
  for (int i = 0; i < 64; i++) TT_ASSERT_EQ(k[i], 0);
  fl_secure_free(k, 64);
  fl_secure_free(NULL, 0);
}

static void test_memzero(void) {
  unsigned char b[64];
  memset(b, 0xAA, sizeof b);
  fl_memzero(b, sizeof b);
  for (size_t i = 0; i < sizeof b; i++) TT_ASSERT_EQ(b[i], 0);
  fl_memzero(NULL, 0);
}

int main(void) {
  TT_RUN(test_strerror);
  TT_RUN(test_secure);
  TT_RUN(test_memzero);
  TT_MAIN_END();
}
