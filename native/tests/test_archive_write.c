#define _GNU_SOURCE
/* Phase 9, T3 : écriture de ZIP (fl_arcw_*), jugée par des outils indépendants : `unzip -t`, `zip -T` et
 * `zipfile` de Python (noms, drapeau UTF-8, dates, contenus), puis relue par notre lecteur. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "fluluks/archive.h"
#include "fluluks/error.h"
#include "fsck_util.h"
#include "tt.h"

#define MIB (1024 * 1024)

static int ecrire_fd(void *ctx, const void *buf, size_t len) {
  int fd = *(int *)ctx;
  size_t done = 0;
  while (done < len) {
    ssize_t n = write(fd, (const char *)buf + done, len - done);
    if (n <= 0) return FL_EIO;
    done += (size_t)n;
  }
  return FL_OK;
}

static int64_t lire_fd(void *ctx, void *buf, size_t len, uint64_t off) {
  ssize_t n = pread(*(int *)ctx, buf, len, (off_t)off);
  return n < 0 ? FL_EIO : n;
}

/* Chemin d'un fichier temporaire du test, dans le dossier de construction. */
static const char *tmp(const char *nom) {
  static char buf[4200];
  char td[4096];
  if (fu_tmpdir(td, sizeof td) != 0) return "/nonexistent";
  snprintf(buf, sizeof buf, "%s/%s", td, nom);
  return buf;
}

typedef struct {
  int fd;
  fl_arc_out out;
  fl_arc_writer *w;
} ecrit;

static int creer(ecrit *z, const char *chemin) {
  z->w = NULL;
  z->fd = open(chemin, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (z->fd < 0) return -999;
  z->out.ctx = &z->fd;
  z->out.write = ecrire_fd;
  return fl_arcw_open(&z->out, &z->w);
}

/* Ajoute un fichier de `n` octets (octet i = (i * mul) mod 256). */
static int fichier(fl_arc_writer *w, const char *nom, size_t n, int mul, bool store, int64_t mtime) {
  int rc = fl_arcw_add(w, nom, FL_ARC_FILE, (int64_t)n, mtime, store);
  if (rc != FL_OK) return rc;
  static uint8_t buf[65536];
  for (size_t done = 0; done < n;) {
    size_t k = n - done < sizeof buf ? n - done : sizeof buf;
    for (size_t i = 0; i < k; i++) buf[i] = (uint8_t)((done + i) * (size_t)mul);
    rc = fl_arcw_write(w, buf, k);
    if (rc != FL_OK) return rc;
    done += k;
  }
  return FL_OK;
}

static int lancer(char *const argv[], char *out, size_t cap) { return fu_run(argv, out, cap); }

/* Script Python qui juge le ZIP : « nom|taille|utf8|AAAA-MM-JJ HH:MM:SS|crc-ok » par entrée. */
static const char *JUGE =
    "import sys, zipfile\n"
    "z = zipfile.ZipFile(sys.argv[1])\n"
    "assert z.testzip() is None\n"
    "for i in z.infolist():\n"
    "    print('%s|%d|%d|%04d-%02d-%02d %02d:%02d:%02d' % ((i.filename, i.file_size, (i.flag_bits >> 11) & 1) + i.date_time))\n";

static void juger(const char *zip, char *out, size_t cap) {
  char *py[] = {"python3", "-I", "-c", (char *)JUGE, (char *)zip, NULL};
  TT_ASSERT_EQ(lancer(py, out, cap), 0);
  char tmpout[8192];
  char *uz[] = {"unzip", "-tqq", (char *)zip, NULL};
  TT_ASSERT_EQ(lancer(uz, tmpout, sizeof tmpout), 0);
  char *zt[] = {"zip", "-T", (char *)zip, NULL};
  TT_ASSERT_EQ(lancer(zt, tmpout, sizeof tmpout), 0);
}

#define T0 1714564800 /* 2024-05-01 12:00:00 UTC */

static void test_arcw_arbre(void) {
  const char *zip = tmp("arcw-arbre.zip");
  ecrit z;
  TT_ASSERT_EQ(creer(&z, zip), FL_OK);
  TT_ASSERT_EQ(fl_arcw_add(z.w, "dossier", FL_ARC_DIR, 0, T0, false), FL_OK);
  TT_ASSERT_EQ(fichier(z.w, "dossier/vide.txt", 0, 1, false, T0), FL_OK);
  TT_ASSERT_EQ(fichier(z.w, "dossier/trois.bin", 3 * MIB, 13, false, T0), FL_OK);
  TT_ASSERT_EQ(fichier(z.w, "photo.jpg", 100000, 7, true, T0), FL_OK);
  TT_ASSERT_EQ(fl_arcw_close(z.w, true), FL_OK);
  close(z.fd);
  char out[8192];
  juger(zip, out, sizeof out);
  TT_ASSERT_STR_EQ(out,
                   "dossier/|0|0|2024-05-01 12:00:00\n"
                   "dossier/vide.txt|0|0|2024-05-01 12:00:00\n"
                   "dossier/trois.bin|3145728|0|2024-05-01 12:00:00\n"
                   "photo.jpg|100000|0|2024-05-01 12:00:00\n");
  /* relu par notre lecteur, octet par octet ; photo.jpg stockée sans compression (taille du ZIP) */
  int fd = open(zip, O_RDONLY);
  struct stat st;
  fstat(fd, &st);
  fl_arc_in in = {.ctx = &fd, .size = st.st_size, .pread = lire_fd};
  fl_arc_reader *r = NULL;
  TT_ASSERT_EQ(fl_arc_open(&in, &r), FL_OK);
  static fl_arc_entry e;
  static uint8_t buf[4 * MIB];
  int vus = 0;
  while (fl_arc_next(r, &e) == 1) {
    int64_t total = 0, k;
    while ((k = fl_arc_read(r, buf + total, sizeof buf - (size_t)total)) > 0) total += k;
    TT_ASSERT_EQ(total, e.size);
    int mul = !strcmp(e.name, "photo.jpg") ? 7 : 13, bon = 1;
    for (int64_t i = 0; i < total; i++) bon &= buf[i] == (uint8_t)(i * mul);
    TT_ASSERT(bon);
    vus++;
  }
  TT_ASSERT_EQ(vus, 4);
  fl_arc_close(r);
  close(fd);
  TT_ASSERT(st.st_size > 100000); /* stockée : pas de compression de la photo */
  TT_ASSERT(st.st_size < 100000 + 3 * MIB / 4); /* trois.bin, répétitif, compressé */
  unlink(zip);
}

static void test_arcw_noms_utf8(void) {
  const char *zip = tmp("arcw-utf8.zip");
  ecrit z;
  TT_ASSERT_EQ(creer(&z, zip), FL_OK);
  TT_ASSERT_EQ(fichier(z.w, "été/日本.txt", 10, 1, false, T0), FL_OK);
  TT_ASSERT_EQ(fichier(z.w, "ascii.txt", 10, 1, false, T0), FL_OK);
  TT_ASSERT_EQ(fl_arcw_close(z.w, true), FL_OK);
  close(z.fd);
  char out[8192];
  juger(zip, out, sizeof out);
  TT_ASSERT_STR_EQ(out, "été/日本.txt|10|1|2024-05-01 12:00:00\nascii.txt|10|0|2024-05-01 12:00:00\n");
  unlink(zip);
}

/* Date relue par Python à 2 s près (résolution DOS) ; une seconde impaire est arrondie. */
static void test_arcw_dates(void) {
  const char *zip = tmp("arcw-dates.zip");
  ecrit z;
  TT_ASSERT_EQ(creer(&z, zip), FL_OK);
  TT_ASSERT_EQ(fichier(z.w, "a.txt", 1, 1, false, 946684799 /* 1999-12-31 23:59:59 UTC */), FL_OK);
  TT_ASSERT_EQ(fl_arcw_close(z.w, true), FL_OK);
  close(z.fd);
  char out[8192];
  juger(zip, out, sizeof out);
  /* l'heure DOS est locale : le test tourne en UTC (TZ fixé par main) */
  TT_ASSERT(!strcmp(out, "a.txt|1|0|1999-12-31 23:59:58\n") || !strcmp(out, "a.txt|1|0|2000-01-01 00:00:00\n"));
  unlink(zip);
}

static void test_arcw_refus(void) {
  const char *zip = tmp("arcw-refus.zip");
  ecrit z;
  TT_ASSERT_EQ(creer(&z, zip), FL_OK);
  const char *mauvais[] = {"", "/abs", "../x", "a/../b", "a/./b", "a//b"};
  for (size_t i = 0; i < sizeof mauvais / sizeof *mauvais; i++)
    TT_ASSERT_EQ(fl_arcw_add(z.w, mauvais[i], FL_ARC_FILE, 0, T0, false), FL_EINVAL);
  TT_ASSERT_EQ(fl_arcw_add(z.w, "lien", FL_ARC_SYMLINK, 0, T0, false), FL_EINVAL);
  TT_ASSERT_EQ(fichier(z.w, "ok.txt", 4, 1, false, T0), FL_OK);
  TT_ASSERT_EQ(fl_arcw_write(z.w, "trop", 4), FL_EINVAL); /* au-delà de la taille annoncée */
  TT_ASSERT_EQ(fl_arcw_close(z.w, true), FL_OK);
  close(z.fd);
  unlink(zip);
}

/* Abandon : plus rien n'est écrit après close(false) ; l'appelant supprime le fichier. */
static void test_arcw_abandon(void) {
  const char *zip = tmp("arcw-abandon.zip");
  ecrit z;
  TT_ASSERT_EQ(creer(&z, zip), FL_OK);
  TT_ASSERT_EQ(fichier(z.w, "a.bin", 200000, 3, false, T0), FL_OK);
  off_t avant = lseek(z.fd, 0, SEEK_CUR);
  TT_ASSERT_EQ(fl_arcw_close(z.w, false), FL_OK);
  TT_ASSERT_EQ(lseek(z.fd, 0, SEEK_CUR), avant);
  close(z.fd);
  unlink(zip);
}

/* Erreur de la destination (disque plein, débranché) : rendue telle quelle. */
static int ecrire_panne(void *ctx, const void *buf, size_t len) {
  (void)ctx, (void)buf, (void)len;
  return FL_ENOSPC;
}

static void test_arcw_panne(void) {
  fl_arc_out out = {.ctx = NULL, .write = ecrire_panne};
  fl_arc_writer *w = NULL;
  TT_ASSERT_EQ(fl_arcw_open(&out, &w), FL_OK);
  int rc = fichier(w, "a.bin", 4 * MIB, 3, true, T0);
  if (rc == FL_OK) rc = fl_arcw_close(w, true);
  else fl_arcw_close(w, false);
  TT_ASSERT_EQ(rc, FL_ENOSPC);
}

/* ZIP64 : un fichier de 4,5 Gio (zéros, deflate) ; unzip -t et Python le relisent. */
static void test_arcw_zip64_lent(void) {
  const char *zip = tmp("arcw-zip64.zip");
  ecrit z;
  TT_ASSERT_EQ(creer(&z, zip), FL_OK);
  uint64_t n = 4608ull * MIB;
  TT_ASSERT_EQ(fl_arcw_add(z.w, "gros.bin", FL_ARC_FILE, (int64_t)n, T0, false), FL_OK);
  static uint8_t zero[MIB];
  for (uint64_t done = 0; done < n; done += MIB) TT_ASSERT_EQ(fl_arcw_write(z.w, zero, MIB), FL_OK);
  TT_ASSERT_EQ(fl_arcw_close(z.w, true), FL_OK);
  close(z.fd);
  char out[8192];
  juger(zip, out, sizeof out);
  TT_ASSERT_STR_EQ(out, "gros.bin|4831838208|0|2024-05-01 12:00:00\n");
  unlink(zip);
}

int main(int argc, char **argv) {
  setenv("TZ", "UTC", 1);
  tzset();
  if (argc > 1 && !strcmp(argv[1], "lent")) {
    TT_RUN(test_arcw_zip64_lent);
  } else {
    TT_RUN(test_arcw_arbre);
    TT_RUN(test_arcw_noms_utf8);
    TT_RUN(test_arcw_dates);
    TT_RUN(test_arcw_refus);
    TT_RUN(test_arcw_abandon);
    TT_RUN(test_arcw_panne);
  }
  TT_MAIN_END();
}
