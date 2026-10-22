#define _GNU_SOURCE
/* Phase 8, T1 : table de partitions à une partition (MBR jusqu'à 2 Tio, GPT au-delà), jugée par sfdisk et
 * sgdisk (sans root, sur des images creuses) et relue par fl_part_scan. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/format.h"
#include "fluluks/partition.h"
#include "fsck_util.h"
#include "tt.h"

#define MIB (1024ull * 1024ull)

/* Image creuse de `bytes` octets, pleine de 0xA5 au début et à la fin (anciennes données). */
static int image(char *path, size_t cap, const char *nom, uint64_t bytes) {
  char td[4096];
  if (fu_tmpdir(td, sizeof td) != 0) return -1;
  snprintf(path, cap, "%s/%s", td, nom);
  int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) return -1;
  int rc = ftruncate(fd, (off_t)bytes);
  static unsigned char vieux[2 * 1024 * 1024];
  memset(vieux, 0xA5, sizeof vieux);
  if (rc == 0 && pwrite(fd, vieux, sizeof vieux, 0) != (ssize_t)sizeof vieux) rc = -1;
  if (rc == 0 && pwrite(fd, vieux, sizeof vieux, (off_t)(bytes - sizeof vieux)) != (ssize_t)sizeof vieux) rc = -1;
  close(fd);
  return rc;
}

static int run(char *out, size_t cap, const char *a0, const char *a1, const char *a2) {
  char *argv[] = {(char *)a0, (char *)a1, (char *)a2, NULL};
  return fu_run(argv, out, cap);
}

/* Écrit la table et vérifie : relue par fl_part_scan, une partition, alignée sur 1 Mio, dans le disque. */
static void ecrire(const char *img, uint32_t ss, fl_fmt_fs fs, bool chiffre, fl_pt_kind attendu) {
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(img, ss, false, &d), FL_OK);
  uint64_t first = 0, count = 0;
  TT_ASSERT_EQ(fl_part_write_single(d, fs, chiffre, &first, &count), FL_OK);
  TT_ASSERT_EQ(first * ss, MIB);
  TT_ASSERT_EQ((count * ss) % MIB, 0);
  TT_ASSERT(first + count <= d->sector_count);
  TT_ASSERT((d->sector_count - first - count) * ss < 2 * MIB); /* tout le disque, au Mio près */
  static fl_part_table t;
  TT_ASSERT_EQ(fl_part_scan(d, &t), FL_OK);
  TT_ASSERT_EQ(t.kind, attendu);
  TT_ASSERT_EQ(t.count, 1);
  TT_ASSERT_EQ(t.parts[0].first_lba, first);
  TT_ASSERT_EQ(t.parts[0].sector_count, count);
  TT_ASSERT(!t.used_backup_gpt);
  fl_bdev_close(d);
}

static void test_mbr_petit_disque(void) {
  char img[4096], out[8192];
  struct { fl_fmt_fs fs; bool chiffre; const char *type; } cas[] = {
      {FL_FMT_FAT32, false, "type=c"}, {FL_FMT_EXFAT, false, "type=7"}, {FL_FMT_NTFS, false, "type=7"},
      {FL_FMT_EXT4, false, "type=83"}, {FL_FMT_FAT32, true, "type=83"}, /* LUKS : partition Linux */
  };
  for (size_t i = 0; i < sizeof cas / sizeof cas[0]; i++) {
    TT_ASSERT_EQ(image(img, sizeof img, "part-mbr.img", 64 * MIB), 0);
    ecrire(img, 512, cas[i].fs, cas[i].chiffre, FL_PT_MBR);
    TT_ASSERT_EQ(run(out, sizeof out, "sfdisk", "-d", img), 0);
    if (!strstr(out, "label: dos") || !strstr(out, "start=        2048") || !strstr(out, cas[i].type)) {
      fprintf(stderr, "%s\n", out);
      TT_ASSERT(0);
    }
    TT_ASSERT_EQ(run(out, sizeof out, "sfdisk", "--verify", img), 0);
    unlink(img);
  }
}

static void test_gpt_gros_disque(void) {
  char img[4096], out[8192];
  struct { fl_fmt_fs fs; const char *code; } cas[] = {{FL_FMT_NTFS, "0700"}, {FL_FMT_EXT4, "8300"}};
  for (size_t i = 0; i < 2; i++) {
    TT_ASSERT_EQ(image(img, sizeof img, "part-gpt.img", 3ull * 1024 * 1024 * MIB), 0); /* 3 Tio, creuse */
    ecrire(img, 512, cas[i].fs, false, FL_PT_GPT);
    TT_ASSERT_EQ(run(out, sizeof out, "sgdisk", "-v", img), 0);
    if (!strstr(out, "No problems found")) {
      fprintf(stderr, "%s\n", out);
      TT_ASSERT(0);
    }
    TT_ASSERT_EQ(run(out, sizeof out, "sgdisk", "-p", img), 0);
    if (!strstr(out, cas[i].code)) {
      fprintf(stderr, "%s\n", out);
      TT_ASSERT(0);
    }
    unlink(img);
  }
}

/* Secteurs de 4 096 octets (certains boîtiers USB) : relu par notre analyseur, aligné. */
static void test_secteurs_4096(void) {
  char img[4096];
  TT_ASSERT_EQ(image(img, sizeof img, "part-4k.img", 64 * MIB), 0);
  ecrire(img, 4096, FL_FMT_EXFAT, false, FL_PT_MBR);
  unlink(img);
  TT_ASSERT_EQ(image(img, sizeof img, "part-4k-gpt.img", 3ull * 1024 * 1024 * MIB), 0);
  ecrire(img, 4096, FL_FMT_EXT4, true, FL_PT_GPT);
  unlink(img);
}

/* Un ancien GPT (en tête et de secours en fin de disque) ne survit pas à une nouvelle table MBR. */
static void test_ancien_gpt_efface(void) {
  char img[4096], out[8192];
  TT_ASSERT_EQ(image(img, sizeof img, "part-ancien.img", 64 * MIB), 0);
  ecrire(img, 512, FL_FMT_EXT4, false, FL_PT_MBR);
  char *gpt[] = {"sgdisk", "-g", "-o", img, NULL}; /* vieux GPT vide, en tête et en fin */
  TT_ASSERT_EQ(fu_run(gpt, out, sizeof out), 0);
  TT_ASSERT_EQ(run(out, sizeof out, "sfdisk", "-d", img), 0);
  TT_ASSERT(strstr(out, "label: gpt") != NULL); /* la préparation a bien laissé un GPT */
  ecrire(img, 512, FL_FMT_FAT32, false, FL_PT_MBR);
  TT_ASSERT_EQ(run(out, sizeof out, "sfdisk", "-d", img), 0);
  if (!strstr(out, "label: dos")) {
    fprintf(stderr, "%s\n", out);
    TT_ASSERT(0);
  }
  unlink(img);
}

int main(void) {
  TT_RUN(test_mbr_petit_disque);
  TT_RUN(test_gpt_gros_disque);
  TT_RUN(test_secteurs_4096);
  TT_RUN(test_ancien_gpt_efface);
  TT_MAIN_END();
}
