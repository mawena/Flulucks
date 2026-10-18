#define _GNU_SOURCE
/* Banc de non-corruption (phase 3, T3) : teste le banc lui-même (images d'écriture, juges e2fsck et
 * debugfs, modèle d'arborescence, périphérique à pannes). Aucune écriture du moteur ici : il n'y en a
 * pas encore. Réservé au PC : sauté si e2fsck/debugfs sont absents, et sur Android. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
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

/* Met à zéro le bitmap de blocs du groupe 0 de la copie. */
static int abimer_bitmap(const char *copie) {
  uint64_t blk;
  uint32_t bs;
  if (bitmap_groupe0(copie, &blk, &bs) != 0) return -1;
  fl_bdev *d = NULL;
  if (fl_file_bdev_open(copie, 512, false, &d) != FL_OK) return -1;
  static uint8_t zeros[65536];
  int rc = fl_bdev_write(d, blk * bs / 512, bs / 512, zeros);
  fl_bdev_close(d);
  return rc == FL_OK ? 0 : -1;
}

static void test_banc_detecte_corruption(void) {
  char copie[4096], out[1 << 16];
  TT_ASSERT(copie_de("ext4-rw-reel.img", copie, sizeof copie) == 0);
  TT_ASSERT_EQ(fu_e2fsck_n(copie, out, sizeof out), 0);
  TT_ASSERT_EQ(abimer_bitmap(copie), 0);
  int fsck = fu_e2fsck_n(copie, out, sizeof out);
  if (fsck != 4 && fsck != 12) fprintf(stderr, "e2fsck -fn = %d\n%s\n", fsck, out);
  TT_ASSERT(fsck == 4 || fsck == 12); /* erreurs laissées sans correction */
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
  TT_ASSERT_EQ(fl_fault_bdev_new(parent, 1, 0, &f), FL_EINVAL); /* err doit être négatif */
  TT_ASSERT_EQ(fl_fault_bdev_new(parent, 1, 5, &f), FL_EINVAL);
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
  char tree[4096];
  snprintf(tree, sizeof tree, "%s", tt_fixture("tree"));
  tm *base = tm_new(); /* condensats de tree/ calculés une fois (clairsemé.bin : 5 Gio lus en entier) */
  TT_ASSERT(base != NULL && tm_from_dir(base, tree) == 0);
  TT_ASSERT(tm_count(base) > 300);
  for (size_t i = 0; i < NIMG; i++) {
    char copie[4096];
    TT_ASSERT(copie_de(IMAGES[i], copie, sizeof copie) == 0);
    tm *m = tm_clone(base);
    TT_ASSERT(m != NULL);
    fl_bdev *dev = NULL;
    fl_fs *fs = monter(copie, &dev);
    /* metadata_csum_seed + orphan_file (options par défaut d'e2fsprogs ≥ 1.47) : monté depuis le
     * correctif lwext4 0005 ; toutes les images passent donc aussi par le moteur. */
    int sans_moteur = 0;
    if (!fs && !sans_moteur) fprintf(stderr, "%s : montage par le moteur impossible\n", IMAGES[i]);
    TT_ASSERT(fs != NULL || sans_moteur);
    int ecarts = fs ? tm_check_fl(m, fs) : 0;
    if (ecarts) fprintf(stderr, "%s : %d écarts (moteur)\n", IMAGES[i], ecarts);
    TT_ASSERT_EQ(ecarts, 0);
    ecarts = tm_check_debugfs(m, copie);
    if (ecarts) fprintf(stderr, "%s : %d écarts (debugfs)\n", IMAGES[i], ecarts);
    TT_ASSERT_EQ(ecarts, 0);

    /* Le modèle n'est pas aveugle : un écart de contenu, un fichier en trop, un manquant, un attribut.
     * Vérifié sur la première image seulement (chaque contrôle relit les 5 Gio de clairsemé.bin). */
    if (i != 0) {
      tm_free(m);
      if (fs) {
        fl_fs_unmount(fs);
        fl_bdev_close(dev);
      }
      unlink(copie);
      continue;
    }
    /* droits des fichiers de tree/ : ceux de l'umask de make_fixtures.sh (0664 ou 0644) */
    char ph[4200];
    struct stat sh;
    snprintf(ph, sizeof ph, "%s/hello.txt", tree);
    TT_ASSERT_EQ(stat(ph, &sh), 0);
    const unsigned mf = sh.st_mode & 07777;
    tm_set_quiet(1);
#define DIFFERENT(mod) ((!fs || tm_check_fl(mod, fs) > 0) && tm_check_debugfs(mod, copie) > 0)
#define IDENTIQUE(mod) ((!fs || tm_check_fl(mod, fs) == 0) && tm_check_debugfs(mod, copie) == 0)
    TT_ASSERT_EQ(tm_apply_file(m, "/hello.txt", "Bonjour FluLuk5\n", 16, getuid(), getgid(), mf), 0);
    int ok1 = DIFFERENT(m);
    tm_free(m);
    m = tm_clone(base);
    TT_ASSERT(m != NULL);
    TT_ASSERT_EQ(tm_apply_unlink(m, "/hello.txt"), 0); /* sur le volume : fichier en trop */
    int ok2 = DIFFERENT(m);
    TT_ASSERT_EQ(tm_apply_file(m, "/hello.txt", "Bonjour FluLuks\n", 16, getuid(), getgid(), mf), 0);
    TT_ASSERT_EQ(tm_apply_file(m, "/fantome", "x", 1, getuid(), getgid(), mf), 0); /* manquant */
    int ok3 = DIFFERENT(m);
    TT_ASSERT_EQ(tm_apply_unlink(m, "/fantome"), 0);
    int ok4 = IDENTIQUE(m); /* remis à l'identique */
    TT_ASSERT_EQ(tm_apply_attrs(m, "/hello.txt", getuid() + 1, getgid(), mf), 0);
    int ok5 = DIFFERENT(m);
    /* gros.bin (5 Mio, plusieurs tranches) : le même contenu en mémoire donne le même condensat que le
     * fichier lu sur disque ; un octet changé au milieu est vu. */
    TT_ASSERT_EQ(tm_apply_attrs(m, "/hello.txt", getuid(), getgid(), mf), 0);
    uint8_t *gros = malloc((size_t)5 << 20);
    TT_ASSERT(gros != NULL);
    for (size_t k = 0; k < ((size_t)5 << 20); k++) gros[k] = (uint8_t)((k * 31 + 7) & 0xFF);
    TT_ASSERT_EQ(tm_apply_file(m, "/gros.bin", gros, (size_t)5 << 20, getuid(), getgid(), mf), 0);
    int ok6 = IDENTIQUE(m);
    gros[(size_t)2500000] ^= 1;
    TT_ASSERT_EQ(tm_apply_file(m, "/gros.bin", gros, (size_t)5 << 20, getuid(), getgid(), mf), 0);
    int ok7 = DIFFERENT(m);
    free(gros);
#undef DIFFERENT
#undef IDENTIQUE
    tm_set_quiet(0);
    TT_ASSERT(ok1 && ok2 && ok3 && ok4 && ok5 && ok6 && ok7);
    tm_free(m);
    if (fs) {
      fl_fs_unmount(fs);
      fl_bdev_close(dev);
    }
    unlink(copie);
  }
  tm_free(base);
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
      /* Sans metadata_csum : journal sans somme (la v1 est refusée en écriture, voir make_fixtures.sh). */
      TT_ASSERT(strstr(out, "journal_incompat_revoke journal_64bit") != NULL);
      TT_ASSERT(strstr(out, "journal_checksum") == NULL);
    } else {
      TT_ASSERT(strstr(out, "journal_incompat_revoke journal_64bit journal_checksum_v3") != NULL);
    }
    TT_ASSERT(strstr(out, "needs_recovery") == NULL);
    TT_ASSERT(strstr(out, "Filesystem state:         clean") != NULL);
  }
}

/* Pose sur la copie un journal à rejouer (debugfs jo -c / jw -r 100 / jc), SANS e2fsck -fy. */
static int poser_journal(const char *copie) {
  char td[4096], cmd[4300], out[8192];
  if (fu_tmpdir(td, sizeof td) != 0) return -1;
  snprintf(cmd, sizeof cmd, "%s/jcmd-XXXXXX", td);
  int fd = mkstemp(cmd);
  if (fd < 0) return -1;
  noter(cmd);
  static const char t[] = "jo -c\njw -r 100\njc\n";
  int ok = write(fd, t, sizeof t - 1) == (ssize_t)(sizeof t - 1);
  close(fd);
  char *const argv[] = {(char *)"debugfs", (char *)"-w", (char *)"-f", cmd, (char *)copie, NULL};
  ok = ok && fu_run(argv, out, sizeof out) == 0;
  unlink(cmd);
  char *const dump[] = {(char *)"dumpe2fs", (char *)"-h", (char *)copie, NULL};
  return ok && fu_run(dump, out, sizeof out) == 0 && strstr(out, "needs_recovery") ? 0 : -1;
}

/* Le piège : « e2fsck -fn » ignore le journal à rejouer et sort avec 0. Le juge doit refuser. */
static void test_banc_juge_journal_non_rejoue(void) {
  char copie[4096], out[8192];
  TT_ASSERT(copie_de("ext4-rw-1k.img", copie, sizeof copie) == 0);
  TT_ASSERT_EQ(poser_journal(copie), 0);
  char *const brut[] = {(char *)"e2fsck", (char *)"-fn", copie, NULL};
  TT_ASSERT_EQ(fu_run(brut, out, sizeof out), 0); /* l'outil lui-même dit 0... */
  TT_ASSERT(strstr(out, "skipping journal recovery") != NULL);
  TT_ASSERT_EQ(fu_e2fsck_n(copie, out, sizeof out), FU_RC_JOURNAL); /* ... pas le juge */
  unlink(copie);
}

static void test_banc_recover(void) {
  char copie[4096], out[8192], fy[8192];
  TT_ASSERT(copie_de("ext4-rw-1k.img", copie, sizeof copie) == 0);
  TT_ASSERT_EQ(poser_journal(copie), 0);
  int replayed = 0, fy_rc = -1;
  TT_ASSERT_EQ(fu_e2fsck_recover(copie, &replayed, &fy_rc, fy, sizeof fy, out, sizeof out), 0);
  TT_ASSERT_EQ(replayed, 1);
  TT_ASSERT(fy_rc >= 0 && fy_rc < 4);
  TT_ASSERT(strstr(fy, "recovering journal") != NULL);
  /* Deuxième passage : plus rien à rejouer. */
  TT_ASSERT_EQ(fu_e2fsck_recover(copie, &replayed, &fy_rc, NULL, 0, out, sizeof out), 0);
  TT_ASSERT_EQ(replayed, 0);
  unlink(copie);
}

/* Les réparations de -fy ne sont pas cachées : un bitmap abîmé est réparé par -fy (le -fn qui suit est
 * propre), mais fu_e2fsck_recover refuse. */
static void test_banc_recover_refuse_reparation(void) {
  char copie[4096], out[8192], fy[1 << 16];
  TT_ASSERT(copie_de("ext4-rw-reel.img", copie, sizeof copie) == 0);
  TT_ASSERT_EQ(abimer_bitmap(copie), 0);
  int replayed = 0, fy_rc = -1;
  int rc = fu_e2fsck_recover(copie, &replayed, &fy_rc, fy, sizeof fy, out, sizeof out);
  if (rc != FU_RC_REPAIRED) fprintf(stderr, "rc=%d fy_rc=%d\n%s\n", rc, fy_rc, fy);
  TT_ASSERT_EQ(rc, FU_RC_REPAIRED);
  TT_ASSERT(fy_rc == 1 || fy_rc == 2); /* -fy a corrigé quelque chose */
  TT_ASSERT_EQ(fu_e2fsck_n(copie, out, sizeof out), 0); /* et l'image est désormais propre */
  unlink(copie);
}

static void test_banc_fy_benin(void) {
  static const char benin[] =
      "reel.img: recovering journal\n"
      "Pass 1: Checking inodes, blocks, and sizes\n"
      "Pass 5: Checking group summary information\n"
      "Free blocks count wrong (3000, counted=2999).\nFix? yes\n\n"
      "Free inodes count wrong (4000, counted=3999).\nFix? yes\n\n"
      "reel.img: ***** FILE SYSTEM WAS MODIFIED *****\n";
  TT_ASSERT(fu_fy_repairs_benign(benin));
  TT_ASSERT(fu_fy_repairs_benign(""));
  TT_ASSERT(fu_fy_repairs_benign("reel.img: recovering journal\nreel.img: 319/4096 files\n"));
  static const char *const mauvais[] = {
      "Block bitmap differences:  -(1--2)\nFix? yes\n\n",
      "Inode 12 ref count is 2, should be 1.  Fix? yes\n",
      "Free blocks count wrong for group #3 (10, counted=9).\nFix? yes\n\n",
      "Free inodes count wrong for group #0 (10, counted=9).\nFix? yes\n",
      "Entry 'x' in / (2) references inode 99 found in group 0's unused inode area.\nClear? yes\n",
      "Free blocks count wrong (3000, counted=2999).\nFix? yes\n\nBlock bitmap differences:  +5\nFix? yes\n",
      "Inode 12 was part of the orphaned inode list.  FIXED.\n",
      "Clearing orphaned inode 12 (uid=0, gid=0, mode=0100644, size=0)\n",
  };
  for (size_t i = 0; i < sizeof mauvais / sizeof *mauvais; i++) {
    if (fu_fy_repairs_benign(mauvais[i])) fprintf(stderr, "cas %zu jugé bénin : %s\n", i, mauvais[i]);
    TT_ASSERT(!fu_fy_repairs_benign(mauvais[i]));
  }
}

static void test_banc_outils_delai(void) {
  char out[256];
  char *const dort[] = {(char *)"sleep", (char *)"30", NULL};
  time_t t0 = time(NULL);
  TT_ASSERT_EQ(fu_run_timeout(dort, out, sizeof out, 1), FU_RC_TIMEOUT);
  TT_ASSERT(time(NULL) - t0 < 10);
  char *const vrai[] = {(char *)"echo", (char *)"ok", NULL};
  TT_ASSERT_EQ(fu_run_timeout(vrai, out, sizeof out, 30), 0);
  TT_ASSERT_STR_EQ(out, "ok\n");
  char *const absent[] = {(char *)"outil-qui-n-existe-pas", NULL};
  TT_ASSERT(fu_run(absent, out, sizeof out) != 0);
}

int main(void) {
  if (!fu_tools_available()) {
    printf("SAUTÉ : e2fsck/debugfs/dumpe2fs absents ou plateforme Android (banc réservé au PC)\n");
    return 77; /* SKIP_RETURN_CODE de CTest */
  }
  TT_RUN(test_banc_image_propre);
  TT_RUN(test_banc_detecte_corruption);
  TT_RUN(test_banc_debugfs_lit);
  TT_RUN(test_banc_panne);
  TT_RUN(test_banc_modele_operations);
  TT_RUN(test_banc_modele);
  TT_RUN(test_banc_journal_v3);
  TT_RUN(test_banc_juge_journal_non_rejoue);
  TT_RUN(test_banc_recover);
  TT_RUN(test_banc_recover_refuse_reparation);
  TT_RUN(test_banc_fy_benin);
  TT_RUN(test_banc_outils_delai);
  TT_RUN(test_banc_luks_extrait);
  nettoyer();
  TT_MAIN_END();
}
