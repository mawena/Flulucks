#define _GNU_SOURCE
/* Disques NTFS hostiles (phase 5, T8) : 300 images dérivées de ntfs-arbre.img en mémoire (octets aléatoires à
 * graine fixe dans un enregistrement MFT ou un bloc d'index INDX, corrections de fin de secteur recalculées,
 * sinon libntfs-3g rejette le bloc avant d'en lire le contenu), plus les cas fabriqués par ntfsjuge.py
 * (ntfs-hostile-*.img : runlist hors du disque ou sur $MFT, enregistrement qui se désigne comme sa propre
 * base, entrée d'index de longueur nulle, flux compressé faux, nom UTF-16 invalide). Chaque image est montée
 * en lecture seule et parcourue (chaque fichier lu en entier) ; puis, si l'écriture est permise, montée en
 * écriture sur une copie en mémoire pour y créer un fichier et un dossier. Attendu : ni plantage ni rapport
 * ASan/UBSan, ni boucle sans fin (10 s par image), seulement FL_OK ou un fl_err. Tourne aussi sur Android. */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "tt.h"

/* --- périphérique en mémoire --------------------------------------------------------------------- */

typedef struct {
  uint8_t *data;
} mem;

static int m_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  memcpy(buf, ((mem *)d->priv)->data + lba * d->sector_size, (size_t)count * d->sector_size);
  return FL_OK;
}
static int m_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf) {
  memcpy(((mem *)d->priv)->data + lba * d->sector_size, buf, (size_t)count * d->sector_size);
  return FL_OK;
}
static int m_flush(fl_bdev *d) {
  (void)d;
  return FL_OK;
}
static void m_destroy(fl_bdev *d) { (void)d; }
static const fl_bdev_ops k_mem_ops = {m_read, m_write, m_flush, m_destroy};

static uint8_t *charger(const char *fixture, size_t *size) {
  FILE *f = fopen(tt_fixture(fixture), "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  *size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *d = malloc(*size);
  if (d && fread(d, 1, *size, f) != *size) {
    free(d);
    d = NULL;
  }
  fclose(f);
  return d;
}

/* --- parcours ----------------------------------------------------------------------------------- */

typedef struct {
  fl_fs *fs;
  size_t vus, limite_fichier;
  int prof;
  uint8_t *buf;
  int codes_ko; /* code rendu qui n'est ni FL_OK ni un fl_err */
} parcours;

static int code_ok(int64_t rc) { return rc >= FL_EFBIG && rc <= 0; }

static void lire_fichier(parcours *pc, const char *p, uint64_t taille) {
  fl_file *f = NULL;
  int rc = fl_fs_open(pc->fs, p, FL_O_RDONLY, &f);
  if (!code_ok(rc)) pc->codes_ko++;
  if (rc != FL_OK) return;
  if (taille > pc->limite_fichier) taille = pc->limite_fichier; /* creux géant : la fin suffit */
  for (uint64_t o = 0; o < taille;) {
    int64_t n = fl_file_pread(f, pc->buf, 1 << 20, o);
    if (n < 0 && !code_ok(n)) pc->codes_ko++;
    if (n <= 0) break;
    o += (uint64_t)n;
  }
  fl_file_close(f);
}

typedef struct {
  char (*noms)[256];
  fl_stat *st;
  size_t n, cap;
} lot;

static int collecter(void *ctx, const fl_dirent *e) {
  lot *l = ctx;
  if (l->n == l->cap) {
    size_t c = l->cap ? l->cap * 2 : 64;
    void *a = realloc(l->noms, c * sizeof *l->noms), *b = a ? realloc(l->st, c * sizeof *l->st) : NULL;
    if (!a || !b) {
      if (a) l->noms = a;
      return FL_ENOMEM;
    }
    l->noms = a;
    l->st = b;
    l->cap = c;
  }
  memcpy(l->noms[l->n], e->name, sizeof l->noms[0]);
  l->st[l->n++] = e->st;
  return 0;
}

static void parcourir(parcours *pc, const char *dir) {
  if (pc->prof > 64 || pc->vus > 20000) return;
  lot l = {0};
  int rc = fl_fs_readdir(pc->fs, dir, collecter, &l);
  if (!code_ok(rc)) pc->codes_ko++;
  pc->prof++;
  for (size_t i = 0; i < l.n && pc->vus <= 20000; i++) {
    pc->vus++;
    char p[4096];
    int k = snprintf(p, sizeof p, "%s/%s", strcmp(dir, "/") ? dir : "", l.noms[i]);
    if (k <= 0 || (size_t)k >= sizeof p - 256) continue;
    fl_stat st;
    rc = fl_fs_stat(pc->fs, p, &st);
    if (!code_ok(rc)) pc->codes_ko++;
    if (rc != FL_OK) continue;
    if (st.type == FL_FT_DIR) parcourir(pc, p);
    else lire_fichier(pc, p, st.size);
  }
  pc->prof--;
  free(l.noms);
  free(l.st);
}

static const char *g_cas = "";
static void alarme(int sig) {
  (void)sig;
  static const char m[] = "ECHEC : plus de 10 s sur une image hostile : ";
  write(2, m, sizeof m - 1);
  write(2, g_cas, strlen(g_cas));
  write(2, "\n", 1);
  _exit(1);
}

/* Monte en lecture seule et parcourt ; puis, si l'écriture est permise, écrit sur une copie. Rend le nombre
 * de codes hors fl_err. */
static int essayer(const uint8_t *data, size_t size, uint8_t *buf, const char *cas) {
  g_cas = cas;
  alarm(10);
  uint8_t *copie = malloc(size);
  if (!copie) return 1;
  memcpy(copie, data, size);
  mem m = {copie};
  fl_bdev dev = {&k_mem_ops, 512, size / 512, true, &m};
  fl_fs *fs = NULL;
  int ko = 0;
  int rc = fl_fs_mount(&dev, FL_CONTENT_NTFS, false, &fs);
  if (!code_ok(rc)) ko++;
  if (rc == FL_OK) {
    parcours pc = {fs, 0, size, 0, buf, 0};
    fl_statfs st;
    if (!code_ok(fl_fs_statfs(fs, &st))) pc.codes_ko++;
    parcourir(&pc, "/");
    if (fl_fs_unmount(fs) != FL_OK) pc.codes_ko++;
    ko += pc.codes_ko;
  }
  /* en écriture : créer un fichier et un dossier, écrire, renommer, supprimer */
  dev.read_only = false;
  fs = NULL;
  rc = fl_fs_mount(&dev, FL_CONTENT_NTFS, true, &fs);
  if (!code_ok(rc)) ko++;
  if (rc == FL_OK) {
    fl_file *f = NULL;
    int r[6];
    r[0] = fl_fs_mkdir(fs, "/hostile");
    r[1] = fl_fs_open(fs, "/hostile/f.bin", FL_O_RDWR | FL_O_CREAT, &f);
    r[2] = f ? (int)fl_file_pwrite(f, buf, 100000, 0) : FL_OK;
    if (r[2] > 0) r[2] = FL_OK;
    r[3] = f ? fl_file_close(f) : FL_OK;
    r[4] = fl_fs_rename(fs, "/abc.txt", "/hostile/abc.txt");
    r[5] = fl_fs_unlink(fs, "/gros.bin");
    for (int i = 0; i < 6; i++)
      if (!code_ok(r[i])) ko++;
    if (!code_ok(fl_fs_unmount(fs))) ko++;
  }
  alarm(0);
  free(copie);
  return ko;
}

/* --- mutations aléatoires ------------------------------------------------------------------------ */

static uint64_t g_rng = 0x5eed2027;
static uint32_t alea(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 7;
  g_rng ^= g_rng << 17;
  return (uint32_t)(g_rng >> 11);
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* Corrections de fin de secteur (USA) d'un bloc de len octets : retirées (fix = 0) ou reposées (1). Faux si
 * le bloc n'en a pas de valides. */
static int usa(uint8_t *b, size_t len, int pose) {
  uint16_t ofs = rd16(b + 4), cnt = rd16(b + 6);
  if (cnt < 2 || (size_t)(cnt - 1) * 512 != len || ofs + 2u * cnt > 512) return 0;
  for (uint16_t i = 1; i < cnt; i++) {
    uint8_t *fin = b + (size_t)i * 512 - 2, *sauve = b + ofs + 2 * i;
    uint8_t t[2];
    if (pose) {
      memcpy(sauve, fin, 2);
      memcpy(fin, b + ofs, 2);
    } else {
      memcpy(t, sauve, 2);
      memcpy(fin, t, 2);
    }
  }
  return 1;
}

static void test_ntfs_hostile_aleatoire(void) {
  size_t size = 0;
  uint8_t *img = charger("ntfs-arbre.img", &size);
  TT_ASSERT(img != NULL);
  /* blocs « FILE » (1 Kio) et « INDX » (4 Kio) de l'image */
  size_t *blocs = malloc(sizeof *blocs * (size / 1024)), nb = 0;
  for (size_t o = 0; o + 1024 <= size; o += 1024)
    if (!memcmp(img + o, "FILE", 4) || (o % 4096 == 0 && o + 4096 <= size && !memcmp(img + o, "INDX", 4))) blocs[nb++] = o;
  TT_ASSERT(nb > 1000);
  uint8_t *buf = malloc(1 << 20), save[4096];
  memset(buf, 0x5A, 1 << 20);
  int ko = 0;
  char cas[64];
  for (int k = 0; k < 300; k++) {
    size_t o = blocs[alea() % nb];
    size_t len = !memcmp(img + o, "INDX", 4) ? 4096 : 1024;
    memcpy(save, img + o, len);
    if (usa(img + o, len, 0)) {
      int n = 1 + (int)(alea() % 4);
      for (int i = 0; i < n; i++) img[o + 8 + alea() % (len - 8)] = (uint8_t)alea();
      usa(img + o, len, 1);
    }
    snprintf(cas, sizeof cas, "mutation %d (bloc à %zu)", k, o);
    if (getenv("FL_HOSTILE_TRACE")) fprintf(stderr, "%s\n", cas);
    int r = essayer(img, size, buf, cas);
    if (r) fprintf(stderr, "%s : %d code(s) hors fl_err\n", cas, r);
    ko += r;
    memcpy(img + o, save, len);
  }
  free(blocs);
  free(buf);
  free(img);
  TT_ASSERT_EQ(ko, 0);
}

static void test_ntfs_hostile_fabriques(void) {
  static const char *const cas[] = {"runlist-hors", "runlist-mft", "base-soi", "index-zero", "lznt1", "utf16",
                                    "attr-longueur", "index-boucle", "attrlist-zero", "attrlist-boucle"};
  uint8_t *buf = malloc(1 << 20);
  memset(buf, 0x5A, 1 << 20);
  int ko = 0;
  for (size_t i = 0; i < sizeof cas / sizeof *cas; i++) {
    char nom[64];
    snprintf(nom, sizeof nom, "ntfs-hostile-%s.img", cas[i]);
    size_t size = 0;
    uint8_t *img = charger(nom, &size);
    if (!img) {
      fprintf(stderr, "image absente : %s\n", nom);
      ko++;
      continue;
    }
    int r = essayer(img, size, buf, nom);
    if (r) fprintf(stderr, "%s : %d code(s) hors fl_err\n", nom, r);
    ko += r;
    free(img);
  }
  free(buf);
  TT_ASSERT_EQ(ko, 0);
}

int main(void) {
  signal(SIGALRM, alarme);
  time_t t0 = time(NULL);
  TT_RUN(test_ntfs_hostile_fabriques);
  TT_RUN(test_ntfs_hostile_aleatoire);
  long dt = (long)(time(NULL) - t0);
  fprintf(stderr, "durée : %ld s\n", dt);
  if (dt > 240) {
    fprintf(stderr, "ECHEC : plus de 240 s\n");
    tt_failures++;
  }
  TT_MAIN_END();
}
