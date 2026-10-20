/* Périphérique de libntfs-3g sur fl_bdev (ntfs_dev.c) : cohérence du cache d'écriture différée, sur un
 * disque en mémoire. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fs/ntfs_dev.h"
#include "ntfs-3g/device.h"
#include "tt.h"

static uint8_t g_disk[16 << 20];

static int m_read(fl_bdev *d, uint64_t lba, uint32_t c, void *b) {
  memcpy(b, g_disk + lba * d->sector_size, (size_t)c * d->sector_size);
  return FL_OK;
}
static int m_write(fl_bdev *d, uint64_t lba, uint32_t c, const void *b) {
  memcpy(g_disk + lba * d->sector_size, b, (size_t)c * d->sector_size);
  return FL_OK;
}
static int m_flush(fl_bdev *d) {
  (void)d;
  return FL_OK;
}
static void m_destroy(fl_bdev *d) { (void)d; }
static const fl_bdev_ops k_mem = {m_read, m_write, m_flush, m_destroy};

/* Table pleine de secteurs en attente (1 à 8192), puis petite lecture des secteurs 0 (absent) et 1 (en
 * attente) : faire de la place écrit la table ; le secteur 1 lu doit être le plus récent, et le rester. */
static void test_ntfs_dev_lecture_table_pleine(void) {
  memset(g_disk, 0, sizeof g_disk);
  fl_bdev bd = {&k_mem, 512, sizeof g_disk / 512, false, NULL};
  ntfs_dev *d = NULL;
  TT_ASSERT_EQ(ntfs_dev_new(&bd, true, &d), FL_OK);
  struct ntfs_device *dev = ntfs_dev_device(d);
  TT_ASSERT_EQ(dev->d_ops->open(dev, O_RDWR), 0);
  uint8_t s[512], r[1024];
  memset(s, 0xAB, sizeof s);
  for (int i = 1; i <= 8192; i++) TT_ASSERT_EQ(dev->d_ops->pwrite(dev, s, 512, (s64)i * 512), 512);
  TT_ASSERT_EQ(dev->d_ops->pread(dev, r, 1024, 0), 1024);
  TT_ASSERT_EQ(r[0], 0x00);
  TT_ASSERT_EQ(r[512], 0xAB);
  TT_ASSERT_EQ(dev->d_ops->pread(dev, r, 512, 512), 512);
  TT_ASSERT_EQ(r[0], 0xAB);
  /* écriture partielle du secteur 1 : le reste du secteur garde le contenu le plus récent */
  TT_ASSERT_EQ(dev->d_ops->pwrite(dev, "x", 1, 512), 1);
  TT_ASSERT_EQ(dev->d_ops->sync(dev), 0);
  TT_ASSERT_EQ(g_disk[512], 'x');
  TT_ASSERT_EQ(g_disk[513], 0xAB);
  TT_ASSERT_EQ(g_disk[8192 * 512], 0xAB);
  dev->d_ops->close(dev);
  ntfs_dev_free(d, true);
}

/* Écriture au-delà de la fin du disque : refusée, et l'erreur reste (lecture seule collante côté colle). */
static void test_ntfs_dev_ecriture_hors_disque(void) {
  fl_bdev bd = {&k_mem, 512, 1024, false, NULL};
  ntfs_dev *d = NULL;
  TT_ASSERT_EQ(ntfs_dev_new(&bd, true, &d), FL_OK);
  struct ntfs_device *dev = ntfs_dev_device(d);
  TT_ASSERT_EQ(dev->d_ops->open(dev, O_RDWR), 0);
  uint8_t s[512] = {0};
  errno = 0;
  TT_ASSERT_EQ(dev->d_ops->pwrite(dev, s, 512, 1024 * 512), -1);
  TT_ASSERT_EQ(errno, EIO);
  TT_ASSERT_EQ(ntfs_dev_error(d), FL_EIO);
  TT_ASSERT_EQ(dev->d_ops->pwrite(dev, s, 512, 0), -1); /* collante */
  dev->d_ops->close(dev);
  ntfs_dev_free(d, true);
}

int main(void) {
  TT_RUN(test_ntfs_dev_lecture_table_pleine);
  TT_RUN(test_ntfs_dev_ecriture_hors_disque);
  TT_MAIN_END();
}
