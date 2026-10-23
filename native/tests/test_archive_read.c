#define _GNU_SOURCE
/* Phase 9, T2 : lecture d'archives (fl_arc_*). Archives faites par zip, tar, gzip et Python
 * (tools/fixtures/make_archive_fixtures.sh), et archives 7z et RAR de la suite de libarchive faites par
 * 7-Zip et WinRAR (native/tests/data/archives/LISEZMOI.md). Les noms sont rendus bruts ; une archive
 * tronquée ou chiffrée échoue proprement (ASan). */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fluluks/archive.h"
#include "fluluks/error.h"
#include "tt.h"

static const char *arc(const char *nom) {
  static char buf[4096];
  const char *dir = getenv("FL_ARC_FIXTURES");
  snprintf(buf, sizeof buf, "%s/%s", dir ? dir : ".", nom);
  return buf;
}

static int64_t lire_fd(void *ctx, void *buf, size_t len, uint64_t off) {
  ssize_t n = pread(*(int *)ctx, buf, len, (off_t)off);
  return n < 0 ? FL_EIO : n;
}

typedef struct {
  int fd;
  fl_arc_in in;
  fl_arc_reader *r;
} ouverte;

/* Ouvre l'archive de test `nom` ; rend le code de fl_arc_open. */
static int ouvrir(ouverte *o, const char *nom) {
  o->r = NULL;
  o->fd = open(arc(nom), O_RDONLY);
  if (o->fd < 0) return -999;
  o->in.ctx = &o->fd;
  o->in.size = lseek(o->fd, 0, SEEK_END);
  o->in.pread = lire_fd;
  return fl_arc_open(&o->in, &o->r);
}

static void fermer(ouverte *o) {
  if (o->r) fl_arc_close(o->r);
  if (o->fd >= 0) close(o->fd);
}

/* Lit toutes les données de l'entrée courante dans buf (au plus cap) ; rend le total ou le code d'erreur. */
static int64_t donnees(fl_arc_reader *r, uint8_t *buf, size_t cap) {
  int64_t total = 0, k;
  while ((k = fl_arc_read(r, buf + total, cap - (size_t)total)) > 0) total += k;
  return k < 0 ? k : total;
}

/* Parcourt l'archive : « nom:type:taille| » par entrée dans `liste` ; contenus vérifiés par `verifier`. */
static int parcourir(const char *nom, char *liste, size_t cap, void (*verifier)(const fl_arc_entry *, const uint8_t *, int64_t)) {
  ouverte o;
  int rc = ouvrir(&o, nom);
  if (rc != FL_OK) {
    fermer(&o);
    return rc;
  }
  liste[0] = 0;
  static fl_arc_entry e;
  static uint8_t buf[1 << 20];
  while ((rc = fl_arc_next(o.r, &e)) == 1) {
    char un[4200];
    snprintf(un, sizeof un, "%s:%d:%lld|", e.name, (int)e.type, (long long)e.size);
    strncat(liste, un, cap - strlen(liste) - 1);
    int64_t n = donnees(o.r, buf, sizeof buf);
    if (verifier) verifier(&e, buf, n);
  }
  fermer(&o);
  return rc;
}

static void verifier_arbre(const fl_arc_entry *e, const uint8_t *buf, int64_t n) {
  if (!strcmp(e->name, "été.txt")) {
    TT_ASSERT_EQ(n, 8);
    TT_ASSERT(memcmp(buf, "bonjour\n", 8) == 0);
    TT_ASSERT_EQ(e->mtime, 1714564800); /* 2024-05-01 12:00:00 UTC */
  } else if (!strcmp(e->name, "dossier/b.bin")) {
    TT_ASSERT_EQ(n, 102400);
    int bon = 1;
    for (int i = 0; i < 102400; i++) bon &= buf[i] == (uint8_t)(i * 7 % 256);
    TT_ASSERT(bon);
  } else {
    TT_ASSERT_EQ(n, 0);
  }
}

/* Valeurs de verify_data des tests RAR5 de libarchive : entiers 32 bits petit-boutistes. */
static void verifier_rar5(const fl_arc_entry *e, const uint8_t *buf, int64_t n) {
  TT_ASSERT_EQ(n, 4096);
  int magic = e->name[4] - '0';
  int bon = 1;
  for (int i = 0; i < 1024; i++) {
    int k = i + 1, val = k * k - 3 * k + 1 + magic;
    if (val < 0) val = 0;
    uint32_t lu = (uint32_t)buf[4 * i] | (uint32_t)buf[4 * i + 1] << 8 | (uint32_t)buf[4 * i + 2] << 16 |
                  (uint32_t)buf[4 * i + 3] << 24;
    bon &= lu == (uint32_t)val;
  }
  TT_ASSERT(bon);
}

static void verifier_rar4(const fl_arc_entry *e, const uint8_t *buf, int64_t n) {
  if (e->type == FL_ARC_FILE) {
    TT_ASSERT_EQ(n, 20);
    TT_ASSERT(memcmp(buf, "test text document\r\n", 20) == 0);
  }
}

static void verifier_lecture_complete(const fl_arc_entry *e, const uint8_t *buf, int64_t n) {
  (void)buf;
  TT_ASSERT_EQ(n, e->size); /* 7z : CRC vérifié par le lecteur, toute erreur rend n < 0 */
}

static void test_arc_formats(void) {
  static char l[8192];
  const char *arbre = "été.txt:1:8|dossier/:2:0|dossier/b.bin:1:102400|";
  TT_ASSERT_EQ(parcourir("fumee.zip", l, sizeof l, verifier_arbre), 0);
  TT_ASSERT_STR_EQ(l, arbre);
  TT_ASSERT_EQ(parcourir("stocke.zip", l, sizeof l, verifier_arbre), 0);
  TT_ASSERT_STR_EQ(l, arbre);
  TT_ASSERT_EQ(parcourir("arbre.tar", l, sizeof l, verifier_arbre), 0);
  TT_ASSERT_STR_EQ(l, arbre);
  TT_ASSERT_EQ(parcourir("arbre.tar.gz", l, sizeof l, verifier_arbre), 0);
  TT_ASSERT_STR_EQ(l, arbre);
  TT_ASSERT_EQ(parcourir("fumee.tar.xz", l, sizeof l, verifier_arbre), 0);
  TT_ASSERT_STR_EQ(l, arbre);
  TT_ASSERT_EQ(parcourir("test_read_format_7zip_lzma2.7z", l, sizeof l, verifier_lecture_complete), 0);
  TT_ASSERT_STR_EQ(l, "file1:1:2844|");
  TT_ASSERT_EQ(parcourir("test_read_format_7zip_bcj_lzma2.7z", l, sizeof l, verifier_lecture_complete), 0);
  TT_ASSERT_STR_EQ(l, "x86exe:1:27328|");
  TT_ASSERT_EQ(parcourir("test_read_format_rar.rar", l, sizeof l, verifier_rar4), 0);
  TT_ASSERT_STR_EQ(l, "test.txt:1:20|testlink:3:0|testdir/test.txt:1:20|testdir:2:0|testemptydir:2:0|");
  TT_ASSERT_EQ(parcourir("test_read_format_rar5_multiple_files.rar", l, sizeof l, verifier_rar5), 0);
  TT_ASSERT_STR_EQ(l, "test1.bin:1:4096|test2.bin:1:4096|test3.bin:1:4096|test4.bin:1:4096|");
}

/* Un .gz seul : une entrée « data » de taille inconnue, le contenu décompressé. */
static void test_arc_gz_seul(void) {
  ouverte o;
  TT_ASSERT_EQ(ouvrir(&o, "seul.txt.gz"), FL_OK);
  static fl_arc_entry e;
  TT_ASSERT_EQ(fl_arc_next(o.r, &e), 1);
  TT_ASSERT_EQ(e.type, FL_ARC_FILE);
  TT_ASSERT_EQ(e.size, -1);
  uint8_t buf[64];
  TT_ASSERT_EQ(donnees(o.r, buf, sizeof buf), 8);
  TT_ASSERT(memcmp(buf, "bonjour\n", 8) == 0);
  TT_ASSERT_EQ(fl_arc_next(o.r, &e), 0);
  fermer(&o);
}

static void test_arc_noms_bruts(void) {
  static char l[8192];
  TT_ASSERT_EQ(parcourir("hostile.zip", l, sizeof l, NULL), 0);
  /* libarchive lit « \\ » d'un nom ZIP comme un séparateur : « a\\b » devient le chemin « a/b » */
  TT_ASSERT_STR_EQ(l, "../x:1:8|/abs:1:8|a/../../x:1:8|a/b:1:8|ok.txt:1:8|");
}

static void test_arc_zip_cp437(void) {
  ouverte o;
  TT_ASSERT_EQ(ouvrir(&o, "cp437.zip"), FL_OK);
  static fl_arc_entry e;
  TT_ASSERT_EQ(fl_arc_next(o.r, &e), 1);
  TT_ASSERT_EQ(e.name_len, 5);
  TT_ASSERT(memcmp(e.name, "\x82.txt", 5) == 0);
  uint8_t buf[64];
  TT_ASSERT_EQ(donnees(o.r, buf, sizeof buf), 6);
  fermer(&o);
}

/* Entrée chiffrée : signalée, ses données refusées (FL_ENOTSUP), sans plantage ; ZIP, 7z et RAR4. */
static void test_arc_chiffre(void) {
  const char *noms[] = {"chiffre.zip", "test_read_format_7zip_encryption.7z", "test_read_format_rar_encryption_data.rar"};
  for (size_t i = 0; i < 3; i++) {
    ouverte o;
    TT_ASSERT_EQ(ouvrir(&o, noms[i]), FL_OK);
    static fl_arc_entry e;
    TT_ASSERT_EQ(fl_arc_next(o.r, &e), 1);
    TT_ASSERT(e.encrypted);
    uint8_t buf[64];
    TT_ASSERT_EQ(fl_arc_read(o.r, buf, sizeof buf), FL_ENOTSUP);
    fermer(&o);
  }
}

/* Archive coupée : une erreur, à l'ouverture ou pendant la lecture, jamais de plantage ni de fuite. */
static void test_arc_tronque(void) {
  static char l[8192];
  TT_ASSERT(parcourir("tronque.zip", l, sizeof l, NULL) < 0);
  int rc = parcourir("tronque.tar.gz", l, sizeof l, NULL);
  TT_ASSERT(rc < 0);
}

static void test_arc_inconnu(void) {
  ouverte o;
  TT_ASSERT_EQ(ouvrir(&o, "texte.txt"), FL_ENOTSUP);
  TT_ASSERT(o.r == NULL);
  fermer(&o);
}

/* Erreur d'entrée-sortie de la source : rendue telle quelle (disque débranché). */
static int64_t lire_panne(void *ctx, void *buf, size_t len, uint64_t off) {
  (void)ctx, (void)buf, (void)len, (void)off;
  return FL_ENODEV;
}

static void test_arc_panne(void) {
  fl_arc_in in = {.ctx = NULL, .size = 4096, .pread = lire_panne};
  fl_arc_reader *r = NULL;
  TT_ASSERT_EQ(fl_arc_open(&in, &r), FL_ENODEV);
  TT_ASSERT(r == NULL);
}

int main(void) {
  TT_RUN(test_arc_formats);
  TT_RUN(test_arc_gz_seul);
  TT_RUN(test_arc_noms_bruts);
  TT_RUN(test_arc_zip_cp437);
  TT_RUN(test_arc_chiffre);
  TT_RUN(test_arc_tronque);
  TT_RUN(test_arc_inconnu);
  TT_RUN(test_arc_panne);
  TT_MAIN_END();
}
