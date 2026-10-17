#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <string.h>
#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/partition.h"
#include "../src/part/crc32.h"
#include "tt.h"

static fl_bdev *ouvrir(const char *nom) {
  fl_bdev *d = NULL;
  if (fl_file_bdev_open(tt_fixture(nom), 512, true, &d) != FL_OK) return NULL;
  return d;
}

static void test_crc32(void) {
  TT_ASSERT_EQ(fl_crc32("123456789", 9), 0xCBF43926u);
  TT_ASSERT_EQ(fl_crc32("", 0), 0u);
}

static void test_gpt(void) {
  fl_part_table t;
  fl_bdev *d = ouvrir("gpt-luks2.img");
  TT_ASSERT(d != NULL);
  TT_ASSERT_EQ(fl_part_scan(d, &t), FL_OK);
  TT_ASSERT_EQ(t.kind, FL_PT_GPT);
  TT_ASSERT_EQ(t.count, 1);
  TT_ASSERT_EQ(t.parts[0].index, 1);
  TT_ASSERT_EQ(t.parts[0].first_lba, 2048);
  TT_ASSERT_STR_EQ(t.parts[0].name, "FluData");
  TT_ASSERT_EQ(t.used_backup_gpt, false);
  fl_bdev_close(d);
}

static void test_mbr_etendue(void) {
  fl_part_table t;
  fl_bdev *d = ouvrir("mbr-etendue.img");
  TT_ASSERT(d != NULL);
  TT_ASSERT_EQ(fl_part_scan(d, &t), FL_OK);
  TT_ASSERT_EQ(t.kind, FL_PT_MBR);
  TT_ASSERT_EQ(t.count, 3);
  TT_ASSERT_EQ(t.parts[0].index, 1);
  TT_ASSERT_EQ(t.parts[1].index, 5);
  TT_ASSERT_EQ(t.parts[2].index, 6);
  TT_ASSERT_EQ(t.parts[0].mbr_type, 0x0C);
  TT_ASSERT_EQ(t.parts[1].mbr_type, 0x83);
  TT_ASSERT_EQ(t.parts[2].mbr_type, 0x82);
  TT_ASSERT_EQ(t.parts[1].sector_count, 2 * 1024 * 2);
  fl_bdev_close(d);
}

static void test_mbr_simple(void) {
  fl_part_table t;
  fl_bdev *d = ouvrir("mbr-luks1-essiv.img");
  TT_ASSERT(d != NULL);
  TT_ASSERT_EQ(fl_part_scan(d, &t), FL_OK);
  TT_ASSERT_EQ(t.kind, FL_PT_MBR);
  TT_ASSERT_EQ(t.count, 1);
  TT_ASSERT_EQ(t.parts[0].mbr_type, 0x83);
  TT_ASSERT_EQ(t.parts[0].first_lba, 2048);
  fl_bdev_close(d);
}

static void test_sans_table(void) {
  fl_part_table t;
  fl_bdev *d = ouvrir("whole-luks2-4k.img");
  TT_ASSERT(d != NULL);
  TT_ASSERT_EQ(fl_part_scan(d, &t), FL_OK);
  TT_ASSERT_EQ(t.kind, FL_PT_NONE);
  TT_ASSERT_EQ(t.count, 1);
  TT_ASSERT_EQ(t.parts[0].index, 0);
  TT_ASSERT_EQ(t.parts[0].first_lba, 0);
  TT_ASSERT_EQ(t.parts[0].sector_count, d->sector_count);
  fl_bdev_close(d);
}

static void test_gpt_secours(void) {
  fl_part_table t;
  fl_bdev *d = ouvrir("gpt-secours.img");
  TT_ASSERT(d != NULL);
  TT_ASSERT_EQ(fl_part_scan(d, &t), FL_OK);
  TT_ASSERT_EQ(t.kind, FL_PT_GPT);
  TT_ASSERT_EQ(t.count, 2);
  TT_ASSERT_EQ(t.used_backup_gpt, true);
  fl_bdev_close(d);
}

static void test_gpt_hors_bornes(void) {
  fl_part_table t;
  fl_bdev *d = ouvrir("gpt-hors-bornes.img");
  TT_ASSERT(d != NULL);
  TT_ASSERT_EQ(fl_part_scan(d, &t), FL_OK);
  TT_ASSERT_EQ(t.kind, FL_PT_GPT);
  TT_ASSERT_EQ(t.count, 1);
  TT_ASSERT_EQ(t.skipped, 1);
  fl_bdev_close(d);
}

int main(void) {
  TT_RUN(test_crc32);
  TT_RUN(test_gpt);
  TT_RUN(test_mbr_etendue);
  TT_RUN(test_mbr_simple);
  TT_RUN(test_sans_table);
  TT_RUN(test_gpt_secours);
  TT_RUN(test_gpt_hors_bornes);
  TT_MAIN_END();
}
