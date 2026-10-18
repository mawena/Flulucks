#define _GNU_SOURCE
/* Banc de non-corruption (phase 3, T3) : teste le banc lui-même (images d'écriture, juges e2fsck et
 * debugfs, modèle d'arborescence, périphérique à pannes). Aucune écriture du moteur ici : il n'y en a
 * pas encore. Réservé au PC : sauté si e2fsck/debugfs sont absents, et sur Android. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fault_bdev.h"
#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fsck_util.h"
#include "tree_model.h"
#include "tt.h"

static const char *const IMAGES[] = {"ext4-rw-reel.img",   "ext4-rw-1k.img",      "ext4-rw-uninit.img",
                                     "ext4-rw-seed.img",   "ext4-rw-sans-journal.img",
                                     "ext4-rw-meta-bg.img", "ext4-rw-gros.img"};
#define NIMG (sizeof IMAGES / sizeof IMAGES[0])

/* Fichiers temporaires à supprimer à la fin, même si un test a échoué en route. */
static char *temporaires[64];
static size_t ntemp;

static void noter(const char *chemin) {
  if (ntemp < 64) temporaires[ntemp++] = strdup(chemin);
}

static void nettoyer(void) {
  for (size_t i = 0; i < ntemp; i++) {
    unlink(temporaires[i]);
    free(temporaires[i]);
  }
  ntemp = 0;
}

/* Copie d'une fixture dans le dossier temporaire du banc ; vide en cas d'échec. */
static int copie_de(const char *fixture, char *out, size_t cap) {
  char src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture(fixture));
  if (fu_copy_image(src, out, cap) != 0) return -1;
  noter(out);
  return 0;
}

static void test_banc_image_propre(void) {
  for (size_t i = 0; i < NIMG; i++) {
    char copie[4096], out[8192];
    TT_ASSERT(copie_de(IMAGES[i], copie, sizeof copie) == 0);
    int rc = fu_e2fsck_n(copie, out, sizeof out);
    if (rc != 0) fprintf(stderr, "%s : e2fsck -fn = %d\n%s\n", IMAGES[i], rc, out);
    TT_ASSERT_EQ(rc, 0);
    unlink(copie);
  }
}

static void test_banc_luks_extrait(void) {
  char ext[4096], out[8192], pass[4096], hello[64];
  snprintf(pass, sizeof pass, "%s", tt_fixture("pass.txt"));
  char luks[4096];
  snprintf(luks, sizeof luks, "%s", tt_fixture("luks2-rw-reel.img"));
  TT_ASSERT(fu_extract_luks_ext4(luks, pass, ext, sizeof ext) == 0);
  noter(ext);
  TT_ASSERT_EQ(fu_e2fsck_n(ext, out, sizeof out), 0);
  TT_ASSERT_EQ(fu_debugfs_cat(ext, "/hello.txt", hello, sizeof hello), 16);
  TT_ASSERT_MEM_EQ(hello, "Bonjour FluLuks\n", 16);
  /* Même contenu que l'image claire dont elle est issue. */
  char clair[4096];
  TT_ASSERT(copie_de("ext4-rw-reel.img", clair, sizeof clair) == 0);
  char *const cmp[] = {(char *)"cmp", ext, clair, NULL};
  TT_ASSERT_EQ(fu_run(cmp, out, sizeof out), 0);
  unlink(ext);
  unlink(clair);
}

/* Numéro du bloc du bitmap de blocs du groupe 0 et taille de bloc, d'après dumpe2fs. */
static int bitmap_groupe0(const char *img, uint64_t *blk, uint32_t *bs) {
  char out[1 << 16];
  char *const argv[] = {(char *)"dumpe2fs", (char *)img, NULL};
  if (fu_run(argv, out, sizeof out) != 0) return -1;
  const char *b = strstr(out, "Block size:");
  const char *g = strstr(out, "Block bitmap at ");
  unsigned long long n;
  unsigned s;
  if (!b || !g || sscanf(b, "Block size: %u", &s) != 1 || sscanf(g, "Block bitmap at %llu", &n) != 1) return -1;
  *blk = n;
  *bs = s;
  return 0;
}

static void test_banc_detecte_corruption(void) {
  char copie[4096], out[1 << 16];
  TT_ASSERT(copie_de("ext4-rw-reel.img", copie, sizeof copie) == 0);
  TT_ASSERT_EQ(fu_e2fsck_n(copie, out, sizeof out), 0);
  uint64_t blk;
  uint32_t bs;
  TT_ASSERT(bitmap_groupe0(copie, &blk, &bs) == 0);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(copie, 512, false, &d), FL_OK);
  static uint8_t zeros[65536];
  int rc = fl_bdev_write(d, blk * bs / 512, bs / 512, zeros);
  fl_bdev_close(d);
  TT_ASSERT_EQ(rc, FL_OK);
  int fsck = fu_e2fsck_n(copie, out, sizeof out);
  TT_ASSERT(fsck != 0);
  TT_ASSERT(out[0] != 0);
  unlink(copie);
}

static void test_banc_debugfs_lit(void) {
  char copie[4096], buf[64];
  TT_ASSERT(copie_de("ext4-rw-reel.img", copie, sizeof copie) == 0);
  TT_ASSERT_EQ(fu_debugfs_cat(copie, "/hello.txt", buf, sizeof buf), 16);
  TT_ASSERT_MEM_EQ(buf, "Bonjour FluLuks\n", 16);
  TT_ASSERT_EQ(fu_debugfs_cat(copie, "/vide.txt", buf, sizeof buf), 0);
  TT_ASSERT_EQ(fu_debugfs_cat(copie, "/accentué é ☀.txt", buf, sizeof buf), 5);
  TT_ASSERT_MEM_EQ(buf, "utf8\n", 5);
  TT_ASSERT(fu_debugfs_cat(copie, "/inexistant", buf, sizeof buf) < 0);
  TT_ASSERT(fu_debugfs_cat(copie, "/hello.txt", buf, 8) < 0); /* plus grand que cap */
  uint64_t v;
  TT_ASSERT_EQ(fu_debugfs_inode_field(copie, "/hello.txt", "size", &v), 0);
  TT_ASSERT_EQ(v, 16);
  TT_ASSERT_EQ(fu_debugfs_inode_field(copie, "/hello.txt", "uid", &v), 0);
  TT_ASSERT_EQ(v, getuid());
  TT_ASSERT_EQ(fu_debugfs_inode_field(copie, "/hello.txt", "gid", &v), 0);
  TT_ASSERT_EQ(v, getgid());
  TT_ASSERT_EQ(fu_debugfs_inode_field(copie, "/hello.txt", "mode", &v), 0);
  TT_ASSERT((v & ~0777ull) == 0 && (v & 0600) == 0600);
  TT_ASSERT_EQ(fu_debugfs_inode_field(copie, "/hello.txt", "mtime", &v), 0);
  TT_ASSERT(v > 1600000000ull);
  TT_ASSERT_EQ(fu_debugfs_inode_field(copie, "/hello.txt", "crtime", &v), 0);
  TT_ASSERT(fu_debugfs_inode_field(copie, "/inexistant", "uid", &v) < 0);
  TT_ASSERT(fu_debugfs_inode_field(copie, "/hello.txt", "inconnu", &v) < 0);
  unlink(copie);
}

static void test_banc_panne(void) {
  char copie[4096];
  TT_ASSERT(copie_de("ext4-rw-reel.img", copie, sizeof copie) == 0);
  fl_bdev *file = NULL, *parent = NULL, *f = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(copie, 512, false, &file), FL_OK);
  /* Le parent compte lui-même ce qu'il reçoit (writes_ok = jamais). */
  TT_ASSERT_EQ(fl_fault_bdev_new(file, UINT64_MAX, FL_EIO, &parent), FL_OK);
  TT_ASSERT_EQ(fl_fault_bdev_new(parent, 2, FL_EIO, &f), FL_OK);
  uint8_t a[512], b[512], c[512], r[512];
  memset(a, 0xA1, sizeof a);
  memset(b, 0xB2, sizeof b);
  memset(c, 0xC3, sizeof c);
  TT_ASSERT_EQ(fl_bdev_write(f, 1, 1, a), FL_OK);
  TT_ASSERT_EQ(fl_bdev_write(f, 2, 1, b), FL_OK);
  TT_ASSERT_EQ(fl_bdev_write(f, 3, 1, c), FL_EIO);
  TT_ASSERT_EQ(fl_bdev_write(f, 3, 1, c), FL_EIO); /* la panne reste */
  TT_ASSERT_EQ(fl_fault_bdev_writes(f), 2);
  TT_ASSERT_EQ(fl_fault_bdev_writes(parent), 2);
  /* Lectures : continuent ; la troisième écriture n'a rien écrit. */
  TT_ASSERT_EQ(fl_bdev_read(f, 1, 1, r), FL_OK);
  TT_ASSERT_MEM_EQ(r, a, 512);
  TT_ASSERT_EQ(fl_bdev_read(f, 2, 1, r), FL_OK);
  TT_ASSERT_MEM_EQ(r, b, 512);
  TT_ASSERT_EQ(fl_bdev_read(f, 3, 1, r), FL_OK);
  TT_ASSERT(r[0] != 0xC3);
  /* Flush comptés, et transmis. */
  TT_ASSERT_EQ(fl_fault_bdev_flushes(f), 0);
  TT_ASSERT_EQ(fl_bdev_flush(f), FL_OK);
  TT_ASSERT_EQ(fl_bdev_flush(f), FL_OK);
  TT_ASSERT_EQ(fl_fault_bdev_flushes(f), 2);
  TT_ASSERT_EQ(fl_fault_bdev_flushes(parent), 2);
  fl_bdev_close(f);
  /* Variante FL_ENODEV, et writes_ok = 0 : rien n'est jamais écrit. */
  TT_ASSERT_EQ(fl_fault_bdev_new(parent, 0, FL_ENODEV, &f), FL_OK);
  TT_ASSERT_EQ(fl_bdev_write(f, 4, 1, c), FL_ENODEV);
  TT_ASSERT_EQ(fl_fault_bdev_writes(f), 0);
  fl_bdev_close(f);
  fl_bdev_close(parent);
  fl_bdev_close(file);
  unlink(copie);
}

static void test_banc_modele_operations(void) {
  tm *m = tm_new();
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(tm_count(m), 1);
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/d", 1, 2, 0755), 0);
  TT_ASSERT(tm_apply_mkdir(m, "/d", 1, 2, 0755) != 0);       /* déjà pris */
  TT_ASSERT(tm_apply_mkdir(m, "/x/y", 1, 2, 0755) != 0);     /* parent absent */
  TT_ASSERT_EQ(tm_apply_file(m, "/d/f", "abc", 3, 1, 2, 0644), 0);
  TT_ASSERT_EQ(tm_apply_file(m, "/d/f", "abcdef", 6, 1, 2, 0644), 0); /* remplace */
  TT_ASSERT_EQ(tm_count(m), 3);
  TT_ASSERT(tm_apply_rmdir(m, "/d") != 0);                   /* non vide */
  TT_ASSERT_EQ(tm_apply_mkdir(m, "/e", 0, 0, 0700), 0);
  TT_ASSERT_EQ(tm_apply_rename(m, "/d", "/e/d2"), 0);
  TT_ASSERT(tm_apply_rename(m, "/e", "/e/d2/x") != 0);       /* dans soi-même */
  TT_ASSERT_EQ(tm_apply_attrs(m, "/e/d2/f", 5, 6, 0600), 0);
  TT_ASSERT_EQ(tm_apply_unlink(m, "/e/d2/f"), 0);
  TT_ASSERT(tm_apply_unlink(m, "/e/d2/f") != 0);
  TT_ASSERT_EQ(tm_apply_rmdir(m, "/e/d2"), 0);
  TT_ASSERT_EQ(tm_apply_rmdir(m, "/e"), 0);
  TT_ASSERT_EQ(tm_count(m), 1);
  tm_free(m);
}

/* Monte une copie en lecture seule par le moteur ; *dev à fermer après démontage. */
static fl_fs *monter(const char *copie, fl_bdev **dev) {
  fl_fs *fs = NULL;
  if (fl_file_bdev_open(copie, 512, true, dev) != FL_OK) return NULL;
  if (fl_fs_mount(*dev, FL_CONTENT_EXT4, false, &fs) != FL_OK) {
    fl_bdev_close(*dev);
    return NULL;
  }
  return fs;
}

static void test_banc_modele(void) {
  for (size_t i = 0; i < NIMG; i++) {
    char copie[4096], tree[4096];
    TT_ASSERT(copie_de(IMAGES[i], copie, sizeof copie) == 0);
    snprintf(tree, sizeof tree, "%s", tt_fixture("tree"));
    tm *m = tm_new();
    TT_ASSERT(m != NULL && tm_from_dir(m, tree) == 0);
    TT_ASSERT(tm_count(m) > 300);
    fl_bdev *dev = NULL;
    fl_fs *fs = monter(copie, &dev);
    /* metadata_csum_seed + orphan_file (options par défaut d'e2fsprogs ≥ 1.47) : le moteur refuse de
     * monter en phase 3 ; on ne juge alors que par debugfs. */
    int sans_moteur = !strcmp(IMAGES[i], "ext4-rw-seed.img");
    if (!fs && !sans_moteur) fprintf(stderr, "%s : montage par le moteur impossible\n", IMAGES[i]);
    TT_ASSERT(fs != NULL || sans_moteur);
    int ecarts = fs ? tm_check_fl(m, fs) : 0;
    if (ecarts) fprintf(stderr, "%s : %d écarts (moteur)\n", IMAGES[i], ecarts);
    TT_ASSERT_EQ(ecarts, 0);
    ecarts = tm_check_debugfs(m, copie);
    if (ecarts) fprintf(stderr, "%s : %d écarts (debugfs)\n", IMAGES[i], ecarts);
    TT_ASSERT_EQ(ecarts, 0);

    /* Le modèle n'est pas aveugle : un écart de contenu, un fichier en trop, un manquant, un attribut. */
    tm_set_quiet(1);
#define DIFFERENT(mod) ((!fs || tm_check_fl(mod, fs) > 0) && tm_check_debugfs(mod, copie) > 0)
#define IDENTIQUE(mod) ((!fs || tm_check_fl(mod, fs) == 0) && tm_check_debugfs(mod, copie) == 0)
    TT_ASSERT_EQ(tm_apply_file(m, "/hello.txt", "Bonjour FluLuk5\n", 16, getuid(), getgid(), 0664), 0);
    int ok1 = DIFFERENT(m);
    tm_free(m);
    m = tm_new();
    TT_ASSERT(m != NULL && tm_from_dir(m, tree) == 0);
    TT_ASSERT_EQ(tm_apply_unlink(m, "/hello.txt"), 0); /* sur le volume : fichier en trop */
    int ok2 = DIFFERENT(m);
    TT_ASSERT_EQ(tm_apply_file(m, "/hello.txt", "Bonjour FluLuks\n", 16, getuid(), getgid(), 0664), 0);
    TT_ASSERT_EQ(tm_apply_file(m, "/fantome", "x", 1, getuid(), getgid(), 0664), 0); /* manquant */
    int ok3 = DIFFERENT(m);
    TT_ASSERT_EQ(tm_apply_unlink(m, "/fantome"), 0);
    int ok4 = IDENTIQUE(m); /* remis à l'identique */
    TT_ASSERT_EQ(tm_apply_attrs(m, "/hello.txt", getuid() + 1, getgid(), 0664), 0);
    int ok5 = DIFFERENT(m);
#undef DIFFERENT
#undef IDENTIQUE
    tm_set_quiet(0);
    TT_ASSERT(ok1 && ok2 && ok3 && ok4 && ok5);
    tm_free(m);
    if (fs) {
      fl_fs_unmount(fs);
      fl_bdev_close(dev);
    }
    unlink(copie);
  }
}

static void test_banc_journal_v3(void) {
  for (size_t i = 0; i < NIMG; i++) {
    char out[1 << 15];
    char img[4096];
    snprintf(img, sizeof img, "%s", tt_fixture(IMAGES[i]));
    char *const argv[] = {(char *)"dumpe2fs", (char *)"-h", img, NULL};
    TT_ASSERT_EQ(fu_run(argv, out, sizeof out), 0);
    if (!strcmp(IMAGES[i], "ext4-rw-sans-journal.img")) {
      TT_ASSERT(strstr(out, "has_journal") == NULL);
      TT_ASSERT(strstr(out, "Journal features") == NULL);
    } else if (!strcmp(IMAGES[i], "ext4-rw-uninit.img")) {
      /* Sans metadata_csum, e2fsprogs pose la somme v1 (ancien disque) et jamais la v3. */
      TT_ASSERT(strstr(out, "journal_incompat_revoke journal_64bit") != NULL);
      TT_ASSERT(strstr(out, "journal_checksum ") != NULL || strstr(out, "journal_checksum\n") != NULL);
      TT_ASSERT(strstr(out, "journal_checksum_v3") == NULL);
    } else {
      TT_ASSERT(strstr(out, "journal_incompat_revoke journal_64bit journal_checksum_v3") != NULL);
    }
    TT_ASSERT(strstr(out, "needs_recovery") == NULL);
    TT_ASSERT(strstr(out, "Filesystem state:         clean") != NULL);
  }
}

/* fu_e2fsck_recover : une image avec journal à rejouer est rejouée, puis propre. */
static void test_banc_recover(void) {
  char copie[4096], out[8192], td[4096], cmd[4300];
  TT_ASSERT(copie_de("ext4-rw-1k.img", copie, sizeof copie) == 0);
  TT_ASSERT_EQ(fu_tmpdir(td, sizeof td), 0);
  snprintf(cmd, sizeof cmd, "%s/jcmd-XXXXXX", td);
  int fd = mkstemp(cmd);
  TT_ASSERT(fd >= 0);
  noter(cmd);
  static const char t[] = "jo -c\njw -r 100\njc\n";
  TT_ASSERT(write(fd, t, sizeof t - 1) == (ssize_t)(sizeof t - 1));
  close(fd);
  char *const argv[] = {(char *)"debugfs", (char *)"-w", (char *)"-f", cmd, copie, NULL};
  TT_ASSERT_EQ(fu_run(argv, out, sizeof out), 0);
  /* Journal non vide : e2fsck -fn le voit (needs_recovery) et le dit. */
  char *const dump[] = {(char *)"dumpe2fs", (char *)"-h", copie, NULL};
  TT_ASSERT_EQ(fu_run(dump, out, sizeof out), 0);
  TT_ASSERT(strstr(out, "needs_recovery") != NULL);
  int replayed = 0;
  TT_ASSERT_EQ(fu_e2fsck_recover(copie, &replayed, out, sizeof out), 0);
  TT_ASSERT_EQ(replayed, 1);
  /* Deuxième passage : plus rien à rejouer. */
  TT_ASSERT_EQ(fu_e2fsck_recover(copie, &replayed, out, sizeof out), 0);
  TT_ASSERT_EQ(replayed, 0);
  unlink(copie);
  unlink(cmd);
}

int main(void) {
  if (!fu_tools_available()) {
    printf("SAUTÉ : e2fsck/debugfs/dumpe2fs absents ou plateforme Android (banc réservé au PC)\n");
    return 0;
  }
  TT_RUN(test_banc_image_propre);
  TT_RUN(test_banc_detecte_corruption);
  TT_RUN(test_banc_debugfs_lit);
  TT_RUN(test_banc_panne);
  TT_RUN(test_banc_modele_operations);
  TT_RUN(test_banc_modele);
  TT_RUN(test_banc_journal_v3);
  TT_RUN(test_banc_recover);
  TT_RUN(test_banc_luks_extrait);
  nettoyer();
  TT_MAIN_END();
}
