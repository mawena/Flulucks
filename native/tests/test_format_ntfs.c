#define _GNU_SOURCE
/* Phase 8, T4 : NTFS formaté par mkntfs (ntfs-3g 2022.10.3, appelé comme bibliothèque), jugé par ntfsfix -n
 * et ntfsjuge.py, étiquette lue par ntfslabel, puis ouvert en écriture par notre moteur, écrit, et jugé
 * encore. */
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

static void juger(const char *img, const char *etiquette) {
  char out[1 << 14];
  int rc = fu_ntfsfix_n(img, out, sizeof out);
  if (rc != 0 || strstr(out, "rror") || strstr(out, "arning")) {
    fprintf(stderr, "ntfsfix : %s\n", out);
    TT_ASSERT(0);
  }
  rc = fu_ntfsjuge(out, sizeof out, "check", img, NULL);
  if (rc != 0) fprintf(stderr, "ntfsjuge : %s\n", out);
  TT_ASSERT_EQ(rc, 0);
  char *argv[] = {"env", "LC_ALL=C.UTF-8", "ntfslabel", (char *)img, NULL};
  TT_ASSERT_EQ(fu_run(argv, out, sizeof out), 0);
  if (strncmp(out, etiquette, strlen(etiquette)) || out[strlen(etiquette)] != '\n') {
    fprintf(stderr, "étiquette : %s\n", out);
    TT_ASSERT(0);
  }
}

static uint64_t le(const uint8_t *p, int n) {
  uint64_t v = 0;
  for (int i = n - 1; i >= 0; i--) v = v << 8 | p[i];
  return v;
}

/* Formate, juge, lit le secteur de démarrage, monte en écriture, écrit, démonte, juge encore. Numéro de
 * série du volume dans *serie_out (si non NULL). */
static void cas(uint32_t ss, uint64_t taille, const char *etiquette, uint64_t debut, uint64_t *serie_out) {
  char img[4096];
  TT_ASSERT_EQ(image(img, sizeof img, "format-ntfs.img", taille), 0);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(img, ss, false, &d), FL_OK);
  TT_ASSERT_EQ(fl_mkfs_ntfs(d, etiquette, debut), FL_OK);
  juger(img, etiquette);

  uint8_t bs[512];
  TT_ASSERT_EQ(fl_bdev_read_bytes(d, 0, sizeof bs, bs), FL_OK);
  TT_ASSERT_MEM_EQ(bs + 3, "NTFS    ", 8);
  TT_ASSERT_EQ(le(bs + 0x0B, 2), ss);       /* octets par secteur */
  TT_ASSERT_EQ(le(bs + 0x18, 2), 63);       /* secteurs par piste */
  TT_ASSERT_EQ(le(bs + 0x1A, 2), 255);      /* têtes */
  TT_ASSERT_EQ(le(bs + 0x1C, 4), debut);    /* secteurs cachés : début de la partition */
  uint64_t serie = le(bs + 0x48, 8);
  TT_ASSERT(serie != 0);

  fl_fs *fs = NULL;
  TT_ASSERT_EQ(fl_fs_mount(d, FL_CONTENT_NTFS, true, &fs), FL_OK);
  fl_statfs st;
  TT_ASSERT_EQ(fl_fs_statfs(fs, &st), FL_OK);
  if (st.read_only) fprintf(stderr, "lecture seule : %s\n", st.ro_reason);
  TT_ASSERT(!st.read_only);
  TT_ASSERT_STR_EQ(st.label, etiquette);
  TT_ASSERT(st.total_bytes > taille - 2 * MIB);
  TT_ASSERT_EQ(fl_fs_mkdir(fs, "/Photos"), FL_OK);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(fs, "/Photos/été.txt", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  static char gros[3 * 1024 * 1024];
  memset(gros, 'x', sizeof gros);
  TT_ASSERT_EQ(fl_file_pwrite(f, gros, sizeof gros, 0), (int64_t)sizeof gros);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_EQ(fl_fs_unmount(fs), FL_OK);
  fl_bdev_close(d);
  juger(img, etiquette);
  unlink(img);
  if (serie_out) *serie_out = serie;
}

static void test_ntfs_propre(void) { cas(512, 64 * MIB, "Clé été", 2048, NULL); }
static void test_ntfs_secteurs_4096(void) { cas(4096, 64 * MIB, "QUATREK", 256, NULL); }
static void test_ntfs_sans_etiquette(void) { cas(512, 300 * MIB, "", 0, NULL); }

/* Deux formatages de suite dans le même processus : l'état global de mkntfs est remis à zéro, et le
 * numéro de série vient de l'aléa (pas de l'heure, identique dans la même seconde). */
static void test_deux_fois(void) {
  uint64_t a = 0, b = 0;
  cas(512, 64 * MIB, "Un", 2048, &a);
  cas(512, 64 * MIB, "Deux", 2048, &b);
  TT_ASSERT(a != b);
}

/* Étiquette trop longue ou invalide, disque trop petit : refus avant toute écriture. */
static void test_refus(void) {
  char img[4096];
  TT_ASSERT_EQ(image(img, sizeof img, "format-ntfs-refus.img", 64 * MIB), 0);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(img, 512, false, &d), FL_OK);
  TT_ASSERT_EQ(fl_mkfs_ntfs(d, "trente-trois caractères, c'est !!", 2048), FL_EINVAL); /* 32 au plus */
  TT_ASSERT_EQ(fl_mkfs_ntfs(d, "\xC3\x28", 2048), FL_EINVAL); /* UTF-8 invalide */
  TT_ASSERT_EQ(fl_mkfs_ntfs(d, "a\tb", 2048), FL_EINVAL);
  TT_ASSERT_EQ(fl_mkfs_ntfs(d, "", 1ull << 32), FL_EINVAL); /* secteurs cachés sur 32 bits */
  uint8_t s[512], z[512] = {0};
  TT_ASSERT_EQ(fl_bdev_read(d, 0, 1, s), FL_OK);
  TT_ASSERT_MEM_EQ(s, z, sizeof s); /* rien d'écrit */
  fl_bdev_close(d);
  TT_ASSERT_EQ(image(img, sizeof img, "format-ntfs-refus.img", 6 * MIB), 0);
  TT_ASSERT_EQ(fl_file_bdev_open(img, 512, false, &d), FL_OK);
  TT_ASSERT_EQ(fl_mkfs_ntfs(d, "", 2048), FL_ENOSPC);
  fl_bdev_close(d);
  unlink(img);
}

int main(void) {
  if (!fu_ntfs_tools_available()) return 0; /* outils NTFS absents : test sans objet */
  TT_RUN(test_ntfs_propre);
  TT_RUN(test_ntfs_secteurs_4096);
  TT_RUN(test_ntfs_sans_etiquette);
  TT_RUN(test_deux_fois);
  TT_RUN(test_refus);
  TT_MAIN_END();
}
