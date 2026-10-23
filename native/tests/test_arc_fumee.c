/* Phase 9, T1 : libarchive et liblzma construits avec le moteur. Lecture par l'API brute de libarchive
 * d'un tar.xz (filtre xz de liblzma) et d'un zip (deflate de zlib), faits par tar et zip. */
#include <archive.h>
#include <archive_entry.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tt.h"

static const char *arc(const char *nom) {
  static char buf[4096];
  const char *dir = getenv("FL_ARC_FIXTURES");
  snprintf(buf, sizeof buf, "%s/%s", dir ? dir : ".", nom);
  return buf;
}

/* Lit toute l'archive : noms dans l'ordre (séparés par « | »), contenu de été.txt, octets de b.bin vérifiés. */
static void lire(const char *nom) {
  struct archive *a = archive_read_new();
  TT_ASSERT(a != NULL);
  archive_read_support_filter_xz(a);
  archive_read_support_filter_gzip(a);
  archive_read_support_format_tar(a);
  archive_read_support_format_zip(a);
  TT_ASSERT_EQ(archive_read_open_filename(a, arc(nom), 65536), ARCHIVE_OK);
  char noms[512] = "";
  struct archive_entry *e;
  int vus = 0;
  int r;
  while ((r = archive_read_next_header(a, &e)) == ARCHIVE_OK) {
    const char *n = archive_entry_pathname(e);
    strncat(noms, n, sizeof noms - strlen(noms) - 2);
    strcat(noms, "|");
    static unsigned char buf[200000];
    la_ssize_t k, total = 0;
    while ((k = archive_read_data(a, buf + total, sizeof buf - (size_t)total)) > 0) total += k;
    TT_ASSERT(k == 0);
    if (!strcmp(n, "été.txt")) {
      TT_ASSERT_EQ(total, 8);
      TT_ASSERT(memcmp(buf, "bonjour\n", 8) == 0);
      vus++;
    } else if (!strcmp(n, "dossier/b.bin")) {
      TT_ASSERT_EQ(total, 102400);
      int bon = 1;
      for (int i = 0; i < 102400; i++) bon &= buf[i] == (unsigned char)(i * 7 % 256);
      TT_ASSERT(bon);
      vus++;
    }
  }
  TT_ASSERT_EQ(r, ARCHIVE_EOF);
  TT_ASSERT_STR_EQ(noms, "été.txt|dossier/|dossier/b.bin|");
  TT_ASSERT_EQ(vus, 2);
  archive_read_free(a);
}

static void test_tar_xz(void) { lire("fumee.tar.xz"); }
static void test_zip(void) { lire("fumee.zip"); }

int main(void) {
  /* Locale UTF-8, comme Bionic : sous « C », libarchive ne convertit pas les noms UTF-8 d'un ZIP */
  if (!setlocale(LC_CTYPE, "C.UTF-8")) {
    fprintf(stderr, "locale C.UTF-8 absente\n");
    return 1;
  }
  TT_RUN(test_tar_xz);
  TT_RUN(test_zip);
  TT_MAIN_END();
}
