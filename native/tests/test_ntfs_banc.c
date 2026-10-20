#define _GNU_SOURCE
/* Banc NTFS (phase 5, T1) : teste le banc lui-même (images remplies par ntfs-remplir, juges ntfsjuge.py et
 * ntfsfix -n, modèle d'arborescence). Aucun code NTFS du moteur ici. Réservé au PC : sauté si un outil
 * manque, et sur Android. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "crypto/fl_crypto.h"
#include "fsck_util.h"
#include "tree_model.h"
#include "tt.h"

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

static int copie_de(const char *fixture, char *out, size_t cap) {
  char src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture(fixture));
  if (fu_copy_image(src, out, cap) != 0) return -1;
  noter(out);
  return 0;
}

static int check(const char *img) {
  char out[8192];
  int rc = fu_ntfsjuge(out, sizeof out, "check", img, NULL);
  if (rc != 0) fprintf(stderr, "ntfsjuge check %s : %d\n%s", img, rc, out);
  return rc;
}

static void test_banc_images_propres(void) {
  const char *imgs[] = {"ntfs-arbre.img", "ntfs-4k.img", "ntfs-vide.img", "ntfs-512-sur-4k.img", "ntfs-casse.img",
                        "ntfs-hiber.img"};
  for (size_t i = 0; i < sizeof imgs / sizeof *imgs; i++) {
    char img[4096], out[8192];
    snprintf(img, sizeof img, "%s", tt_fixture(imgs[i]));
    TT_ASSERT_EQ(check(img), 0);
    if (strcmp(imgs[i], "ntfs-hiber.img") != 0) TT_ASSERT_EQ(fu_ntfsfix_n(img, out, sizeof out), 0);
  }
}

/* Condensat attendu de /clairsemé.bin : 5 Gio de zéros, « FIN-FLULUKS » dans les 11 derniers octets. */
static int condensat_clairseme(char hex[65]) {
  size_t n = 5120;
  uint8_t *digs = malloc(n * 32), *chunk = calloc(1, TM_CHUNK), fin[32];
  int rc = -1;
  if (digs && chunk && fl_hash("sha256", chunk, TM_CHUNK, digs) == 0) {
    for (size_t i = 1; i < n - 1; i++) memcpy(digs + 32 * i, digs, 32);
    memcpy(chunk + TM_CHUNK - 11, "FIN-FLULUKS", 11);
    if (fl_hash("sha256", chunk, TM_CHUNK, digs + 32 * (n - 1)) == 0 && fl_hash("sha256", digs, n * 32, fin) == 0) {
      for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", fin[i]);
      rc = 0;
    }
  }
  free(digs);
  free(chunk);
  return rc;
}

/* Ligne de manifeste de `chemin` dans le fichier, ou NULL (tampon statique). */
static const char *ligne_de(const char *fichier, const char *chemin) {
  static char l[8192];
  FILE *f = fopen(fichier, "r");
  if (!f) return NULL;
  const char *r = NULL;
  while (fgets(l, sizeof l, f)) {
    l[strcspn(l, "\n")] = 0;
    const char *c = strrchr(l, '\t');
    if (c && !strcmp(c + 1, chemin)) {
      r = l;
      break;
    }
  }
  fclose(f);
  return r;
}

static void test_banc_manifeste(void) {
  tm *m = tm_new();
  TT_ASSERT(m != NULL);
  char src[4096];
  snprintf(src, sizeof src, "%s", tt_fixture("tree-ntfs"));
  TT_ASSERT_EQ(tm_from_dir(m, src), 0);
  tm_set_ntfs_attrs(m);
  /* spéciaux posés par ntfs-remplir --speciaux, hors liens, point d'analyse et fichier creux */
  TT_ASSERT_EQ(tm_apply_file(m, "/lien-dur.txt", "minuscules\n", 11, 0, 0, 0644), 0);
  static const char motif[] = "FluLuks NTFS\n";
  size_t clen = 256 * 1024;
  char *c = malloc(clen);
  TT_ASSERT(c != NULL);
  for (size_t i = 0; i < clen; i++) c[i] = motif[i % (sizeof motif - 1)];
  TT_ASSERT_EQ(tm_apply_file(m, "/compressé.txt", c, clen, 0, 0, 0644), 0);
  free(c);
  char nom[700] = "/nom-trop-long-";
  size_t p = strlen(nom);
  for (int i = 0; i < 200; i++, p += 3) memcpy(nom + p, "\xe4\xb8\xad", 3);
  memcpy(nom + p, ".txt", 5);
  TT_ASSERT_EQ(tm_apply_file(m, nom, "long\n", 5, 0, 0, 0644), 0);

  char dir[4096], juge[4200], modele[4200];
  TT_ASSERT_EQ(fu_tmpdir(dir, sizeof dir), 0);
  snprintf(juge, sizeof juge, "%s/banc-ntfs-juge.txt", dir);
  snprintf(modele, sizeof modele, "%s/banc-ntfs-modele.txt", dir);
  noter(juge);
  noter(modele);
  TT_ASSERT_EQ(fu_ntfs_manifest(tt_fixture("ntfs-arbre.img"), juge), 0);
  TT_ASSERT_EQ(tm_write_manifest(m, modele), 0);
  tm_free(m);

  /* Le modèle entier se retrouve dans le juge, ligne pour ligne. */
  FILE *f = fopen(modele, "r");
  TT_ASSERT(f != NULL);
  char l[8192];
  int lignes = 0, ecarts = 0;
  while (fgets(l, sizeof l, f)) {
    l[strcspn(l, "\n")] = 0;
    const char *ch = strrchr(l, '\t');
    const char *j = ch ? ligne_de(juge, ch + 1) : NULL;
    lignes++;
    if (!j || strcmp(j, l) != 0) {
      if (ecarts++ < 10) fprintf(stderr, "modèle : %s\njuge   : %s\n", l, j ? j : "(absent)");
    }
  }
  fclose(f);
  TT_ASSERT_EQ(ecarts, 0);
  TT_ASSERT(lignes > 1000);

  char hex[65];
  TT_ASSERT_EQ(condensat_clairseme(hex), 0);
  char attendu[256];
  snprintf(attendu, sizeof attendu, "f\t5368709120\t%s\t0\t0\t100644\t/clairsemé.bin", hex);
  const char *j = ligne_de(juge, "/clairsemé.bin");
  TT_ASSERT(j != NULL);
  TT_ASSERT_STR_EQ(j, attendu);
  j = ligne_de(juge, "/lien");
  TT_ASSERT(j != NULL);
  TT_ASSERT_STR_EQ(j, "l\t0\t-\t0\t0\t120777\t/lien");
  j = ligne_de(juge, "/jonction");
  TT_ASSERT(j != NULL);
  TT_ASSERT_STR_EQ(j, "l\t0\t-\t0\t0\t120777\t/jonction");
  j = ligne_de(juge, "/dédup.bin");
  TT_ASSERT(j != NULL);
  TT_ASSERT_STR_EQ(j, "o\t0\t-\t0\t0\t100000\t/dédup.bin");

  /* Rien d'autre dans le juge : modèle + 4 lignes. */
  f = fopen(juge, "r");
  TT_ASSERT(f != NULL);
  int nj = 0;
  while (fgets(l, sizeof l, f)) nj++;
  fclose(f);
  TT_ASSERT_EQ(nj, lignes + 4);
}

static void test_banc_detecte(void) {
  const char *cas[] = {"bitmap", "miroir", "liens", "nom"};
  for (size_t i = 0; i < sizeof cas / sizeof *cas; i++) {
    char img[4096], out[8192];
    TT_ASSERT_EQ(copie_de("ntfs-arbre.img", img, sizeof img), 0);
    TT_ASSERT_EQ(fu_ntfsjuge(out, sizeof out, "abime", img, cas[i]), 0);
    TT_ASSERT_EQ(fu_ntfsjuge(out, sizeof out, "check", img, NULL), 3);
    TT_ASSERT(strlen(out) > 0);
    unlink(img);
  }
}

static void test_banc_flags(void) {
  TT_ASSERT_EQ(fu_ntfs_flags(tt_fixture("ntfs-sale.img")), 0x0001);
  TT_ASSERT_EQ(fu_ntfs_flags(tt_fixture("ntfs-chkdsk.img")), 0x4000);
  TT_ASSERT_EQ(fu_ntfs_flags(tt_fixture("ntfs-arbre.img")), 0);
  /* les images marquées restent cohérentes (seul le drapeau change) */
  TT_ASSERT_EQ(check(tt_fixture("ntfs-sale.img")), 0);
  TT_ASSERT_EQ(check(tt_fixture("ntfs-journal.img")), 0);
  TT_ASSERT_EQ(check(tt_fixture("ntfs-chiffre.img")), 0);
}

static void test_banc_libre(void) {
  /* ntfs-vide : libre selon le juge = libre selon ntfsinfo -m (« Free Clusters »), indépendants */
  char out[16384];
  char *const argv[] = {(char *)"ntfsinfo", (char *)"-m", (char *)tt_fixture("ntfs-vide.img"), NULL};
  TT_ASSERT_EQ(fu_run(argv, out, sizeof out), 0);
  const char *cs = strstr(out, "Cluster Size: ");
  const char *fc = strstr(out, "Free Clusters: ");
  TT_ASSERT(cs != NULL && fc != NULL);
  unsigned long long clsz = strtoull(cs + 14, NULL, 10), libres = strtoull(fc + 15, NULL, 10);
  TT_ASSERT(clsz == 4096 && libres > 0);
  TT_ASSERT_EQ(fu_ntfs_free(tt_fixture("ntfs-vide.img")), (long long)(libres * clsz));
}

int main(void) {
  if (!fu_ntfs_tools_available()) {
    printf("SAUTÉ : outils NTFS absents ou plateforme Android (banc réservé au PC)\n");
    return 77;
  }
  TT_RUN(test_banc_images_propres);
  TT_RUN(test_banc_manifeste);
  TT_RUN(test_banc_detecte);
  TT_RUN(test_banc_flags);
  TT_RUN(test_banc_libre);
  nettoyer();
  TT_MAIN_END();
}
