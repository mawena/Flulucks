#define _GNU_SOURCE
/* FAT12/16/32 et exFAT par FatFs (phase 4) : montage, statfs, politique de lecture seule, lecture.
 * Tourne aussi sur Android (fixtures poussées) : les écritures ne portent que sur des copies, dans le
 * dossier temporaire du banc. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fluluks/probe.h"
#include "fs/ro_reasons.h"
#include "fsck_util.h"
#include "tt.h"

/* Un volume monté et ce qui le porte. */
typedef struct {
  char copie[4096]; /* "" si la fixture est montée directement (lecture seule) */
  fl_bdev *dev;
  fl_fs *fs;
} vol;

/* Monte une fixture : copie (want_rw) ou image elle-même en lecture seule. ss : taille de secteur du
 * périphérique. Rend le code de fl_fs_mount (ou FL_EIO si la copie ou l'ouverture échoue). */
static int monter_ss(const char *fixture, uint32_t ss, bool want_rw, vol *v) {
  memset(v, 0, sizeof *v);
  const char *src = tt_fixture(fixture);
  if (want_rw) {
    if (fu_copy_image(src, v->copie, sizeof v->copie) != 0) return FL_EIO;
    src = v->copie;
  }
  if (fl_file_bdev_open(src, ss, !want_rw, &v->dev) != FL_OK) return FL_EIO;
  fl_probe_result pr;
  if (fl_probe(v->dev, &pr) != FL_OK) return FL_EIO;
  return fl_fs_mount(v->dev, pr.content, want_rw, &v->fs);
}

static int monter(const char *fixture, bool want_rw, vol *v) { return monter_ss(fixture, 512, want_rw, v); }

static void demonter(vol *v) {
  if (v->fs && fl_fs_unmount(v->fs) != FL_OK) fprintf(stderr, "démontage refusé\n");
  v->fs = NULL;
  fl_bdev_close(v->dev);
  v->dev = NULL;
  if (v->copie[0]) unlink(v->copie);
  v->copie[0] = 0;
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* Octets de données (clusters × taille de cluster), calculés depuis le secteur de démarrage. */
static uint64_t octets_donnees(fl_bdev *dev) {
  uint8_t b[512];
  if (fl_bdev_read_bytes(dev, 0, sizeof b, b) != FL_OK) return 0;
  if (!memcmp(b + 3, "EXFAT   ", 8)) return (uint64_t)le32(b + 92) << (b[108] + b[109]);
  uint32_t bps = le16(b + 11), spc = b[13], rsvd = le16(b + 14), nfats = b[16], rootent = le16(b + 17);
  uint32_t tot = le16(b + 19) ? le16(b + 19) : le32(b + 32), fatsz = le16(b + 22) ? le16(b + 22) : le32(b + 36);
  uint32_t data = rsvd + nfats * fatsz + (rootent * 32 + bps - 1) / bps;
  return (uint64_t)((tot - data) / spc) * spc * bps;
}

static void test_fat_montage_types(void) {
  static const struct {
    const char *img, *label;
  } cas[] = {{"fat12-arbre.img", "FLUFAT12"}, {"fat16-arbre.img", "FLUFAT16"}, {"fat32-arbre.img", "FLUFAT32"},
             {"fat32-4k.img", "FLUFAT4K"},    {"exfat-arbre.img", "FLUEXFAT"}};
  for (size_t i = 0; i < sizeof cas / sizeof cas[0]; i++) {
    vol v;
    int rc = monter(cas[i].img, true, &v);
    if (rc != FL_OK) fprintf(stderr, "%s : montage = %d\n", cas[i].img, rc);
    if (rc != FL_OK) demonter(&v);
    TT_ASSERT_EQ(rc, FL_OK);
    fl_statfs st;
    rc = fl_fs_statfs(v.fs, &st);
    uint64_t attendu = octets_donnees(v.dev);
    demonter(&v);
    TT_ASSERT_EQ(rc, FL_OK);
    TT_ASSERT(!st.read_only);
    TT_ASSERT_STR_EQ(st.ro_reason, "");
    TT_ASSERT_STR_EQ(st.label, cas[i].label);
    TT_ASSERT_EQ(st.total_bytes, attendu);
    TT_ASSERT(st.free_bytes > 0 && st.free_bytes < st.total_bytes);
  }
}

static void test_fat_libre(void) {
  if (!fu_fat_tools_available()) return; /* juge absent (Android) */
  vol v;
  TT_ASSERT_EQ(monter("fat32-vide.img", true, &v), FL_OK);
  fl_statfs st;
  int rc = fl_fs_statfs(v.fs, &st);
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(st.free_bytes, fu_fat_free(tt_fixture("fat32-vide.img")));
}

/* Volume en écriture refusée pour `raison`, mais lisible. */
static void attendre_lecture_seule(const char *img, uint32_t ss, const char *raison) {
  vol v;
  int rc = monter_ss(img, ss, true, &v);
  if (rc != FL_OK) fprintf(stderr, "%s : montage = %d\n", img, rc);
  if (rc != FL_OK) demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  fl_statfs st;
  rc = fl_fs_statfs(v.fs, &st);
  int mk = fl_fs_mkdir(v.fs, "/nouveau");
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  if (!st.read_only || strcmp(st.ro_reason, raison)) fprintf(stderr, "%s : « %s »\n", img, st.ro_reason);
  TT_ASSERT(st.read_only);
  TT_ASSERT_STR_EQ(st.ro_reason, raison);
  TT_ASSERT_EQ(mk, FL_EROFS);
}

static void test_fat_sale(void) {
  static const char *const sales[] = {"fat16-sale.img", "fat32-sale.img", "fat32-fat1-sale.img", "exfat-sale.img"};
  for (size_t i = 0; i < sizeof sales / sizeof sales[0]; i++) attendre_lecture_seule(sales[i], 512, FL_RO_DIRTY);
  attendre_lecture_seule("fat32-media.img", 512, FL_RO_MEDIA);
  attendre_lecture_seule("exfat-media.img", 512, FL_RO_MEDIA);
  attendre_lecture_seule("exfat-somme.img", 512, FL_RO_CSUM);
}

static void test_fat_secteur_512_sur_4k(void) { attendre_lecture_seule("fat32-512-sur-4k.img", 4096, FL_RO_SECTOR); }

static void test_fat_lecture_seule_demandee(void) {
  vol v;
  TT_ASSERT_EQ(monter("fat32-arbre.img", false, &v), FL_OK);
  fl_statfs st;
  int rc = fl_fs_statfs(v.fs, &st);
  int mk = fl_fs_mkdir(v.fs, "/x");
  demonter(&v);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT(st.read_only);
  TT_ASSERT_STR_EQ(st.ro_reason, FL_RO_USER);
  TT_ASSERT_EQ(mk, FL_EROFS);
}

static void test_fat_emplacements(void) {
  vol v[9];
  for (int tour = 0; tour < 2; tour++) {
    for (int i = 0; i < 8; i++) TT_ASSERT_EQ(monter("exfat-arbre.img", false, &v[i]), FL_OK);
    int rc = monter("fat16-arbre.img", false, &v[8]);
    demonter(&v[8]);
    for (int i = 0; i < 8; i++) demonter(&v[i]);
    TT_ASSERT_EQ(rc, FL_EBUSY);
    vol c;
    rc = monter("sig-exfat.img", false, &c);
    demonter(&c);
    TT_ASSERT_EQ(rc, FL_ECORRUPT);
  }
}

static void test_fat_raisons_tiennent(void) {
  TT_ASSERT(strlen(FL_RO_DIRTY) <= 127);
  TT_ASSERT(strlen(FL_RO_MEDIA) <= 127);
  TT_ASSERT(strlen(FL_RO_SECTOR) <= 127);
}

int main(void) {
  TT_RUN(test_fat_montage_types);
  TT_RUN(test_fat_libre);
  TT_RUN(test_fat_sale);
  TT_RUN(test_fat_secteur_512_sur_4k);
  TT_RUN(test_fat_lecture_seule_demandee);
  TT_RUN(test_fat_emplacements);
  TT_RUN(test_fat_raisons_tiennent);
  TT_MAIN_END();
}
