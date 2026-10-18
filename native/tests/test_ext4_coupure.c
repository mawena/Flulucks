#define _GNU_SOURCE
/* Coupures pendant l'écriture ext4 (phase 3, T5) : journal jbd2 v3 correct (correctif lwext4 0004),
 * séquence conservée d'une session à l'autre, barrières d'écriture.
 *
 * Une coupure : copie d'une image du banc sous fault_bdev(writes_ok = N, FL_EIO) ; passé N écritures,
 * plus rien n'atteint la copie (le disque reste figé comme après une coupure de courant), le moteur
 * rend une erreur, puis le démontage libère tout (ASan) en rendant une erreur. Le juge : « e2fsck
 * -fy » rejoue le journal sans autre réparation que bénigne, puis « e2fsck -fn » est propre
 * (fu_e2fsck_recover = 0), comme le demande le § 4.2 (« récupérable par Ubuntu »).
 *
 * Le vérificateur de journal reprend jcheck.py de l'essai du 2026-10-18, aligné sur jbd2 (recovery.c) :
 * sommes crc32c v3 des blocs de descripteur, de révocation, des blocs de données (étiquettes, sur le
 * bloc tel qu'il est dans le journal, donc échappé) et des blocs de commit, calculées ici sans lwext4 ;
 * octets inutilisés de ces blocs à zéro (correctif 0007).
 *
 * Réservé au PC : sauté (code 77) si e2fsck/debugfs/dumpe2fs sont absents, et sur Android. */
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fault_bdev.h"
#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fsck_util.h"
#include "tt.h"

#define MIB ((size_t)1 << 20)

/* ---------- utilitaires ---------- */

static char *temporaires[64];
static size_t ntemp;

static void noter(const char *chemin) {
  if (ntemp < sizeof temporaires / sizeof *temporaires) temporaires[ntemp++] = strdup(chemin);
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

/* Contenu déterministe (xorshift). */
static void remplir(uint8_t *b, size_t n, uint64_t graine) {
  uint64_t x = graine * 0x9E3779B97F4A7C15ull + 1;
  for (size_t i = 0; i < n; i += 8) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    size_t k = n - i < 8 ? n - i : 8;
    memcpy(b + i, &x, k);
  }
}

static int64_t g_t = 1760000000LL;
static int64_t horloge(void) { return g_t; }

typedef struct {
  fl_bdev *file, *dev; /* dev : fault_bdev au-dessus de file */
  fl_fs *fs;
} vol;

/* Monte img en écriture sous fault_bdev(writes_ok). */
static int vol_monter(vol *v, const char *img, uint64_t writes_ok) {
  memset(v, 0, sizeof *v);
  int rc = fl_file_bdev_open(img, 512, false, &v->file);
  if (rc != FL_OK) return rc;
  rc = fl_fault_bdev_new(v->file, writes_ok, FL_EIO, &v->dev);
  if (rc == FL_OK) rc = fl_fs_mount(v->dev, FL_CONTENT_EXT4, true, &v->fs);
  if (rc == FL_OK) {
    fl_statfs s;
    rc = fl_fs_statfs(v->fs, &s);
    if (rc == FL_OK && s.read_only) {
      fprintf(stderr, "monté en lecture seule : %s\n", s.ro_reason);
      rc = FL_EROFS;
    }
    if (rc != FL_OK) fl_fs_unmount(v->fs);
  }
  if (rc != FL_OK) {
    fl_bdev_close(v->dev);
    fl_bdev_close(v->file);
    memset(v, 0, sizeof *v);
  }
  return rc;
}

/* Démontage (propre ou après la panne) ; libère les périphériques dans tous les cas. */
static int vol_demonter(vol *v) {
  int rc = fl_fs_unmount(v->fs);
  fl_bdev_close(v->dev);
  fl_bdev_close(v->file);
  memset(v, 0, sizeof *v);
  return rc;
}

/* Crée (ou remplace) un fichier entier ; première erreur. */
static int ecrire_fichier(fl_fs *fs, const char *chemin, const void *data, size_t n) {
  fl_file *f = NULL;
  int rc = fl_fs_open(fs, chemin, FL_O_RDWR | FL_O_CREAT | FL_O_TRUNC, &f);
  if (rc != FL_OK) return rc;
  int64_t w = n ? fl_file_pwrite(f, data, n, 0) : 0;
  rc = fl_file_close(f);
  if (w != (int64_t)n) return w < 0 ? (int)w : FL_EIO;
  return rc;
}

/* Le juge après coupure. */
static int recuperer(const char *img) {
  static char fy[1 << 16], fn[1 << 16];
  int rejoue = 0, fy_rc = 0;
  int rc = fu_e2fsck_recover(img, &rejoue, &fy_rc, fy, sizeof fy, fn, sizeof fn);
  if (rc != 0) fprintf(stderr, "récupération : %d (e2fsck -fy = %d)\n-- -fy --\n%s\n-- -fn --\n%s\n", rc, fy_rc, fy, fn);
  return rc;
}

/* ---------- vérificateur jbd2 v3 (indépendant de lwext4) ---------- */

static uint32_t crc_t[256];

static void crc_init(void) {
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0x82F63B78u : c >> 1;
    crc_t[i] = c;
  }
}

/* crc32c brut, comme jbd2 (ni inversion initiale ni finale : la graine les porte). */
static uint32_t crc32c(uint32_t c, const void *buf, size_t n) {
  const uint8_t *p = buf;
  while (n--) c = crc_t[(c ^ *p++) & 0xFF] ^ (c >> 8);
  return c;
}

static uint32_t be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

typedef struct {
  uint8_t *j;  /* journal entier (inode 8), lu par debugfs */
  size_t len;
  uint32_t bs, maxlen, first, sequence, start;
} journal;

static int journal_lire(const char *img, journal *jr) {
  char td[4096], fichier[4200], cmd[8400], out[4096];
  memset(jr, 0, sizeof *jr);
  if (fu_tmpdir(td, sizeof td) != 0) return -1;
  snprintf(fichier, sizeof fichier, "%s/journal-%d", td, (int)getpid());
  snprintf(cmd, sizeof cmd, "dump <8> %s", fichier);
  char *const argv[] = {(char *)"debugfs", (char *)"-R", cmd, (char *)img, NULL};
  fu_run(argv, out, sizeof out);
  int fd = open(fichier, O_RDONLY);
  struct stat st;
  int rc = -1;
  if (fd >= 0 && fstat(fd, &st) == 0 && st.st_size >= 1024) {
    jr->len = (size_t)st.st_size;
    jr->j = malloc(jr->len);
    if (jr->j && pread(fd, jr->j, jr->len, 0) == (ssize_t)jr->len) rc = 0;
  }
  if (fd >= 0) close(fd);
  unlink(fichier);
  if (rc != 0) return -1;
  const uint8_t *sb = jr->j;
  if (be32(sb) != 0xC03B3998u) return -1;
  jr->bs = be32(sb + 12);
  jr->maxlen = be32(sb + 16);
  jr->first = be32(sb + 20);
  jr->sequence = be32(sb + 24);
  jr->start = be32(sb + 28);
  if (jr->bs < 1024 || (size_t)jr->maxlen * jr->bs > jr->len) return -1;
  return 0;
}

typedef struct {
  int commits;  /* commits dont toutes les sommes sont justes */
  int fausses;  /* sommes fausses */
  int sales;    /* blocs dont des octets inutilisés ne sont pas nuls */
  int echappes; /* étiquettes ESCAPE */
} bilan;

static bool nuls(const uint8_t *p, size_t n) {
  while (n--)
    if (*p++) return false;
  return true;
}

/* Parcourt le journal depuis le bloc `bloc`, transactions attendues à partir de `seq`. Arrêt au premier
 * bloc qui n'est pas de la transaction attendue (comme le rejeu). */
static void journal_verifier(const journal *jr, uint32_t bloc, uint32_t seq, bilan *r) {
  const uint8_t *sb = jr->j;
  uint32_t graine = crc32c(0xFFFFFFFFu, sb + 48, 16);
  uint8_t *tmp = malloc(jr->bs);
  memset(r, 0, sizeof *r);
  if (!tmp) {
    r->fausses++;
    return;
  }
#define BLOC(n) (jr->j + (size_t)(jr->first + ((n) - jr->first) % (jr->maxlen - jr->first)) * jr->bs)
  for (uint32_t pas = 0; pas < jr->maxlen; pas++) {
    const uint8_t *b = BLOC(bloc);
    if (be32(b) != 0xC03B3998u || be32(b + 8) != seq) break;
    uint32_t type = be32(b + 4);
    if (type == 1) { /* descripteur */
      memcpy(tmp, b, jr->bs);
      memset(tmp + jr->bs - 4, 0, 4);
      if (crc32c(graine, tmp, jr->bs) != be32(b + jr->bs - 4)) r->fausses++;
      uint32_t off = 12, jb = bloc;
      while (off + 16 <= jr->bs - 4) {
        uint32_t flags = be32(b + off + 4), somme = be32(b + off + 12);
        off += (flags & 2) ? 16 : 32; /* SAME_UUID, sinon l'uuid suit */
        jb++;
        /* jbd2 vérifie la somme sur le bloc du journal, avant de remettre le magique (ESCAPE) */
        const uint8_t *d = BLOC(jb);
        if (flags & 1) {
          r->echappes++;
          if (be32(d) != 0) r->fausses++; /* le magique doit être effacé dans le journal */
        }
        uint8_t s[4] = {(uint8_t)(seq >> 24), (uint8_t)(seq >> 16), (uint8_t)(seq >> 8), (uint8_t)seq};
        if (crc32c(crc32c(graine, s, 4), d, jr->bs) != somme) r->fausses++;
        if (flags & 8) break; /* LAST_TAG */
      }
      if (off < jr->bs - 4 && !nuls(b + off, jr->bs - 4 - off)) r->sales++;
      bloc = jb + 1;
    } else if (type == 2) { /* commit */
      memcpy(tmp, b, jr->bs);
      memset(tmp + 16, 0, 4);
      bool juste = crc32c(graine, tmp, jr->bs) == be32(b + 16);
      if (!juste) r->fausses++;
      else r->commits++;
      /* type et taille de somme (v1) nuls, h_chksum[1..7] et le reste du bloc nuls */
      if (!nuls(b + 12, 4) || !nuls(b + 20, jr->bs - 20)) r->sales++;
      bloc++;
      seq++;
    } else if (type == 5) { /* révocation : somme de queue, octets après r_count nuls */
      memcpy(tmp, b, jr->bs);
      memset(tmp + jr->bs - 4, 0, 4);
      if (crc32c(graine, tmp, jr->bs) != be32(b + jr->bs - 4)) r->fausses++;
      uint32_t n = be32(b + 12);
      if (n < 16 || n > jr->bs - 4) r->fausses++;
      else if (!nuls(b + n, jr->bs - 4 - n)) r->sales++;
      bloc++;
    } else {
      bloc++;
    }
  }
#undef BLOC
  free(tmp);
}

/* ---------- charges ---------- */

/* Un fichier de `n` octets par tranches de 1 Mio ; première erreur (FL_OK si tout est écrit). */
static int charge_gros(fl_fs *fs, const char *chemin, size_t n) {
  uint8_t *b = malloc(MIB);
  if (!b) return FL_ENOMEM;
  fl_file *f = NULL;
  int rc = fl_fs_open(fs, chemin, FL_O_RDWR | FL_O_CREAT, &f);
  for (size_t o = 0; rc == FL_OK && o < n; o += MIB) {
    remplir(b, MIB, o / MIB);
    int64_t w = fl_file_pwrite(f, b, MIB, o);
    if (w != (int64_t)MIB) rc = w < 0 ? (int)w : FL_EIO;
  }
  if (f) {
    int rc2 = fl_file_close(f);
    if (rc == FL_OK) rc = rc2;
  }
  free(b);
  return rc;
}

/* `n` fichiers de 0 à 222 octets dans `dossier` ; première erreur. */
static int charge_fichiers(fl_fs *fs, const char *dossier, int n, uint64_t graine) {
  uint8_t data[7 * 37];
  char p[256];
  int rc = fl_fs_mkdir(fs, dossier);
  for (int i = 0; rc == FL_OK && i < n; i++) {
    snprintf(p, sizeof p, "%s/entree_avec_un_nom_assez_long_%05d.txt", dossier, i);
    size_t k = (size_t)(i % 7) * 37;
    remplir(data, k, graine + (uint64_t)i);
    rc = ecrire_fichier(fs, p, data, k);
  }
  return rc;
}

/* Une coupure : la charge doit échouer (ou le démontage), le démontage libère tout, puis le juge. */
typedef int (*charge_fn)(fl_fs *fs);

static int charge_gros300(fl_fs *fs) { return charge_gros(fs, "/coupe.bin", 300 * MIB); }
static int charge_3000(fl_fs *fs) { return charge_fichiers(fs, "/d3000", 3000, 1); }

static void couper(const char *fixture, uint64_t n, charge_fn charge) {
  char img[4096];
  vol v;
  TT_ASSERT(copie_de(fixture, img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, n), FL_OK);
  int rc = charge(v.fs);
  uint64_t ecrites = fl_fault_bdev_writes(v.dev);
  int ru = vol_demonter(&v);
  fprintf(stderr, "%s, N = %llu : charge %d, démontage %d (%llu écritures)\n", fixture, (unsigned long long)n,
          rc, ru, (unsigned long long)ecrites);
  TT_ASSERT(rc != FL_OK || ru != FL_OK); /* la panne est signalée */
  TT_ASSERT(rc == FL_OK || rc == FL_EIO);
  TT_ASSERT(ru == FL_OK || ru == FL_EIO);
  TT_ASSERT_EQ(recuperer(img), 0);
  unlink(img);
}

/* ---------- tests ---------- */

static void test_ext4_coupure_gros_fichier(void) {
  static const uint64_t ns[] = {50, 500, 2000, 3000, 4000, 4500};
  for (size_t i = 0; i < sizeof ns / sizeof *ns; i++) couper("ext4-rw-gros.img", ns[i], charge_gros300);
}

static void test_ext4_coupure_3000(void) {
  static const uint64_t ns[] = {500, 5000, 20000, 40000};
  for (size_t i = 0; i < sizeof ns / sizeof *ns; i++) couper("ext4-rw-reel.img", ns[i], charge_3000);
}

#define NFERMES 20
#define TFERME (64 * 1024)

static int ecrire_fermes(fl_fs *fs) {
  uint8_t *b = malloc(TFERME);
  int rc = b ? FL_OK : FL_ENOMEM;
  for (int i = 0; rc == FL_OK && i < NFERMES; i++) {
    char p[64];
    snprintf(p, sizeof p, "/ferme-%02d.bin", i);
    remplir(b, TFERME, 900 + (uint64_t)i);
    rc = ecrire_fichier(fs, p, b, TFERME);
  }
  free(b);
  return rc;
}

/* Fichiers écrits et fermés (synchronisés), puis coupure en pleine écriture d'un gros fichier : les
 * fichiers fermés sont intacts après récupération. */
static void test_ext4_coupure_fichiers_fermes_intacts(void) {
  char img[4096];
  vol v;
  /* Nombre d'écritures des fichiers fermés (déterministe), mesuré sur une première copie. */
  TT_ASSERT(copie_de("ext4-rw-gros.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, UINT64_MAX), FL_OK);
  TT_ASSERT_EQ(ecrire_fermes(v.fs), FL_OK);
  uint64_t w = fl_fault_bdev_writes(v.dev);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  unlink(img);

  TT_ASSERT(copie_de("ext4-rw-gros.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(vol_monter(&v, img, w + 2000), FL_OK);
  TT_ASSERT_EQ(ecrire_fermes(v.fs), FL_OK);
  int rc = charge_gros300(v.fs);
  int ru = vol_demonter(&v);
  TT_ASSERT_EQ(rc, FL_EIO);
  TT_ASSERT_EQ(ru, FL_EIO);
  TT_ASSERT_EQ(recuperer(img), 0);
  uint8_t *b = malloc(TFERME), *lu = malloc(TFERME + 1);
  TT_ASSERT(b && lu);
  for (int i = 0; i < NFERMES; i++) {
    char p[64];
    snprintf(p, sizeof p, "/ferme-%02d.bin", i);
    remplir(b, TFERME, 900 + (uint64_t)i);
    TT_ASSERT_EQ(fu_debugfs_cat(img, p, lu, TFERME + 1), TFERME);
    TT_ASSERT_MEM_EQ(lu, b, TFERME);
  }
  free(b);
  free(lu);
  unlink(img);
}

/* Le contenu de `n` fichiers d'une session (ceux de charge_fichiers), lu par debugfs. */
static bool session_intacte(const char *img, const char *dossier, int n, uint64_t graine) {
  uint8_t data[7 * 37], lu[7 * 37 + 1];
  for (int i = 0; i < n; i++) {
    char p[256];
    snprintf(p, sizeof p, "%s/entree_avec_un_nom_assez_long_%05d.txt", dossier, i);
    size_t k = (size_t)(i % 7) * 37;
    remplir(data, k, graine + (uint64_t)i);
    int64_t r = fu_debugfs_cat(img, p, lu, sizeof lu);
    if (r != (int64_t)k || memcmp(lu, data, k) != 0) {
      fprintf(stderr, "%s : %lld octets lus, attendu %zu\n", p, (long long)r, k);
      return false;
    }
  }
  return true;
}

/* Défaut de séquence de l'essai : la séquence du journal repartait de 0 à chaque session, et une
 * coupure à la troisième rejouait des transactions des précédentes (fichiers dans lost+found, dossier
 * vidé). Deux sessions propres, puis une troisième coupée. */
static void test_ext4_coupure_sessions(void) {
  static const uint64_t ns[] = {100, 1000};
  for (size_t i = 0; i < sizeof ns / sizeof *ns; i++) {
    char img[4096];
    vol v;
    journal j1, j2;
    TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
    TT_ASSERT_EQ(vol_monter(&v, img, UINT64_MAX), FL_OK);
    TT_ASSERT_EQ(charge_fichiers(v.fs, "/s1", 50, 100), FL_OK);
    TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
    TT_ASSERT_EQ(journal_lire(img, &j1), 0);
    TT_ASSERT_EQ(vol_monter(&v, img, UINT64_MAX), FL_OK);
    TT_ASSERT_EQ(charge_fichiers(v.fs, "/s2", 400, 200), FL_OK);
    TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
    TT_ASSERT_EQ(journal_lire(img, &j2), 0);
    /* journal propre, séquence croissante d'une session à l'autre */
    TT_ASSERT_EQ(j1.start, 0);
    TT_ASSERT_EQ(j2.start, 0);
    TT_ASSERT(j2.sequence > j1.sequence + 400);
    free(j1.j);
    free(j2.j);
    TT_ASSERT_EQ(vol_monter(&v, img, ns[i]), FL_OK);
    int rc = charge_fichiers(v.fs, "/s3", 400, 300);
    int ru = vol_demonter(&v);
    TT_ASSERT(rc == FL_EIO || ru == FL_EIO);
    TT_ASSERT_EQ(recuperer(img), 0); /* ni lost+found ni « unused area » : réparations bénignes seules */
    TT_ASSERT(session_intacte(img, "/s1", 50, 100));
    TT_ASSERT(session_intacte(img, "/s2", 400, 200));
    unlink(img);
  }
}

/* Les sommes v3 écrites par lwext4 sont celles de jbd2 (avant le correctif 0004 : séquence hachée dans
 * l'ordre de l'hôte, somme de commit stockée sans to_be32), et les octets inutilisés des blocs du
 * journal sont nuls (avant 0007 : restes du cache). */
static void test_ext4_journal_sommes(void) {
  char img[4096];
  vol v;
  journal avant, apres;
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(journal_lire(img, &avant), 0);
  TT_ASSERT_EQ(vol_monter(&v, img, UINT64_MAX), FL_OK);
  TT_ASSERT_EQ(charge_fichiers(v.fs, "/j", 50, 7), FL_OK);
  /* suppressions : blocs de révocation */
  for (int i = 0; i < 50; i += 2) {
    char p[256];
    snprintf(p, sizeof p, "/j/entree_avec_un_nom_assez_long_%05d.txt", i);
    TT_ASSERT_EQ(fl_fs_unlink(v.fs, p), FL_OK);
  }
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(journal_lire(img, &apres), 0);
  /* la session écrit à partir du premier bloc du journal, transactions sequence + 1, + 2… */
  bilan r;
  journal_verifier(&apres, apres.first, avant.sequence + 1, &r);
  fprintf(stderr, "journal : %d commits, %d sommes fausses, %d blocs sales, séquence %u → %u\n", r.commits,
          r.fausses, r.sales, avant.sequence, apres.sequence);
  TT_ASSERT_EQ(r.fausses, 0);
  TT_ASSERT_EQ(r.sales, 0);
  TT_ASSERT(r.commits >= 75);
  /* s_sequence garde la prochaine transaction (jbd2), au lieu de repartir de 0 */
  TT_ASSERT(apres.sequence >= avant.sequence + 1 + (uint32_t)r.commits);
  free(avant.j);
  free(apres.j);
  unlink(img);
}

/* Barrières, dans l'ordre : chaque bloc de commit est écrit seul entre deux vidages (le premier rend
 * stables le descripteur, les données et les révocations de la transaction ; le second précède toute
 * écriture en place), et il y a autant de commits dans la trace que dans le journal. */
static void test_ext4_barrieres(void) {
  char img[4096];
  vol v;
  journal avant, apres;
  static char trace[1 << 16];
  TT_ASSERT(copie_de("ext4-rw-reel.img", img, sizeof img) == 0);
  TT_ASSERT_EQ(journal_lire(img, &avant), 0);
  TT_ASSERT_EQ(vol_monter(&v, img, UINT64_MAX), FL_OK);
  fl_fault_bdev_trace(v.dev, avant.bs, trace, sizeof trace);
  uint8_t b[10000];
  remplir(b, sizeof b, 3);
  fl_file *f = NULL;
  TT_ASSERT_EQ(fl_fs_open(v.fs, "/barriere.bin", FL_O_RDWR | FL_O_CREAT, &f), FL_OK);
  TT_ASSERT_EQ(fl_file_pwrite(f, b, sizeof b, 0), (int64_t)sizeof b);
  TT_ASSERT_EQ(fl_fs_mkdir(v.fs, "/barriere-d"), FL_OK);
  TT_ASSERT_EQ(fl_file_close(f), FL_OK);
  TT_ASSERT_EQ(charge_fichiers(v.fs, "/barriere-n", 20, 9), FL_OK);
  TT_ASSERT_EQ(fl_fs_unlink(v.fs, "/barriere.bin"), FL_OK);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK); /* la trace reste dans le tampon */
  TT_ASSERT_EQ(journal_lire(img, &apres), 0);
  bilan r;
  journal_verifier(&apres, apres.first, avant.sequence + 1, &r);
  size_t n = strlen(trace), c = 0, mal = 0;
  for (size_t i = 0; i < n; i++) {
    if (trace[i] != 'C') continue;
    c++;
    if (i == 0 || trace[i - 1] != 'F' || i + 1 >= n || trace[i + 1] != 'F') mal++;
  }
  fprintf(stderr, "barrières : %d transactions, %zu commits tracés, %zu mal encadrés\n", r.commits, c, mal);
  TT_ASSERT(n + 1 < sizeof trace); /* trace complète */
  TT_ASSERT_EQ(r.fausses, 0);
  TT_ASSERT(r.commits >= 23); /* création, écriture, mkdir, 20 fichiers, suppression */
  TT_ASSERT_EQ(c, (size_t)r.commits);
  TT_ASSERT_EQ(mal, 0);
  free(avant.j);
  free(apres.j);
  unlink(img);
}

/* Copie de ext4-rw-reel.img dont le premier bloc de la table d'inodes commence par le magique jbd2
 * (i_mode et i_uid de l'inode 1, réservé) : tout bloc journalisé qui contient l'inode racine (2) est
 * alors échappé. e2fsck n'aime pas cet inode 1 : l'image ne sert qu'aux tests du journal. */
static int copie_magique(char *img, size_t cap) {
  if (copie_de("ext4-rw-reel.img", img, cap) != 0) return -1;
  char out[4096];
  char *const a1[] = {(char *)"debugfs", (char *)"-w", (char *)"-R", (char *)"sif <1> mode 0x3bc0", img, NULL};
  char *const a2[] = {(char *)"debugfs", (char *)"-w", (char *)"-R", (char *)"sif <1> uid 0x9839", img, NULL};
  if (fu_run(a1, out, sizeof out) != 0 || fu_run(a2, out, sizeof out) != 0) return -1;
  return 0;
}

static int charge_racine(fl_fs *fs) {
  int rc = FL_OK;
  for (int i = 0; rc == FL_OK && i < 5; i++) {
    char p[64];
    snprintf(p, sizeof p, "/echappe-%d.txt", i);
    rc = ecrire_fichier(fs, p, p, strlen(p));
  }
  return rc;
}

/* Blocs échappés : la somme d'étiquette porte sur le bloc tel qu'il est dans le journal (magique
 * effacé), comme jbd2 la vérifie ; avant 0007, lwext4 sommait le bloc d'origine. */
static void test_ext4_journal_echappement(void) {
  char img[4096];
  vol v;
  journal avant, apres;
  TT_ASSERT_EQ(copie_magique(img, sizeof img), 0);
  TT_ASSERT_EQ(journal_lire(img, &avant), 0);
  TT_ASSERT_EQ(vol_monter(&v, img, UINT64_MAX), FL_OK);
  TT_ASSERT_EQ(charge_racine(v.fs), FL_OK);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  TT_ASSERT_EQ(journal_lire(img, &apres), 0);
  bilan r;
  journal_verifier(&apres, apres.first, avant.sequence + 1, &r);
  fprintf(stderr, "échappement : %d commits, %d étiquettes échappées, %d sommes fausses\n", r.commits,
          r.echappes, r.fausses);
  TT_ASSERT(r.echappes >= 5);
  TT_ASSERT_EQ(r.fausses, 0);
  TT_ASSERT_EQ(r.sales, 0);
  free(avant.j);
  free(apres.j);
  unlink(img);
}

/* Coupure juste après le premier commit (bloc échappé compris), avant toute écriture en place : e2fsck
 * rejoue le journal sans rejeter de bloc, et le fichier créé (vide) est là. */
static void test_ext4_coupure_echappement(void) {
  char img[4096];
  vol v;
  static char trace[1 << 16];
  /* 1. session complète tracée : nombre d'écritures jusqu'au premier commit inclus */
  TT_ASSERT_EQ(copie_magique(img, sizeof img), 0);
  journal j;
  TT_ASSERT_EQ(journal_lire(img, &j), 0);
  free(j.j);
  TT_ASSERT_EQ(vol_monter(&v, img, UINT64_MAX), FL_OK);
  fl_fault_bdev_trace(v.dev, j.bs, trace, sizeof trace);
  uint64_t w0 = fl_fault_bdev_writes(v.dev);
  TT_ASSERT_EQ(ecrire_fichier(v.fs, "/echappe-0.txt", "coupure\n", 8), FL_OK);
  TT_ASSERT_EQ(vol_demonter(&v), FL_OK);
  unlink(img);
  uint64_t n = w0;
  const char *c = strchr(trace, 'C');
  TT_ASSERT(c != NULL);
  for (const char *t = trace; t <= c; t++)
    if (*t != 'F') n++;
  /* 2. même session, coupée après ce commit */
  TT_ASSERT_EQ(copie_magique(img, sizeof img), 0);
  TT_ASSERT_EQ(vol_monter(&v, img, n), FL_OK);
  int rc = ecrire_fichier(v.fs, "/echappe-0.txt", "coupure\n", 8);
  int ru = vol_demonter(&v);
  TT_ASSERT(rc != FL_OK || ru != FL_OK);
  /* 3. rejeu seul (l'inode 1 retouché ferait réparer un e2fsck complet) */
  static char out[1 << 16];
  char *const argv[] = {(char *)"e2fsck", (char *)"-fy", (char *)"-E", (char *)"journal_only", img, NULL};
  int e = fu_run(argv, out, sizeof out);
  if (strstr(out, "checksum") || strstr(out, "Checksum")) fprintf(stderr, "e2fsck = %d\n%s\n", e, out);
  TT_ASSERT(strstr(out, "checksum") == NULL && strstr(out, "Checksum") == NULL);
  TT_ASSERT(e < 4);
  /* le premier commit est la création (le contenu vient dans une transaction suivante) : le nouvel
   * inode est dans le bloc échappé (inodes 1 à 16), il doit avoir été rejoué */
  char lu[16];
  TT_ASSERT_EQ(fu_debugfs_cat(img, "/echappe-0.txt", lu, sizeof lu), 0);
  uint64_t mode = 0;
  TT_ASSERT_EQ(fu_debugfs_inode_field(img, "/echappe-0.txt", "mode", &mode), 0);
  TT_ASSERT(mode != 0);
  unlink(img);
}

int main(void) {
  if (!fu_tools_available()) {
    printf("SAUTÉ : e2fsck/debugfs/dumpe2fs absents ou plateforme Android (banc réservé au PC)\n");
    return 77; /* SKIP_RETURN_CODE de CTest */
  }
  crc_init();
  fl_fs_set_clock(horloge);
  TT_RUN(test_ext4_journal_sommes);
  TT_RUN(test_ext4_barrieres);
  TT_RUN(test_ext4_journal_echappement);
  TT_RUN(test_ext4_coupure_echappement);
  TT_RUN(test_ext4_coupure_sessions);
  TT_RUN(test_ext4_coupure_fichiers_fermes_intacts);
  TT_RUN(test_ext4_coupure_gros_fichier);
  TT_RUN(test_ext4_coupure_3000);
  fl_fs_set_clock(NULL);
  nettoyer();
  TT_MAIN_END();
}
