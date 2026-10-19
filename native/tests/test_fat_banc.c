#define _GNU_SOURCE
/* Banc FAT/exFAT (phase 4, T1) : teste le banc lui-même (images, juges fsck.fat / fsck.exfat et
 * fatjuge.py, modèle d'arborescence). Aucun code FAT du moteur ici. Réservé au PC : sauté si un outil
 * manque, et sur Android. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fsck_util.h"
#include "tree_model.h"
#include "tt.h"

static char *temporaires[64];
static size_t ntemp;

static void noter(const char *chemin) {
  if (ntemp < 64) temporaires[ntemp++] = strdup(chemin);
}

static void nettoyer(void) {
  for (size_t i = 0; i < ntemp; i++) {
    unlink(temporaires[i]);
    free(temporaires[i]);
  }
  ntemp = 0;
}

static int copie_de(const char *fixture, char *out, size_t cap) {
  char src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture(fixture));
  if (fu_copy_image(src, out, cap) != 0) return -1;
  noter(out);
  return 0;
}

/* Modèle d'une arborescence source (attributs FAT) ; sans mille/ ni gros.bin si petit (FAT12). */
static tm *modele_arbre(const char *arbre, int petit) {
  tm *m = tm_new();
  if (!m) return NULL;
  char src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture(arbre));
  if (tm_from_dir(m, src) != 0) {
    tm_free(m);
    return NULL;
  }
  tm_set_fat_attrs(m);
  if (petit) {
    char p[64];
    for (int i = 0; i < 1000; i++) {
      snprintf(p, sizeof p, "/mille/f%04d.txt", i);
      if (tm_apply_unlink(m, p) != 0) {
        tm_free(m);
        return NULL;
      }
    }
    if (tm_apply_rmdir(m, "/mille") != 0 || tm_apply_unlink(m, "/gros.bin") != 0) {
      tm_free(m);
      return NULL;
    }
  }
  return m;
}

static int ecrire_octets(const char *img, uint64_t off, const void *p, size_t n) {
  int fd = open(img, O_WRONLY);
  if (fd < 0) return -1;
  ssize_t w = pwrite(fd, p, n, (off_t)off);
  close(fd);
  return w == (ssize_t)n ? 0 : -1;
}

static int lire_octets(const char *img, uint64_t off, void *p, size_t n) {
  int fd = open(img, O_RDONLY);
  if (fd < 0) return -1;
  ssize_t r = pread(fd, p, n, (off_t)off);
  close(fd);
  return r == (ssize_t)n ? 0 : -1;
}

/* fatjuge.py locate : offset du premier cluster de données d'un fichier, ou -1. */
static int64_t localiser(const char *img, const char *chemin) {
  char out[256];
  if (fu_fatjuge(out, sizeof out, "locate", img, chemin) != 0) return -1;
  return strtoll(out, NULL, 10);
}

static void test_banc_fixtures_propres(void) {
  static const char *const imgs[] = {"fat12-arbre.img", "fat16-arbre.img", "fat32-arbre.img", "fat32-4k.img",
                                     "exfat-arbre.img", "fat32-vide.img",  "exfat-vide.img"};
  for (size_t i = 0; i < sizeof imgs / sizeof imgs[0]; i++) {
    char out[8192];
    int rc = fu_fsck_fat_n(tt_fixture(imgs[i]), out, sizeof out);
    if (rc != 0) fprintf(stderr, "%s : fsck = %d\n%s\n", imgs[i], rc, out);
    TT_ASSERT_EQ(rc, 0);
  }
}

static void test_banc_manifeste_arbre(void) {
  static const char *const imgs[][2] = {{"fat16-arbre.img", "tree-fat"},
                                        {"fat32-arbre.img", "tree-fat"},
                                        {"fat32-4k.img", "tree-fat"},
                                        {"exfat-arbre.img", "tree-exfat"}};
  for (size_t i = 0; i < sizeof imgs / sizeof imgs[0]; i++) {
    tm *m = modele_arbre(imgs[i][1], 0);
    TT_ASSERT(m != NULL);
    int d = tm_check_fat_judge(m, tt_fixture(imgs[i][0]));
    tm_free(m);
    if (d != 0) fprintf(stderr, "%s : %d écart(s)\n", imgs[i][0], d);
    TT_ASSERT_EQ(d, 0);
  }
  tm *p = modele_arbre("tree-fat", 1);
  TT_ASSERT(p != NULL);
  int d = tm_check_fat_judge(p, tt_fixture("fat12-arbre.img"));
  tm_free(p);
  TT_ASSERT_EQ(d, 0);
}

static void test_banc_voit_contenu(void) {
  static const char *const imgs[] = {"fat32-arbre.img", "exfat-arbre.img"};
  for (size_t i = 0; i < 2; i++) {
    char copie[4096];
    TT_ASSERT(copie_de(imgs[i], copie, sizeof copie) == 0);
    int64_t off = localiser(copie, "/gros.bin");
    TT_ASSERT(off > 0);
    uint8_t b;
    TT_ASSERT(lire_octets(copie, (uint64_t)off + 1000, &b, 1) == 0);
    b ^= 0xFF;
    TT_ASSERT(ecrire_octets(copie, (uint64_t)off + 1000, &b, 1) == 0);
    tm *m = modele_arbre(i ? "tree-exfat" : "tree-fat", 0);
    TT_ASSERT(m != NULL);
    tm_set_quiet(1);
    int d = tm_check_fat_judge(m, copie);
    tm_set_quiet(0);
    tm_free(m);
    TT_ASSERT(d >= 1);
  }
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void test_banc_voit_fat_cassee(void) {
  char copie[4096], out[8192];
  TT_ASSERT(copie_de("fat32-arbre.img", copie, sizeof copie) == 0);
  uint8_t bs[512];
  TT_ASSERT(lire_octets(copie, 0, bs, sizeof bs) == 0);
  uint32_t bps = le16(bs + 11), spc = bs[13], rsvd = le16(bs + 14), nfats = bs[16], fatsz = le32(bs + 36);
  uint64_t data = ((uint64_t)rsvd + (uint64_t)nfats * fatsz) * bps;
  int64_t off = localiser(copie, "/gros.bin");
  TT_ASSERT(off > 0 && (uint64_t)off >= data);
  uint32_t clu = (uint32_t)(((uint64_t)off - data) / ((uint64_t)bps * spc) + 2);
  uint8_t e[4] = {(uint8_t)clu, (uint8_t)(clu >> 8), (uint8_t)(clu >> 16), (uint8_t)(clu >> 24)};
  for (uint32_t f = 0; f < nfats; f++)
    TT_ASSERT(ecrire_octets(copie, ((uint64_t)rsvd + (uint64_t)f * fatsz) * bps + (uint64_t)clu * 4, e, 4) == 0);
  TT_ASSERT(fu_fsck_fat_n(copie, out, sizeof out) != 0);
  char man[4200];
  snprintf(man, sizeof man, "%s.manifest", copie);
  noter(man);
  TT_ASSERT_EQ(fu_fat_manifest(copie, man), 3);
}

static void test_banc_voit_somme_exfat(void) {
  char out[8192];
  TT_ASSERT(fu_fsck_fat_n(tt_fixture("exfat-somme.img"), out, sizeof out) != 0);
}

/* fsck.fat -n -v : « N bytes per cluster » et « …, used/total clusters ». */
static void test_banc_libre(void) {
  char out[16384];
  char *const argv[] = {(char *)"fsck.fat", (char *)"-n", (char *)"-v", (char *)tt_fixture("fat32-vide.img"), NULL};
  TT_ASSERT_EQ(fu_run(argv, out, sizeof out), 0);
  const char *c = strstr(out, " bytes per cluster");
  TT_ASSERT(c != NULL);
  while (c > out && c[-1] >= '0' && c[-1] <= '9') c--;
  unsigned long long clsz = strtoull(c, NULL, 10);
  const char *u = strstr(out, " clusters\n");
  TT_ASSERT(u != NULL);
  const char *s = u;
  while (s > out && s[-1] != ' ') s--;
  unsigned long long used = 0, total = 0;
  TT_ASSERT(sscanf(s, "%llu/%llu", &used, &total) == 2);
  TT_ASSERT(clsz > 0 && total > used);
  TT_ASSERT_EQ(fu_fat_free(tt_fixture("fat32-vide.img")), (long long)((total - used) * clsz));
}

int main(void) {
  if (!fu_fat_tools_available()) {
    printf("SAUTÉ : outils FAT/exFAT absents ou plateforme Android (banc réservé au PC)\n");
    return 77;
  }
  TT_RUN(test_banc_fixtures_propres);
  TT_RUN(test_banc_manifeste_arbre);
  TT_RUN(test_banc_voit_contenu);
  TT_RUN(test_banc_voit_fat_cassee);
  TT_RUN(test_banc_voit_somme_exfat);
  TT_RUN(test_banc_libre);
  nettoyer();
  TT_MAIN_END();
}
