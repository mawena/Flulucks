#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <string.h>
#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/partition.h"
#include "fluluks/probe.h"
#include "tt.h"

static fl_bdev *ouvrir(const char *nom) {
  fl_bdev *d = NULL;
  if (fl_file_bdev_open(tt_fixture(nom), 512, true, &d) != FL_OK) return NULL;
  return d;
}

static void sonde(const char *nom, fl_content attendu, const char *label, fl_probe_result *r) {
  fl_bdev *d = ouvrir(nom);
  TT_ASSERT(d != NULL);
  TT_ASSERT_EQ(fl_probe(d, r), FL_OK);
  TT_ASSERT_EQ(r->content, attendu);
  if (label) TT_ASSERT_STR_EQ(r->label, label);
  fl_bdev_close(d);
}

static void test_probe_ext4(void) {
  fl_probe_result r;
  sonde("ext4-plain.img", FL_CONTENT_EXT4, "FLUTEST", &r);
  TT_ASSERT_STR_EQ(r.uuid, "0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0");
}

static void test_probe_luks(void) {
  fl_probe_result r;
  sonde("whole-luks2-4k.img", FL_CONTENT_LUKS2, NULL, &r);
  TT_ASSERT(strlen(r.uuid) == 36);
  sonde("whole-luks1-xts.img", FL_CONTENT_LUKS1, NULL, &r);
  TT_ASSERT(strlen(r.uuid) == 36);
  fl_bdev *d = ouvrir("gpt-luks2.img");
  TT_ASSERT(d != NULL);
  fl_part_table t;
  TT_ASSERT_EQ(fl_part_scan(d, &t), FL_OK);
  TT_ASSERT(t.count >= 1);
  fl_bdev *s = NULL;
  TT_ASSERT_EQ(fl_slice_bdev_new(d, t.parts[0].first_lba, t.parts[0].sector_count, &s), FL_OK);
  TT_ASSERT_EQ(fl_probe(s, &r), FL_OK);
  TT_ASSERT_EQ(r.content, FL_CONTENT_LUKS2);
  fl_bdev_close(s);
  fl_bdev_close(d);
}

static void test_probe_fat(void) {
  fl_probe_result r;
  sonde("fat16.img", FL_CONTENT_FAT16, "FLUFAT16", &r);
  sonde("fat32.img", FL_CONTENT_FAT32, "FLUFAT32", &r);
}

static void test_probe_autres(void) {
  fl_probe_result r;
  sonde("ntfs.img", FL_CONTENT_NTFS, "", &r);
  sonde("swap.img", FL_CONTENT_SWAP, NULL, &r);
  sonde("sig-exfat.img", FL_CONTENT_EXFAT, NULL, &r);
  sonde("sig-lvm2.img", FL_CONTENT_LVM2, NULL, &r);
  sonde("sig-bitlocker.img", FL_CONTENT_BITLOCKER, NULL, &r);
  sonde("sig-btrfs.img", FL_CONTENT_BTRFS, NULL, &r);
  sonde("sig-xfs.img", FL_CONTENT_XFS, NULL, &r);
}

static fl_bdev *tranche(fl_bdev *d, int idx, uint64_t force_count) {
  fl_part_table t;
  fl_bdev *s = NULL;
  if (fl_part_scan(d, &t) != FL_OK) return NULL;
  for (uint32_t i = 0; i < t.count; i++)
    if (t.parts[i].index == (uint32_t)idx) {
      uint64_t n = force_count ? force_count : t.parts[i].sector_count;
      if (fl_slice_bdev_new(d, t.parts[i].first_lba, n, &s) != FL_OK) return NULL;
      return s;
    }
  return NULL;
}

static void test_probe_inconnu(void) {
  fl_probe_result r;
  fl_bdev *d = ouvrir("mbr-etendue.img");
  TT_ASSERT(d != NULL);
  fl_bdev *s = tranche(d, 5, 0);
  TT_ASSERT(s != NULL);
  TT_ASSERT_EQ(fl_probe(s, &r), FL_OK);
  TT_ASSERT_EQ(r.content, FL_CONTENT_UNKNOWN);
  TT_ASSERT_STR_EQ(r.label, "");
  TT_ASSERT_STR_EQ(r.uuid, "");
  fl_bdev_close(s);
  fl_bdev_close(d);
}

static void test_probe_minuscule(void) {
  fl_probe_result r;
  fl_bdev *d = ouvrir("mbr-etendue.img");
  TT_ASSERT(d != NULL);
  fl_bdev *s = tranche(d, 5, 1);
  TT_ASSERT(s != NULL);
  TT_ASSERT_EQ(fl_probe(s, &r), FL_OK);
  TT_ASSERT_EQ(r.content, FL_CONTENT_UNKNOWN);
  fl_bdev_close(s);
  fl_bdev_close(d);
}

/* Périphériques en mémoire pour les cas hostiles. */
typedef struct { uint8_t data[2048 * 512]; bool fail; } memdev;

static int mem_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  memdev *m = d->priv;
  if (m->fail) return FL_EIO;
  memcpy(buf, m->data + lba * 512, (size_t)count * 512);
  return FL_OK;
}
static const fl_bdev_ops mem_ops = { mem_read, NULL, NULL, NULL };

static void mem_init(fl_bdev *d, memdev *m) {
  memset(m, 0, sizeof *m);
  d->ops = &mem_ops;
  d->sector_size = 512;
  d->sector_count = 2048;
  d->read_only = true;
  d->priv = m;
}

static void test_probe_erreur_es(void) {
  static memdev m;
  fl_bdev d;
  fl_probe_result r;
  mem_init(&d, &m);
  m.fail = true;
  TT_ASSERT_EQ(fl_probe(&d, &r), FL_EIO);
}

static void test_probe_bpb_hostile(void) {
  static memdev m;
  fl_bdev d;
  fl_probe_result r;
  for (int cas = 0; cas < 5; cas++) {
    mem_init(&d, &m);
    uint8_t *b = m.data;
    b[11] = 0x00; b[12] = 0x02; /* 512 o/secteur */
    b[13] = 1; b[14] = 1; b[15] = 0; b[16] = 2;
    b[19] = 0x00; b[20] = 0x08; /* 2048 secteurs */
    b[22] = 8;
    b[510] = 0x55; b[511] = 0xAA;
    if (cas == 0) b[13] = 0;               /* secteurs/cluster = 0 */
    if (cas == 1) { b[11] = 0; b[12] = 0; } /* octets/secteur = 0 */
    if (cas == 2) b[16] = 0;               /* 0 FAT */
    if (cas == 3) { b[19] = 10; b[20] = 0; } /* total <= métadonnées */
    if (cas == 4) b[13] = 3;               /* pas une puissance de 2 */
    TT_ASSERT_EQ(fl_probe(&d, &r), FL_OK);
    TT_ASSERT_EQ(r.content, FL_CONTENT_UNKNOWN);
  }
}

static void test_noms(void) {
  TT_ASSERT_STR_EQ(fl_content_name(FL_CONTENT_LUKS2), "LUKS2");
  TT_ASSERT_STR_EQ(fl_content_name(FL_CONTENT_EXFAT), "exFAT");
  TT_ASSERT_STR_EQ(fl_content_name(FL_CONTENT_UNKNOWN), "inconnu");
  TT_ASSERT_STR_EQ(fl_content_name((fl_content)99), "inconnu");
  TT_ASSERT(fl_content_supported(FL_CONTENT_EXT4));
  TT_ASSERT(fl_content_supported(FL_CONTENT_LUKS1));
  TT_ASSERT(!fl_content_supported(FL_CONTENT_NTFS));
}

int main(void) {
  TT_RUN(test_probe_ext4);
  TT_RUN(test_probe_luks);
  TT_RUN(test_probe_fat);
  TT_RUN(test_probe_autres);
  TT_RUN(test_probe_inconnu);
  TT_RUN(test_probe_minuscule);
  TT_RUN(test_probe_erreur_es);
  TT_RUN(test_probe_bpb_hostile);
  TT_RUN(test_noms);
  TT_MAIN_END();
}
