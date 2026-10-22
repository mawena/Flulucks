#define _GNU_SOURCE
/* Phase 8, T5 : formatage complet d'un disque (fl_format) : table, LUKS2 si demandé, système de fichiers.
 * Chaque combinaison est jugée par son outil de référence (le contenu chiffré après déchiffrement par le
 * moteur) et par blkid ; les anciennes signatures disparaissent ; un disque trop petit est refusé avant
 * toute écriture. FL_KEEP=1 garde chaque disque formaté (build/native/tmp/format-<fs>-<clair|luks>-<ss>.img)
 * pour tools/fixtures/verify_format_with_kernel.sh (phrase de passe : « phrase de passe »). */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/format.h"
#include "fluluks/partition.h"
#include "fsck_util.h"
#include "tt.h"

#define MIB (1024ull * 1024ull)
#define PASS "phrase de passe"
#define MEM 32768

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

/* Copie creuse de la partition 0 de img (secteurs de ss octets) dans out. */
static void extraire_partition(const char *img, uint32_t ss, char *out, size_t cap) {
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(img, ss, true, &d), FL_OK);
  static fl_part_table t;
  TT_ASSERT_EQ(fl_part_scan(d, &t), FL_OK);
  TT_ASSERT_EQ(t.count, 1);
  uint64_t off = t.parts[0].first_lba * ss, len = t.parts[0].sector_count * ss;
  fl_bdev_close(d);
  TT_ASSERT_EQ(image(out, cap, "partition.img", len), 0);
  int in = open(img, O_RDONLY), o = open(out, O_RDWR);
  TT_ASSERT(in >= 0 && o >= 0);
  static uint8_t buf[1 << 20];
  for (uint64_t p = 0; p < len; p += sizeof buf) {
    size_t n = len - p < sizeof buf ? (size_t)(len - p) : sizeof buf;
    TT_ASSERT_EQ(pread(in, buf, n, (off_t)(off + p)), (ssize_t)n);
    bool nul = true;
    for (size_t i = 0; i < n && nul; i++) nul = buf[i] == 0;
    if (!nul) TT_ASSERT_EQ(pwrite(o, buf, n, (off_t)p), (ssize_t)n);
  }
  close(in);
  close(o);
}

static void blkid_type(const char *img, const char *type) {
  char out[4096], attendu[64];
  char *argv[] = {"blkid", "-p", "-o", "export", (char *)img, NULL};
  TT_ASSERT_EQ(fu_run(argv, out, sizeof out), 0);
  snprintf(attendu, sizeof attendu, "\nTYPE=%s\n", type);
  if (!strstr(out, attendu)) {
    fprintf(stderr, "blkid (%s attendu) :\n%s\n", type, out);
    TT_ASSERT(0);
  }
}

static void juger_fs(const char *part, fl_fmt_fs fs, const char *etiquette) {
  char out[1 << 15];
  switch (fs) {
    case FL_FMT_FAT32:
    case FL_FMT_EXFAT: {
      int rc = fu_fsck_fat_n(part, out, sizeof out);
      if (rc != 0) fprintf(stderr, "%s\n", out);
      TT_ASSERT_EQ(rc, 0);
      blkid_type(part, fs == FL_FMT_FAT32 ? "vfat" : "exfat");
      break;
    }
    case FL_FMT_EXT4: {
      int rc = fu_e2fsck_n(part, out, sizeof out);
      if (rc != 0) fprintf(stderr, "%s\n", out);
      TT_ASSERT_EQ(rc, 0);
      blkid_type(part, "ext4");
      break;
    }
    case FL_FMT_NTFS: {
      int rc = fu_ntfsfix_n(part, out, sizeof out);
      if (rc != 0) fprintf(stderr, "%s\n", out);
      TT_ASSERT_EQ(rc, 0);
      TT_ASSERT_EQ(fu_ntfsjuge(out, sizeof out, "check", part, NULL), 0);
      blkid_type(part, "ntfs");
      break;
    }
  }
  /* -o value : étiquette brute (export échappe les octets non ASCII) */
  char *argv[] = {"blkid", "-p", "-o", "value", "-s", "LABEL", (char *)part, NULL};
  TT_ASSERT_EQ(fu_run(argv, out, sizeof out), 0);
  if (strncmp(out, etiquette, strlen(etiquette)) || out[strlen(etiquette)] != '\n') {
    fprintf(stderr, "étiquette %s :\n%s\n", etiquette, out);
    TT_ASSERT(0);
  }
}

static int g_derniers[2];
static void progres(int pct, void *arg) {
  int *p = arg;
  TT_ASSERT(pct >= p[0] && pct <= 100); /* croissant */
  p[0] = pct;
  p[1]++;
}

/* Formate tout le disque image, puis juge la table, le LUKS2 éventuel et le système de fichiers. */
static void cas(uint32_t ss, uint64_t taille, fl_fmt_fs fs, bool chiffre, const char *etiquette) {
  char img[4096], part[4096], clair[4096], cle[4200];
  TT_ASSERT_EQ(image(img, sizeof img, "format-disque.img", taille), 0);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(img, ss, false, &d), FL_OK);
  fl_fmt_opts o = {.fs = fs, .encrypted = chiffre, .label = etiquette, .kdf_mem_kib = MEM,
                   .root_uid = 1000, .root_gid = 1000};
  g_derniers[0] = 0;
  g_derniers[1] = 0;
  int rc = fl_format(d, &o, chiffre ? PASS : NULL, chiffre ? strlen(PASS) : 0, progres, g_derniers);
  if (rc != FL_OK) fprintf(stderr, "fs %d chiffré %d : %d\n", fs, chiffre, rc);
  TT_ASSERT_EQ(rc, FL_OK);
  TT_ASSERT_EQ(g_derniers[0], 100);
  TT_ASSERT(g_derniers[1] >= 3);
  fl_bdev_close(d);

  extraire_partition(img, ss, part, sizeof part);
  if (chiffre) {
    blkid_type(part, "crypto_LUKS");
    char td[4096];
    TT_ASSERT_EQ(fu_tmpdir(td, sizeof td), 0);
    snprintf(cle, sizeof cle, "%s/cle-format.txt", td);
    FILE *f = fopen(cle, "wb");
    fputs(PASS, f);
    fclose(f);
    TT_ASSERT_EQ(fu_extract_luks_ext4(part, cle, clair, sizeof clair), 0);
    unlink(cle);
  }
  juger_fs(chiffre ? clair : part, fs, etiquette);
  if (chiffre) unlink(clair);
  unlink(part);
  const char *keep = getenv("FL_KEEP");
  if (keep && !strcmp(keep, "1")) {
    static const char *noms[] = {"", "fat32", "exfat", "ext4", "ntfs"};
    char td[4096], garde[4300];
    TT_ASSERT_EQ(fu_tmpdir(td, sizeof td), 0);
    snprintf(garde, sizeof garde, "%s/format-%s-%s-%u.img", td, noms[fs], chiffre ? "luks" : "clair", ss);
    TT_ASSERT_EQ(rename(img, garde), 0);
  } else {
    unlink(img);
  }
}

static void test_chaque_systeme(void) {
  struct { fl_fmt_fs fs; const char *lab; } c[] = {
      {FL_FMT_FAT32, "PORTAIL"}, {FL_FMT_EXFAT, "Clé"}, {FL_FMT_EXT4, "Données"}, {FL_FMT_NTFS, "Disque"}};
  for (int chiffre = 0; chiffre < 2; chiffre++)
    for (size_t i = 0; i < 4; i++) cas(512, 128 * MIB, c[i].fs, chiffre, c[i].lab);
}

static void test_secteurs_4096(void) {
  cas(4096, 128 * MIB, FL_FMT_EXFAT, true, "QUATREK");
  cas(4096, 128 * MIB, FL_FMT_EXT4, false, "QUATREK");
}

/* Ancien LUKS2 au début de la future partition, ancien btrfs, ancien GPT (et sa copie en fin de disque) :
 * après formatage, blkid ne voit que la nouvelle table et le nouveau système de fichiers. */
static void test_anciennes_signatures(void) {
  char img[4096], part[4096], out[4096], cle[4200], ref[4096];
  TT_ASSERT_EQ(image(img, sizeof img, "format-ancien.img", 64 * MIB), 0);
  char *gpt[] = {"sgdisk", "-g", "-o", img, NULL};
  TT_ASSERT_EQ(fu_run(gpt, out, sizeof out), 0);
  /* en-tête LUKS2 de cryptsetup (primaire et secondaire, 32 Kio) recopié à 1 Mio */
  TT_ASSERT_EQ(image(ref, sizeof ref, "ancien-luks.img", 32 * MIB), 0);
  char td[4096];
  TT_ASSERT_EQ(fu_tmpdir(td, sizeof td), 0);
  snprintf(cle, sizeof cle, "%s/cle-ancien.txt", td);
  FILE *f = fopen(cle, "wb");
  fputs("ancienne", f);
  fclose(f);
  char *lf[] = {"cryptsetup", "luksFormat", "-q", "--type", "luks2", "--pbkdf", "pbkdf2",
                "--pbkdf-force-iterations", "1000", "--key-file", cle, ref, NULL};
  TT_ASSERT_EQ(fu_run(lf, out, sizeof out), 0);
  int fr = open(ref, O_RDONLY), fi = open(img, O_RDWR);
  static uint8_t buf[32768];
  TT_ASSERT_EQ(pread(fr, buf, sizeof buf, 0), (ssize_t)sizeof buf);
  TT_ASSERT_EQ(pwrite(fi, buf, sizeof buf, (off_t)MIB), (ssize_t)sizeof buf);
  /* superbloc btrfs (magie à 64 Kio + 0x40 de la partition) */
  TT_ASSERT_EQ(pwrite(fi, "_BHRfS_M", 8, (off_t)(MIB + 65536 + 0x40)), 8);
  close(fr);
  close(fi);
  unlink(ref);
  unlink(cle);

  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(img, 512, false, &d), FL_OK);
  fl_fmt_opts o = {.fs = FL_FMT_EXT4, .label = "Neuf", .kdf_mem_kib = MEM, .root_uid = 1000, .root_gid = 1000};
  TT_ASSERT_EQ(fl_format(d, &o, NULL, 0, NULL, NULL), FL_OK);
  fl_bdev_close(d);
  char *bl[] = {"blkid", "-p", "-o", "export", img, NULL};
  TT_ASSERT_EQ(fu_run(bl, out, sizeof out), 0);
  if (!strstr(out, "PTTYPE=dos")) {
    fprintf(stderr, "%s\n", out);
    TT_ASSERT(0);
  }
  extraire_partition(img, 512, part, sizeof part);
  char *bp[] = {"blkid", "-p", "-o", "export", part, NULL};
  int rc = fu_run(bp, out, sizeof out);
  if (rc != 0 || !strstr(out, "\nTYPE=ext4\n") || strstr(out, "ambivalent")) {
    fprintf(stderr, "rc %d :\n%s\n", rc, out);
    TT_ASSERT(0);
  }
  unlink(part);
  unlink(img);
}

/* Disque trop petit, étiquette trop longue, phrase vide : refus avant toute écriture (image inchangée). */
static void test_refus_avant_ecriture(void) {
  char img[4096];
  TT_ASSERT_EQ(image(img, sizeof img, "format-refus.img", 16 * MIB), 0);
  int fd = open(img, O_RDWR);
  static uint8_t vieux[16 * 1024 * 1024], lu[16 * 1024 * 1024];
  memset(vieux, 0xA5, sizeof vieux);
  TT_ASSERT_EQ(pwrite(fd, vieux, sizeof vieux, 0), (ssize_t)sizeof vieux);
  close(fd);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(img, 512, false, &d), FL_OK);
  fl_fmt_opts o = {.fs = FL_FMT_EXT4, .encrypted = true, .label = "", .kdf_mem_kib = MEM};
  TT_ASSERT_EQ(fl_format(d, &o, PASS, strlen(PASS), NULL, NULL), FL_ENOSPC); /* 16 Mio d'en-tête LUKS2 */
  o.encrypted = false;
  o.fs = FL_FMT_FAT32;
  TT_ASSERT_EQ(fl_format(d, &o, NULL, 0, NULL, NULL), FL_ENOSPC); /* FAT32 : 33 Mio au moins */
  o.fs = FL_FMT_EXT4;
  o.label = "dix-sept octets!!";
  TT_ASSERT_EQ(fl_format(d, &o, NULL, 0, NULL, NULL), FL_EINVAL);
  o.label = "Données";
  o.encrypted = true;
  TT_ASSERT_EQ(fl_format(d, &o, PASS, 0, NULL, NULL), FL_EINVAL); /* phrase vide */
  o.fs = (fl_fmt_fs)9;
  o.encrypted = false;
  TT_ASSERT_EQ(fl_format(d, &o, NULL, 0, NULL, NULL), FL_EINVAL);
  fl_bdev_close(d);
  fd = open(img, O_RDONLY);
  TT_ASSERT_EQ(pread(fd, lu, sizeof lu, 0), (ssize_t)sizeof lu);
  close(fd);
  TT_ASSERT(memcmp(lu, vieux, sizeof lu) == 0);
  unlink(img);
}

int main(void) {
  char out[256];
  char *v[] = {"cryptsetup", "--version", NULL};
  if (!fu_tools_available() || !fu_fat_tools_available() || !fu_ntfs_tools_available() ||
      fu_run(v, out, sizeof out) != 0)
    return 0; /* outils absents : test sans objet */
  TT_RUN(test_chaque_systeme);
  TT_RUN(test_secteurs_4096);
  TT_RUN(test_anciennes_signatures);
  TT_RUN(test_refus_avant_ecriture);
  TT_MAIN_END();
}
