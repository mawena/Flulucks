#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fluluks/blockdev.h"
#include "fluluks/cache.h"
#include "fluluks/error.h"
#include "tt.h"

#define BLK 65536u
#define SPB (BLK / 512u)

/* ---- bdev « compteur » : enveloppe un parent, compte les appels, injecte des pannes, bloque sur demande ---- */
typedef struct {
  fl_bdev *inner;
  pthread_mutex_t m;
  pthread_cond_t cv;
  unsigned long calls;
  unsigned long long bytes;
  int fail_next;
  int gate_on;      /* si vrai, les lectures attendent l'ouverture de la porte */
  int gate_waiting; /* nombre de lectures bloquées */
} cnt_priv;

static int cnt_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  cnt_priv *p = d->priv;
  pthread_mutex_lock(&p->m);
  p->calls++;
  p->bytes += (unsigned long long)count * d->sector_size;
  if (p->fail_next > 0) {
    p->fail_next--;
    pthread_mutex_unlock(&p->m);
    return FL_EIO;
  }
  if (p->gate_on) {
    p->gate_waiting++;
    pthread_cond_broadcast(&p->cv);
    while (p->gate_on) pthread_cond_wait(&p->cv, &p->m);
    p->gate_waiting--;
  }
  pthread_mutex_unlock(&p->m);
  return fl_bdev_read(p->inner, lba, count, buf);
}
static int cnt_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf) {
  cnt_priv *p = d->priv;
  return fl_bdev_write(p->inner, lba, count, buf);
}
static int cnt_flush(fl_bdev *d) { return fl_bdev_flush(((cnt_priv *)d->priv)->inner); }
static void cnt_destroy(fl_bdev *d) {
  cnt_priv *p = d->priv;
  pthread_mutex_destroy(&p->m);
  pthread_cond_destroy(&p->cv);
  free(p);
  free(d);
}
static const fl_bdev_ops cnt_ops = {cnt_read, cnt_write, cnt_flush, cnt_destroy};

/* Ne possède pas inner. */
static fl_bdev *cnt_new(fl_bdev *inner) {
  fl_bdev *d = calloc(1, sizeof *d);
  cnt_priv *p = calloc(1, sizeof *p);
  if (!d || !p) abort();
  pthread_mutex_init(&p->m, NULL);
  pthread_cond_init(&p->cv, NULL);
  p->inner = inner;
  d->ops = &cnt_ops;
  d->sector_size = inner->sector_size;
  d->sector_count = inner->sector_count;
  d->read_only = inner->read_only;
  d->priv = p;
  return d;
}
static cnt_priv *CP(fl_bdev *d) { return d->priv; }

static uint64_t rng_next(uint64_t *s) {
  uint64_t x = *s;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  return *s = x;
}

/* ---- lecture seule sur la fixture ---- */
static void test_cache_identique(void) {
  fl_bdev *file = NULL, *cnt, *c = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(tt_fixture("ext4-plain.img"), 512, true, &file), FL_OK);
  cnt = cnt_new(file);
  TT_ASSERT_EQ(fl_cache_bdev_new(cnt, BLK, 256, &c), FL_OK);
  TT_ASSERT_EQ(c->sector_size, 512);
  TT_ASSERT_EQ(c->sector_count, file->sector_count);
  uint8_t *a = malloc(300 * 512), *b = malloc(300 * 512);
  uint64_t s = 0x1234567ULL;
  int bad = 0;
  for (int i = 0; i < 2000 && !bad; i++) {
    uint32_t n = 1 + (uint32_t)(rng_next(&s) % 300);
    uint64_t lba = rng_next(&s) % (file->sector_count - n + 1);
    if (fl_bdev_read(c, lba, n, a) != FL_OK || fl_bdev_read(file, lba, n, b) != FL_OK ||
        memcmp(a, b, (size_t)n * 512) != 0)
      bad = 1;
  }
  free(a);
  free(b);
  fl_bdev_close(c);
  fl_bdev_close(cnt);
  fl_bdev_close(file);
  TT_ASSERT(!bad);
}

static void test_cache_sequentiel(void) {
  fl_bdev *file = NULL, *cnt, *c = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(tt_fixture("ext4-plain.img"), 512, true, &file), FL_OK);
  cnt = cnt_new(file);
  TT_ASSERT_EQ(fl_cache_bdev_new(cnt, BLK, 256, &c), FL_OK);
  uint8_t *a = malloc(BLK), *b = malloc(BLK);
  int bad = 0;
  for (uint64_t lba = 0; lba < (8u << 20) / 512 && !bad; lba += SPB) {
    if (fl_bdev_read(c, lba, SPB, a) != FL_OK || fl_bdev_read(file, lba, SPB, b) != FL_OK ||
        memcmp(a, b, BLK) != 0)
      bad = 1;
  }
  unsigned long calls = CP(cnt)->calls;
  free(a);
  free(b);
  fl_bdev_close(c);
  fl_bdev_close(cnt);
  fl_bdev_close(file);
  TT_ASSERT(!bad);
  TT_ASSERT(calls <= (8u << 20) / (4 * BLK) + 2);
}

static void test_cache_reutilise(void) {
  fl_bdev *file = NULL, *cnt, *c = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(tt_fixture("ext4-plain.img"), 512, true, &file), FL_OK);
  cnt = cnt_new(file);
  TT_ASSERT_EQ(fl_cache_bdev_new(cnt, BLK, 256, &c), FL_OK);
  uint8_t *a = malloc(BLK), *b = malloc(BLK);
  TT_ASSERT_EQ(fl_bdev_read(c, 40 * SPB, SPB, a), FL_OK);
  unsigned long before = CP(cnt)->calls;
  TT_ASSERT_EQ(fl_bdev_read(c, 40 * SPB, SPB, b), FL_OK);
  unsigned long after = CP(cnt)->calls;
  int same = memcmp(a, b, BLK) == 0;
  free(a);
  free(b);
  fl_bdev_close(c);
  fl_bdev_close(cnt);
  fl_bdev_close(file);
  TT_ASSERT_EQ(after, before);
  TT_ASSERT(same);
}

static void test_cache_erreur_non_memorisee(void) {
  fl_bdev *file = NULL, *cnt, *c = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(tt_fixture("ext4-plain.img"), 512, true, &file), FL_OK);
  cnt = cnt_new(file);
  TT_ASSERT_EQ(fl_cache_bdev_new(cnt, BLK, 256, &c), FL_OK);
  uint8_t *a = malloc(BLK), *b = malloc(BLK);
  CP(cnt)->fail_next = 1;
  int r1 = fl_bdev_read(c, 10 * SPB, SPB, a);
  int r2 = fl_bdev_read(c, 10 * SPB, SPB, a);
  int r3 = fl_bdev_read(file, 10 * SPB, SPB, b);
  int same = memcmp(a, b, BLK) == 0;
  free(a);
  free(b);
  fl_bdev_close(c);
  fl_bdev_close(cnt);
  fl_bdev_close(file);
  TT_ASSERT_EQ(r1, FL_EIO);
  TT_ASSERT_EQ(r2, FL_OK);
  TT_ASSERT_EQ(r3, FL_OK);
  TT_ASSERT(same);
}

typedef struct {
  fl_bdev *cache;
  const char *path; /* tt_fixture() utilise un tampon statique : non réentrant */
  uint64_t seed;
  int bad;
} conc_arg;

static void *conc_worker(void *p) {
  conc_arg *w = p;
  fl_bdev *ref = NULL;
  if (fl_file_bdev_open(w->path, 512, true, &ref) != FL_OK) {
    w->bad = 1;
    return NULL;
  }
  uint8_t *a = malloc(300 * 512), *b = malloc(300 * 512);
  uint64_t s = w->seed;
  for (int i = 0; i < 500 && !w->bad; i++) {
    uint32_t n = 1 + (uint32_t)(rng_next(&s) % 300);
    /* localité forte pour provoquer des attentes sur des blocs en chargement */
    uint64_t span = (i % 2) ? 4096 : ref->sector_count - n + 1;
    uint64_t lba = rng_next(&s) % span;
    if (lba + n > ref->sector_count) lba = ref->sector_count - n;
    if (fl_bdev_read(w->cache, lba, n, a) != FL_OK || fl_bdev_read(ref, lba, n, b) != FL_OK ||
        memcmp(a, b, (size_t)n * 512) != 0)
      w->bad = 1;
  }
  free(a);
  free(b);
  fl_bdev_close(ref);
  return NULL;
}

static void test_cache_concurrence(void) {
  fl_bdev *file = NULL, *cnt, *c = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(tt_fixture("ext4-plain.img"), 512, true, &file), FL_OK);
  cnt = cnt_new(file);
  /* petit cache pour forcer des évictions */
  TT_ASSERT_EQ(fl_cache_bdev_new(cnt, BLK, 32, &c), FL_OK);
  pthread_t th[8];
  conc_arg arg[8];
  char path[4096];
  snprintf(path, sizeof path, "%s", tt_fixture("ext4-plain.img"));
  alarm(30); /* interblocage -> le processus meurt */
  for (int i = 0; i < 8; i++) {
    arg[i].cache = c;
    arg[i].path = path;
    arg[i].seed = 0x9E3779B97F4A7C15ULL * (uint64_t)(i + 1);
    arg[i].bad = 0;
    pthread_create(&th[i], NULL, conc_worker, &arg[i]);
  }
  int bad = 0;
  for (int i = 0; i < 8; i++) {
    pthread_join(th[i], NULL);
    bad |= arg[i].bad;
  }
  alarm(0);
  fl_bdev_close(c);
  fl_bdev_close(cnt);
  fl_bdev_close(file);
  TT_ASSERT(!bad);
}

static void test_cache_fin_disque(void) {
  fl_bdev *file = NULL, *sl = NULL, *cnt, *c = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(tt_fixture("ext4-plain.img"), 512, true, &file), FL_OK);
  /* 1000 secteurs = 7 blocs de 128 secteurs + un dernier bloc partiel de 104 */
  TT_ASSERT_EQ(fl_slice_bdev_new(file, 3, 1000, &sl), FL_OK);
  cnt = cnt_new(sl);
  TT_ASSERT_EQ(fl_cache_bdev_new(cnt, BLK, 8, &c), FL_OK);
  uint8_t a[512], b[512];
  int r1 = fl_bdev_read(c, 999, 1, a);
  int r2 = fl_bdev_read(sl, 999, 1, b);
  int same = memcmp(a, b, 512) == 0;
  /* lecture séquentielle de tout le disque : la lecture anticipée est coupée à la fin */
  fl_bdev_close(c);
  c = NULL;
  int r3 = fl_cache_bdev_new(cnt, BLK, 8, &c);
  uint8_t *x = malloc(1000 * 512), *y = malloc(1000 * 512);
  int r4 = FL_OK;
  for (uint64_t lba = 0; lba < 1000 && r4 == FL_OK; lba += 50) r4 = fl_bdev_read(c, lba, 50, x + lba * 512);
  int r5 = fl_bdev_read(sl, 0, 1000, y);
  int same2 = memcmp(x, y, 1000 * 512) == 0;
  /* dépassement refusé */
  int r6 = fl_bdev_read(c, 999, 2, x);
  free(x);
  free(y);
  fl_bdev_close(c);
  fl_bdev_close(cnt);
  fl_bdev_close(sl);
  fl_bdev_close(file);
  TT_ASSERT_EQ(r1, FL_OK);
  TT_ASSERT_EQ(r2, FL_OK);
  TT_ASSERT(same);
  TT_ASSERT_EQ(r3, FL_OK);
  TT_ASSERT_EQ(r4, FL_OK);
  TT_ASSERT_EQ(r5, FL_OK);
  TT_ASSERT(same2);
  TT_ASSERT_EQ(r6, FL_EINVAL);
}

static void test_cache_parametres(void) {
  fl_bdev *file = NULL, *c = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(tt_fixture("ext4-plain.img"), 512, true, &file), FL_OK);
  TT_ASSERT_EQ(fl_cache_bdev_new(NULL, BLK, 256, &c), FL_EINVAL);
  TT_ASSERT_EQ(fl_cache_bdev_new(file, BLK, 256, NULL), FL_EINVAL);
  TT_ASSERT_EQ(fl_cache_bdev_new(file, 1000, 256, &c), FL_EINVAL);        /* pas une puissance de 2 */
  TT_ASSERT_EQ(fl_cache_bdev_new(file, 256, 256, &c), FL_EINVAL);         /* pas multiple du secteur */
  TT_ASSERT_EQ(fl_cache_bdev_new(file, 2u << 20, 256, &c), FL_EINVAL);    /* > 1 Mio */
  TT_ASSERT_EQ(fl_cache_bdev_new(file, BLK, 4, &c), FL_EINVAL);           /* nblocks < readahead + 1 */
  TT_ASSERT(c == NULL);
  TT_ASSERT_EQ(fl_cache_bdev_new(file, BLK, 5, &c), FL_OK);
  fl_bdev_close(c);
  fl_bdev_close(file);
}

/* ---- écriture : parent inscriptible (fichier temporaire) ---- */
#define W_SECTORS 1000u /* 7 blocs de 128 secteurs + bloc partiel */
static char g_path[] = "fl_test_cache_XXXXXX";

static int make_tmp(void) {
  uint8_t *pat = malloc(W_SECTORS * 512);
  for (size_t i = 0; i < W_SECTORS * 512; i++) pat[i] = (uint8_t)((i * 7 + (i >> 9)) & 0xFF);
  int fd = mkstemp(g_path);
  if (fd < 0) return -1;
  ssize_t w = write(fd, pat, W_SECTORS * 512);
  close(fd);
  free(pat);
  return w == (ssize_t)(W_SECTORS * 512) ? 0 : -1;
}

static void test_cache_ecriture(void) {
  fl_bdev *file = NULL, *cnt, *c = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(g_path, 512, false, &file), FL_OK);
  cnt = cnt_new(file);
  TT_ASSERT_EQ(fl_cache_bdev_new(cnt, BLK, 8, &c), FL_OK);
  TT_ASSERT(!c->read_only);
  uint8_t *a = malloc(2 * BLK), *w = malloc(40 * 512), *ref = malloc(2 * BLK);
  /* met deux blocs en cache puis écrit à cheval sur leur frontière */
  TT_ASSERT_EQ(fl_bdev_read(c, 0, 2 * SPB, a), FL_OK);
  memset(w, 0xA5, 40 * 512);
  TT_ASSERT_EQ(fl_bdev_write(c, SPB - 10, 40, w), FL_OK);
  unsigned long before = CP(cnt)->calls;
  TT_ASSERT_EQ(fl_bdev_read(c, 0, 2 * SPB, a), FL_OK);
  TT_ASSERT_EQ(CP(cnt)->calls, before); /* blocs mis à jour, pas relus */
  TT_ASSERT_EQ(fl_bdev_read(file, 0, 2 * SPB, ref), FL_OK);
  TT_ASSERT_MEM_EQ(a, ref, 2 * BLK);
  TT_ASSERT_MEM_EQ(a + (SPB - 10) * 512, w, 40 * 512);
  /* écriture dans le dernier bloc partiel */
  TT_ASSERT_EQ(fl_bdev_read(c, W_SECTORS - 1, 1, a), FL_OK);
  memset(w, 0x5A, 512);
  TT_ASSERT_EQ(fl_bdev_write(c, W_SECTORS - 1, 1, w), FL_OK);
  TT_ASSERT_EQ(fl_bdev_read(c, W_SECTORS - 1, 1, a), FL_OK);
  TT_ASSERT_MEM_EQ(a, w, 512);
  TT_ASSERT_EQ(fl_bdev_write(c, W_SECTORS, 1, w), FL_EINVAL);
  TT_ASSERT_EQ(fl_bdev_flush(c), FL_OK);
  free(a);
  free(w);
  free(ref);
  fl_bdev_close(c);
  fl_bdev_close(cnt);
  fl_bdev_close(file);
}

typedef struct {
  fl_bdev *cache;
  uint8_t *buf;
  int rc;
} rd_arg;
static void *rd_thread(void *p) {
  rd_arg *r = p;
  r->rc = fl_bdev_read(r->cache, 0, 8, r->buf);
  return NULL;
}

static void test_cache_ecriture_pendant_chargement(void) {
  fl_bdev *file = NULL, *cnt, *c = NULL;
  TT_ASSERT_EQ(fl_file_bdev_open(g_path, 512, false, &file), FL_OK);
  cnt = cnt_new(file);
  TT_ASSERT_EQ(fl_cache_bdev_new(cnt, BLK, 8, &c), FL_OK);
  uint8_t *old = malloc(8 * 512), *w = malloc(8 * 512), *a = malloc(8 * 512);
  cnt_priv *p = CP(cnt);
  pthread_mutex_lock(&p->m);
  p->gate_on = 1;
  pthread_mutex_unlock(&p->m);
  rd_arg ra = {c, old, 0};
  pthread_t th;
  alarm(30);
  pthread_create(&th, NULL, rd_thread, &ra);
  pthread_mutex_lock(&p->m);
  while (p->gate_waiting < 1) pthread_cond_wait(&p->cv, &p->m);
  pthread_mutex_unlock(&p->m);
  /* le bloc 0 est en cours de chargement : on écrit puis on libère le parent */
  memset(w, 0xC3, 8 * 512);
  int rw = fl_bdev_write(c, 0, 8, w);
  pthread_mutex_lock(&p->m);
  p->gate_on = 0;
  pthread_cond_broadcast(&p->cv);
  pthread_mutex_unlock(&p->m);
  pthread_join(th, NULL);
  alarm(0);
  int r2 = fl_bdev_read(c, 0, 8, a);
  int same = memcmp(a, w, 8 * 512) == 0; /* pas de donnée périmée en cache */
  free(old);
  free(w);
  free(a);
  fl_bdev_close(c);
  fl_bdev_close(cnt);
  fl_bdev_close(file);
  TT_ASSERT_EQ(rw, FL_OK);
  TT_ASSERT_EQ(ra.rc, FL_OK);
  TT_ASSERT_EQ(r2, FL_OK);
  TT_ASSERT(same);
}

static void test_cache_lecture_seule_ecriture(void) {
  fl_bdev *file = NULL, *c = NULL;
  uint8_t s[512] = {0};
  TT_ASSERT_EQ(fl_file_bdev_open(tt_fixture("ext4-plain.img"), 512, true, &file), FL_OK);
  TT_ASSERT_EQ(fl_cache_bdev_new(file, BLK, 8, &c), FL_OK);
  TT_ASSERT(c->read_only);
  TT_ASSERT_EQ(fl_bdev_write(c, 0, 1, s), FL_EROFS);
  fl_bdev_close(c);
  fl_bdev_close(file);
}

int main(void) {
  TT_RUN(test_cache_identique);
  TT_RUN(test_cache_sequentiel);
  TT_RUN(test_cache_reutilise);
  TT_RUN(test_cache_erreur_non_memorisee);
  TT_RUN(test_cache_concurrence);
  TT_RUN(test_cache_fin_disque);
  TT_RUN(test_cache_parametres);
  TT_RUN(test_cache_lecture_seule_ecriture);
  if (make_tmp() == 0) {
    TT_RUN(test_cache_ecriture);
    TT_RUN(test_cache_ecriture_pendant_chargement);
    unlink(g_path);
  } else {
    fprintf(stderr, "impossible de creer le fichier temporaire\n");
    tt_failures++;
  }
  TT_MAIN_END();
}
