#define _GNU_SOURCE
/* Phase 8, T5 : en-tête LUKS2 écrit par fl_luks2_format, jugé par cryptsetup (luksDump, open
 * --test-passphrase, sans root), puis relu par notre déverrouillage. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/format.h"
#include "fluluks/luks.h"
#include "fsck_util.h"
#include "tt.h"

#define MIB (1024ull * 1024ull)
#define PASS "phrase de passe été"
#define MEM 32768 /* Kio : Argon2 rapide pour les tests (1 Gio dans l'application) */

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

static void fichier_cle(char *path, size_t cap, const char *nom, const char *contenu) {
  char td[4096];
  TT_ASSERT_EQ(fu_tmpdir(td, sizeof td), 0);
  snprintf(path, cap, "%s/%s", td, nom);
  FILE *f = fopen(path, "wb");
  TT_ASSERT(f != NULL);
  fwrite(contenu, 1, strlen(contenu), f);
  fclose(f);
}

static int test_pass(const char *img, const char *contenu) {
  char cle[4096], out[4096];
  fichier_cle(cle, sizeof cle, "cle.txt", contenu);
  char *argv[] = {"env", "LC_ALL=C", "cryptsetup", "open", "--test-passphrase", "--key-file", cle,
                  (char *)img, NULL};
  int rc = fu_run(argv, out, sizeof out);
  unlink(cle);
  return rc;
}

/* Formate une image, écrit un motif par le volume déchiffré, juge l'en-tête par cryptsetup. */
static void cas(uint32_t ss, const char *secteur) {
  char img[4096], out[1 << 14];
  TT_ASSERT_EQ(image(img, sizeof img, "format-luks.img", 64 * MIB), 0);
  fl_bdev *d = NULL, *dec = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(img, ss, false, &d), FL_OK);
  TT_ASSERT_EQ(fl_luks2_format(d, PASS, strlen(PASS), MEM, &dec), FL_OK);
  TT_ASSERT(dec != NULL);
  TT_ASSERT_EQ(dec->sector_size, ss);
  TT_ASSERT_EQ((uint64_t)dec->sector_count * ss, 64 * MIB - 16 * MIB);
  static uint8_t motif[1 << 16], lu[1 << 16];
  for (size_t i = 0; i < sizeof motif; i++) motif[i] = (uint8_t)(i * 7 + 3);
  TT_ASSERT_EQ(fl_bdev_write(dec, 0, (uint32_t)(sizeof motif / ss), motif), FL_OK);
  TT_ASSERT_EQ(fl_bdev_flush(dec), FL_OK);
  fl_bdev_close(dec);
  fl_bdev_close(d);

  char *dump[] = {"env", "LC_ALL=C", "cryptsetup", "luksDump", img, NULL};
  TT_ASSERT_EQ(fu_run(dump, out, sizeof out), 0);
  const char *attendus[] = {"Version:       \t2", "offset: 16777216 [bytes]", "cipher: aes-xts-plain64",
                            secteur, "Key:        512 bits", "PBKDF:      argon2id", "Time cost:  4",
                            "Memory:     32768", "Threads:    4", "AF stripes: 4000", "Hash:       sha256",
                            "Keyslots area: \t16744448 [bytes]"};
  for (size_t i = 0; i < sizeof attendus / sizeof attendus[0]; i++) {
    if (!strstr(out, attendus[i])) {
      fprintf(stderr, "absent : %s\n%s\n", attendus[i], out);
      TT_ASSERT(0);
    }
  }
  TT_ASSERT_EQ(test_pass(img, PASS), 0);
  TT_ASSERT(test_pass(img, "faux") != 0);

  /* relu par notre déverrouillage */
  TT_ASSERT_EQ(fl_file_bdev_open(img, ss, true, &d), FL_OK);
  fl_luks *l = NULL;
  TT_ASSERT_EQ(fl_luks_open(d, &l), FL_OK);
  TT_ASSERT_EQ(fl_luks_version(l), 2);
  TT_ASSERT_EQ(fl_luks_unlock(l, "faux", 4, &dec), FL_EBADPASS);
  TT_ASSERT_EQ(fl_luks_unlock(l, PASS, strlen(PASS), &dec), FL_OK);
  TT_ASSERT_EQ(fl_bdev_read(dec, 0, (uint32_t)(sizeof lu / ss), lu), FL_OK);
  TT_ASSERT_MEM_EQ(lu, motif, sizeof lu);
  fl_bdev_close(dec);
  fl_luks_close(l);
  fl_bdev_close(d);
  unlink(img);
}

static void test_luks2_512(void) { cas(512, "sector: 512 [bytes]"); }
/* Disque à secteurs de 4 096 octets : secteurs chiffrés de 4 096 octets (dm-crypt l'exige). */
static void test_luks2_4096(void) { cas(4096, "sector: 4096 [bytes]"); }

/* Deux formatages : sels, UUID et clé maîtresse différents (aléa). */
static void test_aleas_differents(void) {
  char a[4096], b[4096], oa[1 << 14], ob[1 << 14];
  TT_ASSERT_EQ(image(a, sizeof a, "luks-a.img", 32 * MIB), 0);
  TT_ASSERT_EQ(image(b, sizeof b, "luks-b.img", 32 * MIB), 0);
  const char *imgs[] = {a, b};
  for (int i = 0; i < 2; i++) {
    fl_bdev *d = NULL, *dec = NULL;
    TT_ASSERT_EQ(fl_file_bdev_open(imgs[i], 512, false, &d), FL_OK);
    TT_ASSERT_EQ(fl_luks2_format(d, PASS, strlen(PASS), MEM, &dec), FL_OK);
    fl_bdev_close(dec);
    fl_bdev_close(d);
  }
  char *da[] = {"env", "LC_ALL=C", "cryptsetup", "luksUUID", a, NULL};
  char *db[] = {"env", "LC_ALL=C", "cryptsetup", "luksUUID", b, NULL};
  TT_ASSERT_EQ(fu_run(da, oa, sizeof oa), 0);
  TT_ASSERT_EQ(fu_run(db, ob, sizeof ob), 0);
  TT_ASSERT(strcmp(oa, ob) != 0);
  TT_ASSERT_EQ(strlen(oa), 37); /* 36 caractères et le saut de ligne */
  unlink(a);
  unlink(b);
}

static void test_refus(void) {
  char img[4096];
  TT_ASSERT_EQ(image(img, sizeof img, "luks-refus.img", 16 * MIB), 0);
  fl_bdev *d = NULL, *dec = (fl_bdev *)1;
  TT_ASSERT_EQ(fl_file_bdev_open(img, 512, false, &d), FL_OK);
  TT_ASSERT_EQ(fl_luks2_format(d, PASS, strlen(PASS), MEM, &dec), FL_ENOSPC); /* rien après l'en-tête */
  TT_ASSERT(dec == NULL);
  TT_ASSERT_EQ(fl_luks2_format(d, PASS, 0, MEM, &dec), FL_EINVAL); /* phrase vide */
  TT_ASSERT_EQ(fl_luks2_format(d, PASS, strlen(PASS), 1024, &dec), FL_EINVAL); /* mémoire Argon2 */
  uint8_t s[512], z[512] = {0};
  TT_ASSERT_EQ(fl_bdev_read(d, 0, 1, s), FL_OK);
  TT_ASSERT_MEM_EQ(s, z, sizeof s);
  fl_bdev_close(d);
  unlink(img);
}

int main(void) {
  char out[256];
  char *v[] = {"cryptsetup", "--version", NULL};
  if (fu_run(v, out, sizeof out) != 0) return 0; /* cryptsetup absent : test sans objet */
  TT_RUN(test_luks2_512);
  TT_RUN(test_luks2_4096);
  TT_RUN(test_aleas_differents);
  TT_RUN(test_refus);
  TT_MAIN_END();
}
