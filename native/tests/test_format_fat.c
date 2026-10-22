#define _GNU_SOURCE
/* Phase 8, T2 : FAT32 et exFAT formatés par FatFs (f_mkfs), jugés par fsck.fat / fsck.exfat (aucune ligne
 * suspecte), étiquette lue par fatlabel / exfatlabel, puis montés et écrits par notre moteur. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/format.h"
#include "fluluks/fs.h"
#include "fluluks/probe.h"
#include "fsck_util.h"
#include "tt.h"

#define MIB (1024ull * 1024ull)

static int image(char *path, size_t cap, const char *nom, uint64_t bytes) {
  char td[4096];
  if (fu_tmpdir(td, sizeof td) != 0) return -1;
  snprintf(path, cap, "%s/%s", td, nom);
  int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) return -1;
  int rc = ftruncate(fd, (off_t)bytes);
  close(fd);
  return rc;
}

static void juger(const char *img, const char *etiquette, bool exfat) {
  char out[1 << 14];
  int rc = fu_fsck_fat_n(img, out, sizeof out);
  if (rc != 0) fprintf(stderr, "%s\n", out);
  TT_ASSERT_EQ(rc, 0);
  /* exfatlabel n'écrit l'UTF-8 que dans une locale UTF-8 */
  char *argv[] = {"env", "LC_ALL=C.UTF-8", (char *)(exfat ? "exfatlabel" : "fatlabel"), (char *)img, NULL};
  TT_ASSERT_EQ(fu_run(argv, out, sizeof out), 0);
  if (!strstr(out, etiquette)) {
    fprintf(stderr, "étiquette : %s\n", out);
    TT_ASSERT(0);
  }
}

/* Formate, juge, monte en écriture, écrit, démonte, juge encore. */
static void cas(uint32_t ss, uint64_t taille, bool exfat, const char *etiquette, const char *lue) {
  char img[4096];
  TT_ASSERT_EQ(image(img, sizeof img, "format-fat.img", taille), 0);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(img, ss, false, &d), FL_OK);
  TT_ASSERT_EQ(fl_mkfs_fat(d, exfat, etiquette), FL_OK);
  juger(img, lue, exfat);

  fl_fs *fs = NULL;
  TT_ASSERT_EQ(fl_fs_mount(d, exfat ? FL_CONTENT_EXFAT : FL_CONTENT_FAT32, true, &fs), FL_OK);
  fl_statfs st;
  TT_ASSERT_EQ(fl_fs_statfs(fs, &st), FL_OK);
  TT_ASSERT(!st.read_only);
  TT_ASSERT(st.total_bytes > 50 * MIB);
  TT_ASSERT_EQ(fl_fs_mkdir(fs, "/Dossier"), FL_OK);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(fs, "/Dossier/été.txt", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pwrite(f, "bonjour", 7, 0), 7);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
  juger(img, lue, exfat);
  unlink(img);
}

static void test_fat32_propre(void) { cas(512, 64 * MIB, false, "Portail", "PORTAIL"); }
static void test_exfat_propre(void) { cas(512, 64 * MIB, true, "Clé été", "Clé été"); }
/* FAT32 en secteurs de 4 096 octets : 65 525 clusters de 4 Kio au moins, donc 300 Mio */
static void test_secteurs_4096(void) {
  cas(4096, 64 * MIB, true, "QUATREK", "QUATREK");
  cas(4096, 300 * MIB, false, "QUATREK", "QUATREK");
}

/* Étiquette trop longue (ou hors ASCII en FAT32) : refus avant toute écriture. */
static void test_etiquette_refusee(void) {
  char img[4096];
  TT_ASSERT_EQ(image(img, sizeof img, "format-fat-lab.img", 64 * MIB), 0);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(img, 512, false, &d), FL_OK);
  TT_ASSERT_EQ(fl_mkfs_fat(d, false, "DOUZE_CARACT"), FL_EINVAL);
  TT_ASSERT_EQ(fl_mkfs_fat(d, false, "été"), FL_EINVAL);
  TT_ASSERT_EQ(fl_mkfs_fat(d, true, "douze carac."), FL_EINVAL);
  uint8_t s[512], z[512] = {0};
  TT_ASSERT_EQ(fl_bdev_read(d, 0, 1, s), FL_OK);
  TT_ASSERT_MEM_EQ(s, z, sizeof s); /* rien d'écrit */
  fl_bdev_close(d);
  unlink(img);
}

/* Trop petit pour FAT32 (FatFs exige 65 525 clusters) : refus. */
static void test_trop_petit(void) {
  char img[4096];
  TT_ASSERT_EQ(image(img, sizeof img, "format-fat-petit.img", 16 * MIB), 0);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(img, 512, false, &d), FL_OK);
  TT_ASSERT_EQ(fl_mkfs_fat(d, false, ""), FL_ENOSPC);
  fl_bdev_close(d);
  unlink(img);
}

int main(void) {
  if (!fu_fat_tools_available()) return 0; /* outils FAT absents : test sans objet */
  TT_RUN(test_fat32_propre);
  TT_RUN(test_exfat_propre);
  TT_RUN(test_secteurs_4096);
  TT_RUN(test_etiquette_refusee);
  TT_RUN(test_trop_petit);
  TT_MAIN_END();
}
