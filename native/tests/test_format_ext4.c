#define _GNU_SOURCE
/* Phase 8, T3 : ext4 avec journal formaté par lwext4 (ext4_mkfs), jugé par e2fsck -fn et dumpe2fs, puis
 * ouvert en écriture par notre moteur (aucune option refusée par ext4_policy), écrit, et jugé encore. */
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

static void juger(const char *img) {
  char out[1 << 15];
  int rc = fu_e2fsck_n(img, out, sizeof out);
  if (rc != 0) fprintf(stderr, "%s\n", out);
  TT_ASSERT_EQ(rc, 0);
}

static void cas(uint32_t ss, uint64_t taille) {
  char img[4096], out[1 << 15];
  TT_ASSERT_EQ(image(img, sizeof img, "format-ext4.img", taille), 0);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(img, ss, false, &d), FL_OK);
  TT_ASSERT_EQ(fl_mkfs_ext4(d, "Données", 1000, 1000), FL_OK);
  juger(img);
  char *dump[] = {"env", "LC_ALL=C.UTF-8", "dumpe2fs", "-h", img, NULL};
  TT_ASSERT_EQ(fu_run(dump, out, sizeof out), 0);
  const char *attendus[] = {"has_journal", "extent", "Filesystem volume name:   Données",
                            "Filesystem state:         clean", "Block size:               4096"};
  for (size_t i = 0; i < sizeof attendus / sizeof attendus[0]; i++) {
    if (!strstr(out, attendus[i])) {
      fprintf(stderr, "absent : %s\n%s\n", attendus[i], out);
      TT_ASSERT(0);
    }
  }

  fl_fs *fs = NULL;
  TT_ASSERT_EQ(fl_fs_mount(d, FL_CONTENT_EXT4, true, &fs), FL_OK);
  fl_statfs st;
  TT_ASSERT_EQ(fl_fs_statfs(fs, &st), FL_OK);
  if (st.read_only) fprintf(stderr, "lecture seule : %s\n", st.ro_reason);
  TT_ASSERT(!st.read_only); /* aucune option refusée par ext4_policy */
  TT_ASSERT_STR_EQ(st.label, "Données");
  fl_stat racine;
  TT_ASSERT_EQ(fl_fs_stat(fs, "/", &racine), FL_OK);
  TT_ASSERT_EQ(racine.uid, 1000);
  TT_ASSERT_EQ(racine.gid, 1000);
  TT_ASSERT_EQ(fl_fs_mkdir(fs, "/Photos"), FL_OK);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(fs, "/Photos/été.txt", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  static char gros[3 * 1024 * 1024];
  memset(gros, 'x', sizeof gros);
  TT_ASSERT_EQ(fl_file_pwrite(f, gros, sizeof gros, 0), (int64_t)sizeof gros);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  fl_stat fst;
  TT_ASSERT_EQ(fl_fs_stat(fs, "/Photos/été.txt", &fst), FL_OK);
  TT_ASSERT_EQ(fst.uid, 1000); /* hérité de la racine : l'utilisateur d'Ubuntu en est propriétaire */
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
  juger(img);
  unlink(img);
}

static void test_ext4_propre(void) { cas(512, 64 * MIB); }
static void test_ext4_secteurs_4096(void) { cas(4096, 64 * MIB); }
static void test_ext4_plusieurs_groupes(void) { cas(512, 1100 * MIB); } /* > 128 Mio : plusieurs groupes */

static void test_refus(void) {
  char img[4096];
  TT_ASSERT_EQ(image(img, sizeof img, "format-ext4-refus.img", 6 * MIB), 0);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(img, 512, false, &d), FL_OK);
  TT_ASSERT_EQ(fl_mkfs_ext4(d, "", 1000, 1000), FL_ENOSPC); /* journal compris : 8 Mio au moins */
  fl_bdev_close(d);
  TT_ASSERT_EQ(image(img, sizeof img, "format-ext4-refus.img", 64 * MIB), 0);
  TT_ASSERT_EQ(fl_file_bdev_open(img, 512, false, &d), FL_OK);
  TT_ASSERT_EQ(fl_mkfs_ext4(d, "dix-sept octets!!", 1000, 1000), FL_EINVAL); /* 16 octets au plus */
  fl_bdev_close(d);
  unlink(img);
}

int main(void) {
  if (!fu_tools_available()) return 0; /* e2fsprogs absent : test sans objet */
  TT_RUN(test_ext4_propre);
  TT_RUN(test_ext4_secteurs_4096);
  TT_RUN(test_ext4_plusieurs_groupes);
  TT_RUN(test_refus);
  TT_MAIN_END();
}
