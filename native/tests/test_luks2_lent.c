#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/luks.h"
#include "luks/luks_internal.h"
#include "tt.h"
#include "luks_rt.h"

/* Disque aux paramètres exacts du disque réel : argon2id t=7 m=1 Gio p=4. */
static void test_luks2_parametres_exacts(void) {
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(tt_fixture("whole-luks2-exact.img"), 512, true, &d), FL_OK);
  char pass[256];
  FILE *f = fopen(tt_fixture("pass.txt"), "rb");
  TT_ASSERT(f != NULL);
  size_t n = fread(pass, 1, sizeof pass, f);
  fclose(f);
  fl_luks *l = NULL;
  TT_ASSERT_EQ(fl_luks_open(d, &l), FL_OK);
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  fl_bdev *dec = NULL;
  int rc = fl_luks_unlock(l, pass, n, &dec);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  printf("test_luks2_parametres_exacts : deverrouillage en %.2f s\n",
         (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9);
  fflush(stdout);
  TT_ASSERT_EQ(rc, FL_OK);
  uint8_t b[4096];
  TT_ASSERT_EQ(fl_bdev_read(dec, 0, 1, b), FL_OK);
  fl_bdev_close(dec);
  fl_luks_close(l);
  fl_bdev_close(d);
}

/* Aller-retour d'écriture aux paramètres Argon2 exacts du vrai disque (copie sous le dossier des fixtures). */
static void test_luks2_ecriture_aller_retour_exact(void) {
  char copie[4096];
  snprintf(copie, sizeof copie, "%s", tt_fixture("tmp-ecriture-exact.img"));
  aller_retour("whole-luks2-exact.img", copie);
  unlink(copie); /* même en cas d'échec */
}

int main(void) {
  TT_RUN(test_luks2_ecriture_aller_retour_exact);
  TT_RUN(test_luks2_parametres_exacts);
  TT_MAIN_END();
}
