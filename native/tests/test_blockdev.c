#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "tt.h"

#define IMG_SIZE 10000

static char g_path[] = "fl_test_bdev_XXXXXX";
static unsigned char g_pat[IMG_SIZE];

static int make_image(void) {
  for (int i = 0; i < IMG_SIZE; i++) g_pat[i] = (unsigned char)(i & 0xFF);
  int fd = mkstemp(g_path);
  if (fd < 0) return -1;
  ssize_t w = write(fd, g_pat, IMG_SIZE);
  close(fd);
  return w == IMG_SIZE ? 0 : -1;
}

static void test_fichier_taille(void) {
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(g_path, 512, true, &d), FL_OK);
  TT_ASSERT_EQ(d->sector_size, 512);
  TT_ASSERT_EQ(d->sector_count, 19);
  TT_ASSERT_EQ(fl_bdev_size_bytes(d), 19 * 512);
  fl_bdev_close(d);
}

static void test_lecture_secteur(void) {
  fl_bdev *d = NULL;
  unsigned char buf[512];
  TT_ASSERT_EQ(fl_file_bdev_open(g_path, 512, true, &d), FL_OK);
  TT_ASSERT_EQ(fl_bdev_read(d, 3, 1, buf), FL_OK);
  TT_ASSERT_MEM_EQ(buf, g_pat + 1536, 512);
  fl_bdev_close(d);
}

static void test_hors_bornes(void) {
  fl_bdev *d = NULL;
  unsigned char buf[1024];
  TT_ASSERT_EQ(fl_file_bdev_open(g_path, 512, true, &d), FL_OK);
  TT_ASSERT_EQ(fl_bdev_read(d, 18, 2, buf), FL_EINVAL);
  TT_ASSERT_EQ(fl_bdev_read(d, UINT64_MAX, 1, buf), FL_EINVAL);
  TT_ASSERT_EQ(fl_bdev_read_bytes(d, UINT64_MAX, 2, buf), FL_EINVAL);
  TT_ASSERT_EQ(fl_bdev_read_bytes(d, 19 * 512 - 1, 2, buf), FL_EINVAL);
  fl_bdev_close(d);
}

static void test_lecture_octets_non_alignee(void) {
  fl_bdev *d = NULL;
  unsigned char buf[700];
  TT_ASSERT_EQ(fl_file_bdev_open(g_path, 512, true, &d), FL_OK);
  TT_ASSERT_EQ(fl_bdev_read_bytes(d, 1000, 700, buf), FL_OK);
  TT_ASSERT_MEM_EQ(buf, g_pat + 1000, 700);
  TT_ASSERT_EQ(fl_bdev_read_bytes(d, 5, 0, buf), FL_OK);
  fl_bdev_close(d);
}

static void test_lecture_seule(void) {
  fl_bdev *d = NULL;
  unsigned char buf[512] = {0};
  TT_ASSERT_EQ(fl_file_bdev_open(g_path, 512, true, &d), FL_OK);
  TT_ASSERT_EQ(fl_bdev_write(d, 0, 1, buf), FL_EROFS);
  fl_bdev_close(d);
}

static void test_tranche(void) {
  fl_bdev *p = NULL, *s = NULL, *bad = NULL;
  unsigned char buf[512];
  TT_ASSERT_EQ(fl_file_bdev_open(g_path, 512, true, &p), FL_OK);
  TT_ASSERT_EQ(fl_slice_bdev_new(p, 2, 5, &s), FL_OK);
  TT_ASSERT_EQ(s->sector_count, 5);
  TT_ASSERT_EQ(s->sector_size, 512);
  TT_ASSERT_EQ(fl_bdev_read(s, 0, 1, buf), FL_OK);
  TT_ASSERT_MEM_EQ(buf, g_pat + 1024, 512);
  TT_ASSERT_EQ(fl_bdev_read(s, 5, 1, buf), FL_EINVAL);
  TT_ASSERT_EQ(fl_slice_bdev_new(p, 17, 3, &bad), FL_EINVAL);
  TT_ASSERT_EQ(fl_slice_bdev_new(p, UINT64_MAX, 2, &bad), FL_EINVAL);
  TT_ASSERT(bad == NULL);
  fl_bdev_close(s);
  /* le parent reste utilisable après fermeture de la tranche */
  TT_ASSERT_EQ(fl_bdev_read(p, 0, 1, buf), FL_OK);
  fl_bdev_close(p);
  fl_bdev_close(NULL);
}

static void test_absent(void) {
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open("/nonexistent/fl_no_such_file", 512, true, &d), FL_ENOENT);
}

int main(void) {
  if (make_image() != 0) { fprintf(stderr, "image temporaire impossible\n"); return 1; }
  TT_RUN(test_fichier_taille);
  TT_RUN(test_lecture_secteur);
  TT_RUN(test_hors_bornes);
  TT_RUN(test_lecture_octets_non_alignee);
  TT_RUN(test_lecture_seule);
  TT_RUN(test_tranche);
  TT_RUN(test_absent);
  unlink(g_path);
  TT_MAIN_END();
}
