#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fake_msc.h"
#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fluluks/luks.h"
#include "fluluks/partition.h"
#include "fluluks/usb.h"
#include "tt.h"
#include "usb/usb_internal.h"

#define KIB 1024u
#define MIB (1024u * 1024u)

/* ---------- horloge virtuelle : les attentes ne coûtent rien ---------- */

typedef struct {
  uint64_t now;
  unsigned sleeps;
} horloge;

static uint64_t h_now(void *ctx) { return ((horloge *)ctx)->now; }
static void h_sleep(void *ctx, unsigned ms) {
  horloge *h = ctx;
  h->now += ms;
  h->sleeps++;
}

static horloge g_h;
static fl_usb_clock g_clk = {h_now, h_sleep, &g_h};

static int ouvrir_usb(fake_msc *m, bool ro, fl_bdev **out) {
  memset(&g_h, 0, sizeof g_h);
  return fl_usb_bdev_open_ex(fake_msc_transport(m), 0, ro, &g_clk, out);
}

static fake_msc *faux(const char *img) {
  fake_msc *m = NULL;
  if (fake_msc_new(tt_fixture(img), &m) != FL_OK) return NULL;
  return m;
}

static uint64_t taille(const char *img) {
  struct stat st;
  if (stat(tt_fixture(img), &st) != 0) return 0;
  return (uint64_t)st.st_size;
}

/* Lecture de référence depuis le fichier image. */
static int lire_ref(const char *img, uint64_t off, size_t len, void *buf) {
  int fd = open(tt_fixture(img), O_RDONLY);
  if (fd < 0) return -1;
  ssize_t r = pread(fd, buf, len, (off_t)off);
  close(fd);
  return r == (ssize_t)len ? 0 : -1;
}

static size_t lire_pass(char *buf, size_t cap) {
  FILE *f = fopen(tt_fixture("pass.txt"), "rb");
  if (!f) return 0;
  size_t n = fread(buf, 1, cap, f);
  fclose(f);
  return n;
}

/* Pile complète : partitions → LUKS2 → ext4 → /hello.txt. Renvoie FL_OK ou la première erreur. */
static int pile_hello(fl_bdev *disk, char *out, size_t cap, int64_t *n) {
  fl_part_table t;
  int rc = fl_part_scan(disk, &t);
  if (rc != FL_OK) return rc;
  if (t.count < 1) return FL_ENOENT;
  fl_bdev *part = NULL, *dec = NULL;
  fl_luks *l = NULL;
  fl_fs *fs = NULL;
  fl_file *f = NULL;
  rc = fl_slice_bdev_new(disk, t.parts[0].first_lba, t.parts[0].sector_count, &part);
  if (rc == FL_OK) rc = fl_luks_open(part, &l);
  char pass[256];
  size_t pl = lire_pass(pass, sizeof pass);
  if (rc == FL_OK && !pl) rc = FL_EIO;
  if (rc == FL_OK) rc = fl_luks_unlock(l, pass, pl, &dec);
  if (rc == FL_OK) rc = fl_fs_mount(dec, FL_CONTENT_EXT4, false, &fs);
  if (rc == FL_OK) rc = fl_fs_open(fs, "/hello.txt", FL_O_RDONLY, &f);
  if (rc == FL_OK) {
    *n = fl_file_pread(f, out, cap, 0);
    if (*n < 0) rc = (int)*n;
  }
  fl_file_close(f);
  fl_fs_unmount(fs);
  fl_bdev_close(dec);
  fl_luks_close(l);
  fl_bdev_close(part);
  return rc;
}

/* Codes d'opération SCSI qui écrivent (ou modifient) le support : aucun ne doit jamais partir. */
static const uint8_t OPS_ECRITURE[] = {
    0x04, /* FORMAT UNIT */           0x07, /* REASSIGN BLOCKS */     0x0A, /* WRITE(6) */
    0x15, /* MODE SELECT(6) */        0x2A, /* WRITE(10) */           0x2E, /* WRITE AND VERIFY(10) */
    0x35, /* SYNCHRONIZE CACHE(10) */ 0x3B, /* WRITE BUFFER */        0x3F, /* WRITE LONG(10) */
    0x41, /* WRITE SAME(10) */        0x42, /* UNMAP */               0x55, /* MODE SELECT(10) */
    0x89, /* COMPARE AND WRITE */     0x8A, /* WRITE(16) */           0x8E, /* WRITE AND VERIFY(16) */
    0x91, /* SYNCHRONIZE CACHE(16) */ 0x93, /* WRITE SAME(16) */      0xAA, /* WRITE(12) */
    0xAE, /* WRITE AND VERIFY(12) */
};

static unsigned ecritures_vues(const fake_msc_stats *s) {
  unsigned n = s->writes;
  for (size_t i = 0; i < sizeof OPS_ECRITURE; i++) n += s->op[OPS_ECRITURE[i]];
  return n;
}

/* Commandes reçues hors du jeu de lecture (TUR, REQUEST SENSE, INQUIRY, READ CAPACITY(10)/(16),
 * READ(10)/(16)) ; START STOP UNIT n'est toléré que si `start_permis`. */
static unsigned hors_lecture(const fake_msc_stats *s, bool start_permis) {
  unsigned n = 0;
  for (unsigned op = 0; op < 256; op++) {
    switch (op) {
      case 0x00: case 0x03: case 0x12: case 0x25: case 0x9E: case 0x28: case 0x88:
        break;
      case 0x1B:
        if (!start_permis) n += s->op[op];
        break;
      default:
        n += s->op[op];
    }
  }
  return n;
}

/* ---------- tests ---------- */

static void test_usb_capacite(void) {
  fake_msc *m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  TT_ASSERT_EQ(d->sector_size, 512);
  TT_ASSERT_EQ(d->sector_count, taille("gpt-luks2.img") / 512);
  TT_ASSERT(d->read_only);
  fake_msc_stats *s = fake_msc_stats_get(m);
  TT_ASSERT_EQ(s->op[0x12], 1); /* INQUIRY */
  TT_ASSERT_EQ(s->op[0x00], 1); /* TEST UNIT READY */
  TT_ASSERT_EQ(s->op[0x25], 1); /* READ CAPACITY(10) */
  TT_ASSERT_EQ(s->op[0x9E], 0); /* pas de READ CAPACITY(16) */
  TT_ASSERT_EQ(s->op[0x1B], 0); /* disque prêt : jamais de START STOP UNIT */
  TT_ASSERT_EQ(hors_lecture(s, false), 0);
  fl_bdev_close(d);
  fake_msc_free(m);
}

static void test_usb_lecture_identique(void) {
  const char *img = "gpt-luks2.img";
  fake_msc *m = faux(img);
  TT_ASSERT(m != NULL);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  uint8_t *a = malloc(2 * MIB), *b = malloc(2 * MIB);
  TT_ASSERT(a && b);
  uint64_t size = taille(img);
  /* 3 Mio au total, décalages variés (alignés, non alignés, fin du disque). */
  struct {
    uint64_t off;
    size_t len;
  } z[] = {
      {0, 512 * KIB},
      {7 * 512, 300 * KIB + 3 * 512},
      {MIB + 123, 1 * MIB - 4567},
      {20 * MIB + 511, 200 * KIB + 1},
  };
  size_t total = 0;
  for (size_t i = 0; i < 4; i++) {
    TT_ASSERT_EQ(fl_bdev_read_bytes(d, z[i].off, z[i].len, a), FL_OK);
    TT_ASSERT_EQ(lire_ref(img, z[i].off, z[i].len, b), 0);
    TT_ASSERT_MEM_EQ(a, b, z[i].len);
    total += z[i].len;
  }
  size_t tail = 3 * MIB - total; /* le reste des 3 Mio : jusqu'au dernier octet du disque */
  TT_ASSERT_EQ(fl_bdev_read_bytes(d, size - tail, tail, a), FL_OK);
  TT_ASSERT_EQ(lire_ref(img, size - tail, tail, b), 0);
  TT_ASSERT_MEM_EQ(a, b, tail);
  TT_ASSERT_EQ(fl_bdev_read(d, d->sector_count, 1, a), FL_EINVAL);

  /* 1 Mio = 16 commandes de 64 Kio. */
  fake_msc_reset_stats(m);
  TT_ASSERT_EQ(fl_bdev_read(d, 4096, 2048, a), FL_OK);
  TT_ASSERT_EQ(lire_ref(img, 4096ull * 512, MIB, b), 0);
  TT_ASSERT_MEM_EQ(a, b, MIB);
  fake_msc_stats *s = fake_msc_stats_get(m);
  TT_ASSERT_EQ(s->op[0x28], 16);
  TT_ASSERT_EQ(s->commands, 16);
  TT_ASSERT_EQ(s->max_read_xfer, 64 * KIB);
  free(a);
  free(b);
  fl_bdev_close(d);
  fake_msc_free(m);
}

static void test_usb_disque_lent(void) {
  fake_msc *m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  fake_msc_not_ready(m, 5);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  fake_msc_stats *s = fake_msc_stats_get(m);
  TT_ASSERT_EQ(s->op[0x00], 6); /* 5 refus puis succès */
  TT_ASSERT_EQ(s->op[0x03], 5); /* un REQUEST SENSE par refus */
  TT_ASSERT_EQ(g_h.sleeps, 5);
  TT_ASSERT(g_h.now < FL_USB_READY_BUDGET_MS);
  TT_ASSERT_EQ(s->op[0x1B], 0); /* 04/01 « becoming ready » : on attend, sans START */
  TT_ASSERT_EQ(hors_lecture(s, false), 0);
  fl_bdev_close(d);
  fake_msc_free(m);
}

static void test_usb_disque_jamais_pret(void) {
  fake_msc *m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  fake_msc_not_ready(m, 1000000);
  fl_bdev *d = (fl_bdev *)1;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_ETIMEDOUT);
  TT_ASSERT(d == NULL);
  TT_ASSERT(g_h.now >= FL_USB_READY_BUDGET_MS);
  TT_ASSERT(g_h.now <= FL_USB_READY_BUDGET_MS + FL_USB_READY_POLL_MS);
  TT_ASSERT_EQ(fake_msc_stats_get(m)->op[0x1B], 0);
  fake_msc_free(m);
}

/* Disque arrêté (02/04/02) : un START STOP UNIT (START=1, LOEJ=0, IMMED=0), puis l'ouverture réussit. */
static void test_usb_demarrage(void) {
  const char *img = "gpt-luks2.img";
  fake_msc *m = faux(img);
  TT_ASSERT(m != NULL);
  fake_msc_require_start(m);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  fake_msc_stats *s = fake_msc_stats_get(m);
  TT_ASSERT_EQ(s->op[0x1B], 1);
  TT_ASSERT_EQ(s->start_stop[0], 0x1B);
  TT_ASSERT_EQ(s->start_stop[1] & 0x01, 0); /* IMMED=0 */
  TT_ASSERT_EQ(s->start_stop[4], 0x01);     /* START=1, LOEJ=0 */
  TT_ASSERT_EQ(s->op[0x00], 2);             /* TUR refusé, START, TUR accepté */
  TT_ASSERT_EQ(d->sector_count, taille(img) / 512);
  uint8_t a[4096], b[4096];
  TT_ASSERT_EQ(fl_bdev_read(d, 0, 8, a), FL_OK);
  TT_ASSERT_EQ(lire_ref(img, 0, sizeof b, b), 0);
  TT_ASSERT_MEM_EQ(a, b, sizeof a);
  TT_ASSERT_EQ(ecritures_vues(s), 0);
  TT_ASSERT_EQ(hors_lecture(s, true), 0);
  fl_bdev_close(d);
  fake_msc_free(m);

  /* START sans effet : envoyé UNE seule fois, puis attente ordinaire jusqu'au délai. */
  m = faux(img);
  TT_ASSERT(m != NULL);
  fake_msc_not_ready_sense(m, 1000000, 0x04, 0x02);
  d = (fl_bdev *)1;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_ETIMEDOUT);
  TT_ASSERT(d == NULL);
  s = fake_msc_stats_get(m);
  TT_ASSERT_EQ(s->op[0x1B], 1);
  TT_ASSERT(g_h.now >= FL_USB_READY_BUDGET_MS);
  TT_ASSERT_EQ(ecritures_vues(s), 0);
  TT_ASSERT_EQ(hors_lecture(s, true), 0);
  fake_msc_free(m);
}

/* 02/04/03 « intervention manuelle requise » : échec immédiat, sans attente ni START. */
static void test_usb_intervention_manuelle(void) {
  fake_msc *m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  fake_msc_not_ready_sense(m, 1000000, 0x04, 0x03);
  fl_bdev *d = (fl_bdev *)1;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_EIO);
  TT_ASSERT(d == NULL);
  TT_ASSERT_EQ(g_h.sleeps, 0);
  TT_ASSERT_EQ(g_h.now, 0);
  fake_msc_stats *s = fake_msc_stats_get(m);
  TT_ASSERT_EQ(s->op[0x00], 1);
  TT_ASSERT_EQ(s->op[0x1B], 0);
  TT_ASSERT_EQ(ecritures_vues(s), 0);
  TT_ASSERT_EQ(hors_lecture(s, false), 0);
  fake_msc_free(m);
}

/* Paquet de longueur nulle à la place du CSW : relu une fois, sans reprise. */
static void test_usb_csw_paquet_vide(void) {
  const char *img = "gpt-luks2.img";
  fake_msc *m = faux(img);
  TT_ASSERT(m != NULL);
  fake_msc_zlp_before_csw(m, 3); /* INQUIRY, TUR, READ CAPACITY */
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  fake_msc_stats *s = fake_msc_stats_get(m);
  TT_ASSERT_EQ(s->zlps, 3);
  TT_ASSERT_EQ(s->resets, 0);
  static uint8_t a[64 * KIB], b[64 * KIB];
  fake_msc_reset_stats(m);
  fake_msc_zlp_before_csw(m, 1);
  TT_ASSERT_EQ(fl_bdev_read(d, 2048, 128, a), FL_OK);
  TT_ASSERT_EQ(lire_ref(img, 2048ull * 512, sizeof b, b), 0);
  TT_ASSERT_MEM_EQ(a, b, sizeof a);
  TT_ASSERT_EQ(s->zlps, 1);
  TT_ASSERT_EQ(s->resets, 0);
  TT_ASSERT_EQ(s->op[0x28], 1);
  TT_ASSERT_EQ(ecritures_vues(s), 0);
  fl_bdev_close(d);
  fake_msc_free(m);
}

/* Pont qui annonce un résidu non nul en envoyant toutes les données : détecté sur INQUIRY,
 * ensuite seuls les octets reçus comptent. */
static void test_usb_residu_fantaisiste(void) {
  const char *img = "gpt-luks2.img";
  static const uint32_t residus[] = {8, 0x10000}; /* plausible, puis plus grand que la demande */
  static uint8_t a[64 * KIB], b[64 * KIB];
  TT_ASSERT_EQ(lire_ref(img, 2048ull * 512, sizeof b, b), 0);
  for (size_t i = 0; i < 2; i++) {
    fake_msc *m = faux(img);
    TT_ASSERT(m != NULL);
    fake_msc_bogus_residue(m, residus[i]);
    fl_bdev *d = NULL;
    TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
    TT_ASSERT_EQ(d->sector_count, taille(img) / 512);
    fake_msc_stats *s = fake_msc_stats_get(m);
    TT_ASSERT_EQ(s->op[0x25], 1); /* READ CAPACITY(10) accepté du premier coup */
    TT_ASSERT_EQ(s->resets, 0);
    fake_msc_reset_stats(m);
    memset(a, 0, sizeof a);
    TT_ASSERT_EQ(fl_bdev_read(d, 2048, 128, a), FL_OK);
    TT_ASSERT_MEM_EQ(a, b, sizeof a);
    TT_ASSERT_EQ(s->op[0x28], 1);
    TT_ASSERT_EQ(ecritures_vues(s), 0);
    TT_ASSERT_EQ(hors_lecture(s, false), 0);
    fl_bdev_close(d);
    fake_msc_free(m);
  }
}

/* Au niveau BOT : l'heuristique ne se déclenche que sur un INQUIRY(36) ou READ CAPACITY(10)
 * complet, réussi, au résidu non nul. */
static void test_usb_residu_heuristique(void) {
  fake_msc *m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  fl_usb_dev u;
  memset(&u, 0, sizeof u);
  u.t = *fake_msc_transport(m);
  u.clk = g_clk;
  uint8_t inq[6] = {0x12, 0, 0, 0, 36, 0}, rc10[10] = {0x25}, r[36];
  uint32_t done = 0;
  uint8_t st = 0;
  /* périphérique honnête : rien ne change */
  TT_ASSERT_EQ(fl_bot_exec(&u, inq, sizeof inq, FL_BOT_IN, r, sizeof r, &done, &st), FL_OK);
  TT_ASSERT_EQ(done, 36);
  TT_ASSERT(!u.ignore_residue);
  /* un READ CAPACITY(10) suffit aussi à le détecter */
  fake_msc_bogus_residue(m, 8);
  TT_ASSERT_EQ(fl_bot_exec(&u, rc10, sizeof rc10, FL_BOT_IN, r, 8, &done, &st), FL_OK);
  TT_ASSERT_EQ(st, 0);
  TT_ASSERT_EQ(done, 8);
  TT_ASSERT(u.ignore_residue);
  fake_msc_free(m);

  /* INQUIRY d'une autre longueur : pas de détection, le résidu est respecté */
  m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  memset(&u, 0, sizeof u);
  u.t = *fake_msc_transport(m);
  u.clk = g_clk;
  fake_msc_bogus_residue(m, 4);
  inq[4] = 32;
  TT_ASSERT_EQ(fl_bot_exec(&u, inq, sizeof inq, FL_BOT_IN, r, 32, &done, &st), FL_OK);
  TT_ASSERT_EQ(done, 28);
  TT_ASSERT(!u.ignore_residue);
  fake_msc_free(m);
}

/* Délais : CBW 5 s ; données et CSW 30 s pour READ(10)/(16), 5 s pour les autres commandes. */
static void test_usb_delais_par_phase(void) {
  fake_msc *m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  uint8_t a[512];
  TT_ASSERT_EQ(fl_bdev_read(d, 0, 1, a), FL_OK);
  fake_msc_stats *s = fake_msc_stats_get(m);
  TT_ASSERT_EQ(s->tmo_cbw[0x28], FL_USB_XFER_TIMEOUT_MS);
  TT_ASSERT_EQ(s->tmo_data[0x28], FL_USB_MEDIA_TIMEOUT_MS);
  TT_ASSERT_EQ(s->tmo_csw[0x28], FL_USB_MEDIA_TIMEOUT_MS);
  TT_ASSERT_EQ(s->tmo_cbw[0x12], FL_USB_XFER_TIMEOUT_MS);
  TT_ASSERT_EQ(s->tmo_data[0x12], FL_USB_XFER_TIMEOUT_MS);
  TT_ASSERT_EQ(s->tmo_csw[0x12], FL_USB_XFER_TIMEOUT_MS);
  TT_ASSERT_EQ(s->tmo_csw[0x00], FL_USB_XFER_TIMEOUT_MS);
  TT_ASSERT_EQ(s->tmo_data[0x25], FL_USB_XFER_TIMEOUT_MS);
  TT_ASSERT_EQ(FL_USB_XFER_TIMEOUT_MS, 5000u);
  TT_ASSERT_EQ(FL_USB_MEDIA_TIMEOUT_MS, 30000u);
  fl_bdev_close(d);

  /* READ(16), envoyé directement */
  fl_usb_dev u;
  memset(&u, 0, sizeof u);
  u.t = *fake_msc_transport(m);
  u.clk = g_clk;
  uint8_t c16[16];
  TT_ASSERT_EQ(fl_scsi_build_read(0x100000000ull, 1, c16), 16);
  memset(c16 + 2, 0, 8); /* LBA 0 en READ(16) */
  uint32_t done = 0;
  uint8_t st = 0;
  TT_ASSERT_EQ(fl_bot_exec(&u, c16, 16, FL_BOT_IN, a, sizeof a, &done, &st), FL_OK);
  TT_ASSERT_EQ(st, 0);
  TT_ASSERT_EQ(done, 512);
  TT_ASSERT_EQ(s->tmo_cbw[0x88], FL_USB_XFER_TIMEOUT_MS);
  TT_ASSERT_EQ(s->tmo_data[0x88], FL_USB_MEDIA_TIMEOUT_MS);
  TT_ASSERT_EQ(s->tmo_csw[0x88], FL_USB_MEDIA_TIMEOUT_MS);
  TT_ASSERT_EQ(ecritures_vues(s), 0);
  fake_msc_free(m);
}

static void test_usb_blocage(void) {
  const char *img = "gpt-luks2.img";
  fake_msc *m = faux(img);
  TT_ASSERT(m != NULL);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  static uint8_t a[64 * KIB], b[64 * KIB];
  fake_msc_reset_stats(m);
  fake_msc_stall_next_data(m);
  TT_ASSERT_EQ(fl_bdev_read(d, 2048, 128, a), FL_OK);
  TT_ASSERT_EQ(lire_ref(img, 2048ull * 512, sizeof b, b), 0);
  TT_ASSERT_MEM_EQ(a, b, sizeof a);
  fake_msc_stats *s = fake_msc_stats_get(m);
  TT_ASSERT_EQ(s->clear_halts, 1);
  TT_ASSERT_EQ(s->resets, 0);
  TT_ASSERT_EQ(s->op[0x28], 2); /* résidu complet → la commande est relancée */
  fl_bdev_close(d);
  fake_msc_free(m);
}

static void test_usb_csw_invalide(void) {
  const char *img = "gpt-luks2.img";
  fake_msc *m = faux(img);
  TT_ASSERT(m != NULL);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  static uint8_t a[4096], b[4096];
  TT_ASSERT_EQ(lire_ref(img, 0, sizeof b, b), 0);
  fake_msc_stats *s = fake_msc_stats_get(m);

  fake_msc_reset_stats(m);
  fake_msc_bad_csw_next(m);
  TT_ASSERT_EQ(fl_bdev_read(d, 0, 8, a), FL_OK);
  TT_ASSERT_MEM_EQ(a, b, sizeof a);
  TT_ASSERT_EQ(s->resets, 1);
  TT_ASSERT_EQ(s->op[0x28], 2);

  fake_msc_reset_stats(m);
  fake_msc_bad_csw_next(m);
  fake_msc_bad_csw_next(m);
  TT_ASSERT_EQ(fl_bdev_read(d, 0, 8, a), FL_EIO);
  TT_ASSERT_EQ(s->op[0x28], 2); /* un seul réessai */

  /* Le périphérique reste utilisable. */
  memset(a, 0, sizeof a);
  TT_ASSERT_EQ(fl_bdev_read(d, 0, 8, a), FL_OK);
  TT_ASSERT_MEM_EQ(a, b, sizeof a);
  fl_bdev_close(d);
  fake_msc_free(m);
}

static void test_usb_disparition(void) {
  fake_msc *m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  uint8_t a[512];
  fake_msc_fail_after(m, 3, FL_ENODEV);
  for (int i = 0; i < 3; i++) TT_ASSERT_EQ(fl_bdev_read(d, (uint64_t)i, 1, a), FL_OK);
  TT_ASSERT_EQ(fl_bdev_read(d, 3, 1, a), FL_ENODEV);
  fake_msc_stats *s = fake_msc_stats_get(m);
  unsigned calls = s->calls;
  TT_ASSERT_EQ(fl_bdev_read(d, 0, 1, a), FL_ENODEV);
  TT_ASSERT_EQ(fl_bdev_read(d, 5, 1, a), FL_ENODEV);
  TT_ASSERT_EQ(s->calls, calls); /* plus rien n'est envoyé */
  TT_ASSERT_EQ(s->resets, 0);    /* FL_ENODEV n'est jamais réessayé */
  TT_ASSERT_STR_EQ(fl_strerror(FL_ENODEV), "disque débranché");
  fl_bdev_close(d);
  fake_msc_free(m);
}

static void test_usb_delai(void) {
  fake_msc *m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  uint8_t a[512];
  fake_msc_fail_after(m, 0, FL_ETIMEDOUT);
  TT_ASSERT_EQ(fl_bdev_read(d, 0, 1, a), FL_ETIMEDOUT);
  fake_msc_stats *s = fake_msc_stats_get(m);
  unsigned calls = s->calls;
  TT_ASSERT_EQ(fl_bdev_read(d, 0, 1, a), FL_ETIMEDOUT);
  TT_ASSERT(s->calls > calls); /* pas « mort » : on réessaie à la lecture suivante */
  fl_bdev_close(d);
  fake_msc_free(m);
}

static void test_usb_sens(void) {
  const char *img = "gpt-luks2.img";
  fake_msc *m = faux(img);
  TT_ASSERT(m != NULL);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  uint8_t a[512], b[512];
  fake_msc_sense_next(m, 0x03, 0x11, 0x00); /* MEDIUM ERROR */
  TT_ASSERT_EQ(fl_bdev_read(d, 1, 1, a), FL_EIO);
  fake_msc_sense_next(m, 0x07, 0x27, 0x00); /* DATA PROTECT */
  TT_ASSERT_EQ(fl_bdev_read(d, 1, 1, a), FL_EROFS);
  fake_msc_sense_next(m, 0x06, 0x28, 0x00); /* UNIT ATTENTION : réessai immédiat */
  TT_ASSERT_EQ(fl_bdev_read(d, 1, 1, a), FL_OK);
  g_h.sleeps = 0;
  fake_msc_not_ready(m, 2); /* NOT READY : réessai après attente */
  TT_ASSERT_EQ(fl_bdev_read(d, 1, 1, a), FL_OK);
  TT_ASSERT_EQ(g_h.sleeps, 2);
  TT_ASSERT_EQ(lire_ref(img, 512, 512, b), 0);
  TT_ASSERT_MEM_EQ(a, b, 512);
  fl_bdev_close(d);
  fake_msc_free(m);
}

static void test_usb_4k(void) {
  const char *img = "whole-luks2-4k.img";
  TT_ASSERT_EQ(taille(img) % 4096, 0);
  fake_msc *m = faux(img);
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(fake_msc_set_block_size(m, 4096), FL_OK);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  TT_ASSERT_EQ(d->sector_size, 4096);
  TT_ASSERT_EQ(d->sector_count, taille(img) / 4096);
  char buf[64] = {0};
  int64_t n = 0;
  TT_ASSERT_EQ(pile_hello(d, buf, sizeof buf, &n), FL_OK);
  TT_ASSERT_EQ(n, 16);
  TT_ASSERT_MEM_EQ(buf, "Bonjour FluLuks\n", 16);
  fake_msc_stats *s = fake_msc_stats_get(m);
  TT_ASSERT(s->max_read_xfer <= 64 * KIB);
  TT_ASSERT_EQ(s->writes, 0);
  fl_bdev_close(d);
  fake_msc_free(m);
}

static void test_usb_lecture_seule(void) {
  fake_msc *m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  fl_bdev *d = NULL;
  uint8_t z[512] = {0};
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  TT_ASSERT_EQ(fl_bdev_write(d, 0, 1, z), FL_EROFS);
  TT_ASSERT_EQ(fl_bdev_flush(d), FL_OK);
  fl_bdev_close(d);
  fake_msc_stats *s = fake_msc_stats_get(m);
  TT_ASSERT_EQ(s->writes, 0);
  TT_ASSERT_EQ(s->op[0x35], 0);

  /* Même ouvert sans read_only, la phase 2 n'écrit jamais ; flush envoie SYNCHRONIZE CACHE. */
  TT_ASSERT_EQ(ouvrir_usb(m, false, &d), FL_OK);
  TT_ASSERT(!d->read_only);
  TT_ASSERT_EQ(fl_bdev_write(d, 0, 1, z), FL_EROFS);
  TT_ASSERT_EQ(fl_bdev_flush(d), FL_OK);
  TT_ASSERT_EQ(s->writes, 0);
  TT_ASSERT_EQ(s->op[0x2A] + s->op[0x8A], 0);
  TT_ASSERT_EQ(s->op[0x35], 1);
  fl_bdev_close(d);
  fake_msc_free(m);
}

static void test_usb_pile_complete(void) {
  fake_msc *m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  char buf[64] = {0};
  int64_t n = 0;
  TT_ASSERT_EQ(pile_hello(d, buf, sizeof buf, &n), FL_OK);
  TT_ASSERT_EQ(n, 16);
  TT_ASSERT_MEM_EQ(buf, "Bonjour FluLuks\n", 16);
  TT_ASSERT_EQ(fake_msc_stats_get(m)->writes, 0);
  TT_ASSERT_EQ(ecritures_vues(fake_msc_stats_get(m)), 0);
  TT_ASSERT_EQ(hors_lecture(fake_msc_stats_get(m), false), 0);
  fl_bdev_close(d);
  fake_msc_free(m);
}

static void test_usb_capacite16(void) {
  fake_msc *m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  fake_msc_force_rc16(m);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  TT_ASSERT_EQ(fake_msc_stats_get(m)->op[0x9E], 1);
  TT_ASSERT_EQ(d->sector_count, taille("gpt-luks2.img") / 512);
  fl_bdev_close(d);
  fake_msc_free(m);
}

static void test_usb_hostile(void) {
  fake_msc *m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  fl_bdev *d = (fl_bdev *)1;
  TT_ASSERT_EQ(fake_msc_set_block_size(m, 520), FL_OK);
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_ENOTSUP);
  TT_ASSERT(d == NULL);
  TT_ASSERT_EQ(fake_msc_set_block_size(m, 256), FL_OK);
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_ENOTSUP);
  TT_ASSERT_EQ(fake_msc_set_block_size(m, 512), FL_OK);

  fake_msc_set_capacity(m, 0); /* dernier LBA 2^64-1 : capacité nulle */
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_EIO);
  fake_msc_set_capacity(m, UINT64_MAX); /* 2^64-1 blocs de 512 : débordement */
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_EIO);
  fake_msc_free(m);

  m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  fake_msc_set_inquiry_len(m, 3);
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_EIO);
  fake_msc_set_inquiry_len(m, 36);
  fake_msc_set_peripheral(m, 0x7F); /* unité logique absente */
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_ENOTSUP);
  fake_msc_set_peripheral(m, 0x01); /* bande */
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_ENOTSUP);
  fake_msc_set_peripheral(m, 0x00);
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  fl_bdev_close(d);
  fake_msc_free(m);
}

static void test_usb_cdb_lecture(void) {
  uint8_t c[16];
  TT_ASSERT_EQ(fl_scsi_build_read(0xFFFFFFFFull, 1, c), 10);
  TT_ASSERT_EQ(c[0], 0x28);
  TT_ASSERT_EQ(c[2] & c[3] & c[4] & c[5], 0xFF);
  TT_ASSERT_EQ(c[7], 0);
  TT_ASSERT_EQ(c[8], 1);
  TT_ASSERT_EQ(fl_scsi_build_read(0xFFFFFFFFull, 2, c), 16);
  TT_ASSERT_EQ(fl_scsi_build_read(0x100000000ull, 1, c), 16);
  TT_ASSERT_EQ(c[0], 0x88);
  static const uint8_t lba[8] = {0, 0, 0, 1, 0, 0, 0, 0};
  TT_ASSERT_MEM_EQ(c + 2, lba, 8);
  static const uint8_t cnt[4] = {0, 0, 0, 1};
  TT_ASSERT_MEM_EQ(c + 10, cnt, 4);
  TT_ASSERT_EQ(fl_scsi_build_read(5, 0x10000, c), 16);
}

typedef struct {
  fl_bdev *d;
  uint64_t lba;
  int ko;
} fil_arg;

static void *fil(void *p) {
  fil_arg *a = p;
  uint8_t *x = malloc(96 * KIB), *y = malloc(96 * KIB);
  if (!x || !y) {
    a->ko = 1;
  } else {
    for (int i = 0; i < 20 && !a->ko; i++) {
      uint64_t lba = a->lba + (uint64_t)i * 37;
      if (fl_bdev_read(a->d, lba, 192, x) != FL_OK || lire_ref("gpt-luks2.img", lba * 512, 96 * KIB, y) != 0 ||
          memcmp(x, y, 96 * KIB) != 0)
        a->ko = 1;
    }
  }
  free(x);
  free(y);
  return NULL;
}

static void test_usb_concurrence(void) {
  fake_msc *m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  fl_bdev *d = NULL;
  TT_ASSERT_EQ(ouvrir_usb(m, true, &d), FL_OK);
  pthread_t th[4];
  fil_arg args[4];
  for (int i = 0; i < 4; i++) {
    args[i] = (fil_arg){d, (uint64_t)i * 20000, 0};
    TT_ASSERT_EQ(pthread_create(&th[i], NULL, fil, &args[i]), 0);
  }
  for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
  for (int i = 0; i < 4; i++) TT_ASSERT_EQ(args[i].ko, 0);
  TT_ASSERT_EQ(fake_msc_stats_get(m)->violations, 0);
  fl_bdev_close(d);
  fake_msc_free(m);
}

static int nul_out(void *c, const void *b, size_t l, unsigned t) {
  (void)c, (void)b, (void)t;
  return (int)l;
}

static void test_usb_arguments(void) {
  fl_bdev *d = (fl_bdev *)1;
  TT_ASSERT_EQ(fl_usb_bdev_open(NULL, 0, true, &d), FL_EINVAL);
  fake_msc *m = faux("gpt-luks2.img");
  TT_ASSERT(m != NULL);
  TT_ASSERT_EQ(fl_usb_bdev_open(fake_msc_transport(m), 16, true, &d), FL_EINVAL);
  TT_ASSERT_EQ(fl_usb_bdev_open(fake_msc_transport(m), 0, true, NULL), FL_EINVAL);
  fl_usb_transport t = {.ctx = NULL, .bulk_out = nul_out};
  TT_ASSERT_EQ(fl_usb_bdev_open(&t, 0, true, &d), FL_EINVAL);
  fake_msc_free(m);

  fl_usb_transport *u = (fl_usb_transport *)1;
  TT_ASSERT_EQ(fl_usbdevfs_transport_new(-1, 0, 0x81, 0x02, &u), FL_EINVAL);
  TT_ASSERT(u == NULL);
  int fd = open("/dev/null", O_RDWR);
  TT_ASSERT(fd >= 0);
  TT_ASSERT_EQ(fl_usbdevfs_transport_new(fd, 0, 0x01, 0x02, &u), FL_EINVAL);
  TT_ASSERT_EQ(fl_usbdevfs_transport_new(fd, 0, 0x81, 0x82, &u), FL_EINVAL);
  TT_ASSERT_EQ(fl_usbdevfs_transport_new(fd, 0, 0x81, 0x02, &u), FL_OK);
  uint8_t b[64];
  TT_ASSERT_EQ(u->bulk_in(u->ctx, b, sizeof b, 100), FL_EIO); /* ENOTTY → FL_EIO */
  TT_ASSERT_EQ(u->bulk_out(u->ctx, b, sizeof b, 100), FL_EIO);
  TT_ASSERT_EQ(u->clear_halt(u->ctx, true), FL_EIO);
  TT_ASSERT_EQ(u->bot_reset(u->ctx), FL_EIO);
  fl_usbdevfs_transport_free(u);
  fl_usbdevfs_transport_free(NULL);
  TT_ASSERT(fcntl(fd, F_GETFD) >= 0); /* fd n'appartient pas au transport */
  close(fd);
}

static void test_usb_messages(void) {
  TT_ASSERT_STR_EQ(fl_strerror(FL_ENODEV), "disque débranché");
  TT_ASSERT_STR_EQ(fl_strerror(FL_ETIMEDOUT), "le disque ne répond pas");
  TT_ASSERT_STR_EQ(fl_strerror(FL_ESTALL), "transfert USB bloqué");
  TT_ASSERT_STR_EQ(fl_strerror(FL_EBUSY), "volume occupé");
}

int main(void) {
  TT_RUN(test_usb_messages);
  TT_RUN(test_usb_arguments);
  TT_RUN(test_usb_cdb_lecture);
  TT_RUN(test_usb_capacite);
  TT_RUN(test_usb_capacite16);
  TT_RUN(test_usb_lecture_identique);
  TT_RUN(test_usb_disque_lent);
  TT_RUN(test_usb_disque_jamais_pret);
  TT_RUN(test_usb_demarrage);
  TT_RUN(test_usb_intervention_manuelle);
  TT_RUN(test_usb_csw_paquet_vide);
  TT_RUN(test_usb_residu_fantaisiste);
  TT_RUN(test_usb_residu_heuristique);
  TT_RUN(test_usb_delais_par_phase);
  TT_RUN(test_usb_blocage);
  TT_RUN(test_usb_csw_invalide);
  TT_RUN(test_usb_disparition);
  TT_RUN(test_usb_delai);
  TT_RUN(test_usb_sens);
  TT_RUN(test_usb_hostile);
  TT_RUN(test_usb_lecture_seule);
  TT_RUN(test_usb_concurrence);
  TT_RUN(test_usb_4k);
  TT_RUN(test_usb_pile_complete);
  TT_MAIN_END();
}
