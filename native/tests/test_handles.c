#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fs/ro_reasons.h"
#include "fsck_util.h"
#include "handles.h"
#include "tt.h"

static size_t lire_pass(char *buf, size_t cap, const char *nom) {
  FILE *f = fopen(tt_fixture(nom), "rb");
  if (!f) return 0;
  size_t n = fread(buf, 1, cap, f);
  fclose(f);
  return n;
}

/* Exécute fn(h, j) et analyse le JSON produit (NULL si erreur). */
static cJSON *json_de(int rc, fl_json *j) {
  if (rc != FL_OK || j->err != FL_OK || !j->buf) return NULL;
  return cJSON_Parse(j->buf);
}

static int64_t monter_plain(int64_t *disk) {
  *disk = fj_open_image(tt_fixture("ext4-plain.img"), true);
  if (*disk <= 0) return *disk;
  return fj_mount(*disk, 0, NULL, 0, false);
}

static void test_scan_gpt_luks2(void) {
  int64_t d = fj_open_image(tt_fixture("gpt-luks2.img"), true);
  TT_ASSERT(d > 0);
  fl_json j;
  fl_json_init(&j);
  cJSON *r = json_de(fj_scan(d, &j), &j);
  fl_json_free(&j);
  TT_ASSERT(r != NULL);
  TT_ASSERT_EQ(cJSON_GetObjectItem(r, "sector_size")->valuedouble, 512);
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(r, "table")->valuestring, "gpt");
  cJSON *parts = cJSON_GetObjectItem(r, "parts");
  TT_ASSERT_EQ(cJSON_GetArraySize(parts), 1);
  cJSON *p = cJSON_GetArrayItem(parts, 0);
  TT_ASSERT_EQ(cJSON_GetObjectItem(p, "index")->valuedouble, 1);
  TT_ASSERT_EQ(cJSON_GetObjectItem(p, "first_lba")->valuedouble, 2048);
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(p, "content")->valuestring, "LUKS2");
  TT_ASSERT(cJSON_IsTrue(cJSON_GetObjectItem(p, "supported")));
  cJSON *l = cJSON_GetObjectItem(p, "luks");
  TT_ASSERT(l != NULL);
  TT_ASSERT_EQ(cJSON_GetObjectItem(l, "version")->valuedouble, 2);
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(l, "cipher")->valuestring, "aes-xts-plain64");
  TT_ASSERT(cJSON_IsFalse(cJSON_GetObjectItem(l, "primary_damaged")));
  TT_ASSERT_EQ(cJSON_GetObjectItem(l, "max_kdf_memory_kib")->valuedouble, 65536);
  cJSON_Delete(r);
  TT_ASSERT_EQ(fj_close_disk(d), FL_OK);
  TT_ASSERT_EQ(fj_debug_used(), 0);
}

/* Copie creuse des 16 premiers Mio (en-têtes LUKS2), magie du primaire détruite. */
static int copie_entete_detruit(const char *src, const char *dst) {
  FILE *a = fopen(src, "rb"), *b = fopen(dst, "wb");
  int ok = a && b;
  static unsigned char buf[1u << 20];
  for (int i = 0; ok && i < 16; i++) ok = fread(buf, 1, sizeof buf, a) == sizeof buf && fwrite(buf, 1, sizeof buf, b) == sizeof buf;
  if (ok) ok = fseek(a, 0, SEEK_END) == 0 && fseek(b, ftell(a) - 1, SEEK_SET) == 0 && fputc(0, b) == 0;
  if (ok) ok = fseek(b, 0, SEEK_SET) == 0 && fwrite("\0\0\0\0\0\0", 1, 6, b) == 6;
  if (a) fclose(a);
  if (b && fclose(b) != 0) ok = 0;
  return ok;
}

static void test_scan_primaire_abime(void) {
  char dst[4096];
  snprintf(dst, sizeof dst, "%s", tt_fixture("tmp-handles-magie.img"));
  TT_ASSERT(copie_entete_detruit(tt_fixture("whole-luks2-4k.img"), dst));
  int64_t d = fj_open_image(dst, true);
  unlink(dst); /* l'image reste ouverte */
  TT_ASSERT(d > 0);
  fl_json j;
  fl_json_init(&j);
  cJSON *r = json_de(fj_scan(d, &j), &j);
  fl_json_free(&j);
  TT_ASSERT(r != NULL);
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(r, "table")->valuestring, "none");
  cJSON *p = cJSON_GetArrayItem(cJSON_GetObjectItem(r, "parts"), 0);
  TT_ASSERT(p != NULL);
  TT_ASSERT_EQ(cJSON_GetObjectItem(p, "index")->valuedouble, 0);
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(p, "content")->valuestring, "LUKS2");
  cJSON *l = cJSON_GetObjectItem(p, "luks");
  TT_ASSERT(l != NULL);
  TT_ASSERT(cJSON_IsTrue(cJSON_GetObjectItem(l, "primary_damaged")));
  TT_ASSERT_EQ(cJSON_GetObjectItem(l, "max_kdf_memory_kib")->valuedouble, 32768);
  cJSON_Delete(r);
  TT_ASSERT_EQ(fj_close_disk(d), FL_OK);
}

static void test_scan_ext4_et_fat(void) {
  int64_t d = fj_open_image(tt_fixture("ext4-plain.img"), true);
  TT_ASSERT(d > 0);
  fl_json j;
  fl_json_init(&j);
  cJSON *r = json_de(fj_scan(d, &j), &j);
  TT_ASSERT(r != NULL);
  cJSON *p = cJSON_GetArrayItem(cJSON_GetObjectItem(r, "parts"), 0);
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(p, "content")->valuestring, "ext4");
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(p, "label")->valuestring, "FLUTEST");
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(p, "uuid")->valuestring, "0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0");
  TT_ASSERT(cJSON_GetObjectItem(p, "luks") == NULL);
  cJSON_Delete(r);
  TT_ASSERT_EQ(fj_close_disk(d), FL_OK);

  d = fj_open_image(tt_fixture("fat16.img"), true);
  TT_ASSERT(d > 0);
  r = json_de(fj_scan(d, &j), &j);
  fl_json_free(&j);
  TT_ASSERT(r != NULL);
  p = cJSON_GetArrayItem(cJSON_GetObjectItem(r, "parts"), 0);
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(p, "content")->valuestring, "FAT16");
  TT_ASSERT(cJSON_IsTrue(cJSON_GetObjectItem(p, "supported")));
  cJSON_Delete(r);
  int64_t fv = fj_mount(d, 0, NULL, 0, false);
  TT_ASSERT(fv > 0);
  TT_ASSERT_EQ(fj_unmount(fv, false), FL_OK);
  TT_ASSERT_EQ(fj_mount(d, 7, NULL, 0, false), FL_EINVAL);
  TT_ASSERT_EQ(fj_close_disk(d), FL_OK);
  TT_ASSERT_EQ(fj_open_image(tt_fixture("absent.img"), true) < 0, 1);
  TT_ASSERT_EQ(fj_debug_used(), 0);
}

static void test_lecture_plain(void) {
  int64_t d, v = monter_plain(&d);
  TT_ASSERT(v > 0);
  fl_json j;
  fl_json_init(&j);
  cJSON *r = json_de(fj_list(v, "/", &j), &j);
  TT_ASSERT(r != NULL);
  TT_ASSERT(cJSON_IsArray(r));
  TT_ASSERT_EQ(j.max, FL_JSON_LIST_MAX); /* plafond dédié de list */
  int vu_hello = 0, vu_emoji = 0;
  const char *prec = "";
  for (cJSON *e = r->child; e; e = e->next) {
    const char *n = cJSON_GetObjectItem(e, "name")->valuestring;
    TT_ASSERT(strcmp(prec, n) < 0); /* trié par octets */
    prec = n;
    if (!strcmp(n, "hello.txt")) {
      vu_hello = 1;
      TT_ASSERT_STR_EQ(cJSON_GetObjectItem(e, "type")->valuestring, "f");
      TT_ASSERT_EQ(cJSON_GetObjectItem(e, "size")->valuedouble, 16);
      TT_ASSERT(cJSON_GetObjectItem(e, "mtime")->valuedouble > 1e9);
    }
    if (!strcmp(n, "accentué é ☀.txt")) vu_emoji = 1;
    if (!strcmp(n, "sous-dossier")) TT_ASSERT_STR_EQ(cJSON_GetObjectItem(e, "type")->valuestring, "d");
  }
  TT_ASSERT(vu_hello && vu_emoji);
  cJSON_Delete(r);

  r = json_de(fj_stat(v, "/accentué é ☀.txt", &j), &j);
  TT_ASSERT(r != NULL);
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(r, "name")->valuestring, "accentué é ☀.txt");
  TT_ASSERT_EQ(cJSON_GetObjectItem(r, "size")->valuedouble, 5);
  cJSON_Delete(r);
  TT_ASSERT_EQ(fj_stat(v, "/absent", &j), FL_ENOENT);
  TT_ASSERT_EQ(fj_list(v, "/hello.txt", &j), FL_ENOTDIR);
  TT_ASSERT_EQ(fj_list(v, "relatif", &j), FL_EINVAL);

  r = json_de(fj_statfs(v, &j), &j);
  TT_ASSERT(r != NULL);
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(r, "label")->valuestring, "FLUTEST");
  TT_ASSERT(cJSON_IsString(cJSON_GetObjectItem(r, "fs")));
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(r, "fs")->valuestring, "ext4"); /* phase 7 : conseil au débranchement */
  TT_ASSERT(cJSON_IsTrue(cJSON_GetObjectItem(r, "read_only")));
  TT_ASSERT(cJSON_IsFalse(cJSON_GetObjectItem(r, "needs_recovery")));
  TT_ASSERT(cJSON_GetObjectItem(r, "total")->valuedouble > cJSON_GetObjectItem(r, "free")->valuedouble);
  TT_ASSERT(cJSON_IsString(cJSON_GetObjectItem(r, "ro_reason")));
  cJSON_Delete(r);
  fl_json_free(&j);

  int64_t f = fj_open_file(v, "/hello.txt", FL_O_RDONLY);
  TT_ASSERT(f > 0);
  char buf[64];
  TT_ASSERT_EQ(fj_pread(f, buf, sizeof buf, 0), 16);
  TT_ASSERT_MEM_EQ(buf, "Bonjour FluLuks\n", 16);
  TT_ASSERT_EQ(fj_pread(f, buf, sizeof buf, 16), 0);
  TT_ASSERT_EQ(fj_pread(f, buf, 3, 8), 3);
  TT_ASSERT_MEM_EQ(buf, "Flu", 3);
  TT_ASSERT_EQ(fj_open_file(v, "/sous-dossier", FL_O_RDONLY), FL_EISDIR);
  TT_ASSERT_EQ(fj_close_file(f), FL_OK);
  TT_ASSERT_EQ(fj_unmount(v, false), FL_OK);
  TT_ASSERT_EQ(fj_close_disk(d), FL_OK);
  TT_ASSERT_EQ(fj_debug_used(), 0);
}

static void test_luks_phrase(void) {
  char pass[256];
  size_t n = lire_pass(pass, sizeof pass, "pass.txt");
  TT_ASSERT(n > 0);
  int64_t d = fj_open_image(tt_fixture("gpt-luks2.img"), true);
  TT_ASSERT(d > 0);
  TT_ASSERT_EQ(fj_mount(d, 1, "faux", 4, false), FL_EBADPASS);
  TT_ASSERT_EQ(fj_mount(d, 1, NULL, 0, false), FL_EINVAL);
  TT_ASSERT_EQ(fj_mount(d, 2, pass, n, false), FL_EINVAL);
  int64_t v = fj_mount(d, 1, pass, n, false);
  TT_ASSERT(v > 0);
  int64_t f = fj_open_file(v, "/hello.txt", FL_O_RDONLY);
  TT_ASSERT(f > 0);
  char buf[32];
  TT_ASSERT_EQ(fj_pread(f, buf, sizeof buf, 0), 16);
  TT_ASSERT_MEM_EQ(buf, "Bonjour FluLuks\n", 16);
  /* fermer le disque démonte de force le volume et ferme le fichier */
  TT_ASSERT_EQ(fj_close_disk(d), FL_OK);
  TT_ASSERT_EQ(fj_pread(f, buf, sizeof buf, 0), FL_EINVAL);
  TT_ASSERT_EQ(fj_unmount(v, true), FL_EINVAL);
  TT_ASSERT_EQ(fj_debug_used(), 0);
}

static void test_poignees_invalides(void) {
  int64_t d, v = monter_plain(&d);
  TT_ASSERT(v > 0);
  int64_t f = fj_open_file(v, "/hello.txt", FL_O_RDONLY);
  TT_ASSERT(f > 0);
  char buf[8];
  fl_json j;
  fl_json_init(&j);
  /* mauvais type */
  TT_ASSERT_EQ(fj_pread(v, buf, sizeof buf, 0), FL_EINVAL);
  TT_ASSERT_EQ(fj_pread(d, buf, sizeof buf, 0), FL_EINVAL);
  TT_ASSERT_EQ(fj_stat(f, "/", &j), FL_EINVAL);
  TT_ASSERT_EQ(fj_scan(v, &j), FL_EINVAL);
  TT_ASSERT_EQ(fj_close_disk(v), FL_EINVAL);
  TT_ASSERT_EQ(fj_close_file(d), FL_EINVAL);
  TT_ASSERT_EQ(fj_unmount(f, true), FL_EINVAL);
  TT_ASSERT_EQ(fj_mount(v, 0, NULL, 0, false), FL_EINVAL);
  /* valeurs absurdes */
  int64_t absurdes[] = {0, -1, -5, INT64_MIN, INT64_MAX, 1, 1023, (int64_t)1 << 16 | 1024, f + 1, f + ((int64_t)1 << 16)};
  for (size_t i = 0; i < sizeof absurdes / sizeof absurdes[0]; i++) {
    TT_ASSERT_EQ(fj_pread(absurdes[i], buf, sizeof buf, 0), FL_EINVAL);
    TT_ASSERT_EQ(fj_close_file(absurdes[i]), FL_EINVAL);
    TT_ASSERT_EQ(fj_close_disk(absurdes[i]), FL_EINVAL);
    TT_ASSERT_EQ(fj_unmount(absurdes[i], true), FL_EINVAL);
  }
  fl_json_free(&j);
  /* poignée périmée, même si l'entrée est réutilisée */
  TT_ASSERT_EQ(fj_close_file(f), FL_OK);
  TT_ASSERT_EQ(fj_close_file(f), FL_EINVAL);
  TT_ASSERT_EQ(fj_pread(f, buf, sizeof buf, 0), FL_EINVAL);
  int64_t f2 = fj_open_file(v, "/hello.txt", FL_O_RDONLY);
  TT_ASSERT(f2 > 0 && f2 != f);
  TT_ASSERT_EQ(fj_pread(f, buf, sizeof buf, 0), FL_EINVAL);
  TT_ASSERT_EQ(fj_pread(f2, buf, 4, 0), 4);
  /* occupé sans force, puis force */
  TT_ASSERT_EQ(fj_unmount(v, false), FL_EBUSY);
  TT_ASSERT_EQ(fj_pread(f2, buf, 4, 0), 4);
  TT_ASSERT_EQ(fj_unmount(v, true), FL_OK);
  TT_ASSERT_EQ(fj_pread(f2, buf, 4, 0), FL_EINVAL);
  TT_ASSERT_EQ(fj_close_file(f2), FL_EINVAL);
  TT_ASSERT_EQ(fj_unmount(v, false), FL_EINVAL);
  TT_ASSERT_EQ(fj_close_disk(d), FL_OK);
  TT_ASSERT_EQ(fj_close_disk(d), FL_EINVAL);
  TT_ASSERT_EQ(fj_debug_used(), 0);
}

static void test_table_pleine(void) {
  int64_t d, v = monter_plain(&d);
  TT_ASSERT(v > 0);
  unsigned n = 0;
  for (;;) {
    int64_t f = fj_open_file(v, "/vide.txt", FL_O_RDONLY);
    if (f < 0) {
      TT_ASSERT_EQ(f, FL_ENOMEM);
      break;
    }
    n++;
    TT_ASSERT(n <= FJ_MAX);
  }
  TT_ASSERT_EQ(n, FJ_MAX - 2);
  TT_ASSERT_EQ(fj_debug_used(), FJ_MAX);
  TT_ASSERT_EQ(fj_close_disk(d), FL_OK);
  TT_ASSERT_EQ(fj_debug_used(), 0);
}

/* ---- lectures parallèles et fermeture forcée pendant les lectures ---- */

typedef struct {
  int64_t f;
  unsigned seed;
  int ok_reads, einval, bad;
  int stop_on_einval;
} lecteur;

static void *lire_gros(void *arg) {
  lecteur *l = arg;
  static const uint64_t taille = 5u << 20;
  uint8_t *buf = malloc(65536);
  if (!buf) {
    l->bad++;
    return NULL;
  }
  /* stop_on_einval : on lit jusqu'à la fermeture (garde-fou : 2 000 000 lectures) ; sinon 200 lectures */
  for (long it = 0;; it++) {
    if (it >= 2000000) {
      l->bad++;
      break;
    }
    uint64_t off = (uint64_t)rand_r(&l->seed) % taille;
    size_t len = 1 + (size_t)rand_r(&l->seed) % 65536;
    int64_t r = fj_pread(l->f, buf, len, off);
    if (r == FL_EINVAL) {
      l->einval++;
      if (l->stop_on_einval) break;
      continue;
    }
    if (r <= 0 || (uint64_t)r > len || off + (uint64_t)r > taille) {
      l->bad++;
      break;
    }
    for (int64_t i = 0; i < r; i++)
      if (buf[i] != (uint8_t)(((off + (uint64_t)i) * 31 + 7) & 0xFF)) {
        l->bad++;
        break;
      }
    l->ok_reads++;
    if (!l->stop_on_einval && l->ok_reads >= 200) break;
  }
  free(buf);
  return NULL;
}

static void attendre_ms(long ms) {
  struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

static void lecteurs(int fermeture /* 0 aucune, 1 unmount force, 2 close disk, 3 close file */) {
  int64_t d, v = monter_plain(&d);
  TT_ASSERT(v > 0);
  enum { N = 8 };
  pthread_t th[N];
  lecteur ls[N];
  int64_t partage = fj_open_file(v, "/gros.bin", FL_O_RDONLY);
  TT_ASSERT(partage > 0);
  for (int i = 0; i < N; i++) {
    memset(&ls[i], 0, sizeof ls[i]);
    /* moitié sur une poignée partagée, moitié sur des poignées propres */
    ls[i].f = i % 2 ? partage : fj_open_file(v, "/gros.bin", FL_O_RDONLY);
    TT_ASSERT(ls[i].f > 0);
    ls[i].seed = 1234u + (unsigned)i;
    ls[i].stop_on_einval = fermeture != 0 && !(fermeture == 3 && i % 2 == 0);
    TT_ASSERT_EQ(pthread_create(&th[i], NULL, lire_gros, &ls[i]), 0);
  }
  if (fermeture) attendre_ms(150);
  if (fermeture == 1) TT_ASSERT_EQ(fj_unmount(v, true), FL_OK);
  if (fermeture == 2) TT_ASSERT_EQ(fj_close_disk(d), FL_OK);
  if (fermeture == 3) TT_ASSERT_EQ(fj_close_file(partage), FL_OK);
  int total = 0;
  for (int i = 0; i < N; i++) {
    pthread_join(th[i], NULL);
    TT_ASSERT_EQ(ls[i].bad, 0);
    total += ls[i].ok_reads;
    if (fermeture == 1 || fermeture == 2 || (fermeture == 3 && i % 2)) TT_ASSERT_EQ(ls[i].einval, 1);
    else TT_ASSERT_EQ(ls[i].einval, 0);
  }
  TT_ASSERT(total > 0);
  if (fermeture != 2) TT_ASSERT_EQ(fj_close_disk(d), FL_OK);
  TT_ASSERT_EQ(fj_debug_used(), 0);
}

static void test_lectures_paralleles(void) { lecteurs(0); }
static void test_demontage_force_pendant_lectures(void) { lecteurs(1); }
static void test_fermeture_disque_pendant_lectures(void) { lecteurs(2); }
static void test_fermeture_fichier_pendant_lectures(void) { lecteurs(3); }

/* ---- fermeture du disque en cours : FL_EBUSY au lieu d'un FL_EINVAL transitoire ---- */

typedef struct {
  int64_t disk, rc;
  int idx; /* partition (tache_monter_idx) */
} tache;

/* Rendez-vous avec le crochet de fj_mount : le fil de montage s'arrête juste avant d'enregistrer son
 * volume (référence du disque tenue) jusqu'à ce que le test le libère. */
static pthread_mutex_t rv_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rv_cv = PTHREAD_COND_INITIALIZER;
static int rv_atteint, rv_libere;

static void crochet_montage(void *arg) {
  (void)arg;
  pthread_mutex_lock(&rv_mu);
  rv_atteint = 1;
  pthread_cond_broadcast(&rv_cv);
  while (!rv_libere) pthread_cond_wait(&rv_cv, &rv_mu);
  pthread_mutex_unlock(&rv_mu);
}

static void liberer_montage(void) {
  pthread_mutex_lock(&rv_mu);
  rv_libere = 1;
  pthread_cond_broadcast(&rv_cv);
  pthread_mutex_unlock(&rv_mu);
}

static void *tache_monter(void *arg) {
  tache *t = arg;
  t->rc = fj_mount(t->disk, 0, NULL, 0, false);
  return NULL;
}

static void *tache_fermer(void *arg) {
  tache *t = arg;
  t->rc = fj_close_disk(t->disk);
  return NULL;
}

/* Attend l'atteinte du crochet (au plus 60 s). */
static int attendre_crochet(void) {
  struct timespec dl;
  clock_gettime(CLOCK_REALTIME, &dl);
  dl.tv_sec += 60;
  int ok = 1;
  pthread_mutex_lock(&rv_mu);
  while (!rv_atteint && ok) ok = pthread_cond_timedwait(&rv_cv, &rv_mu, &dl) == 0;
  pthread_mutex_unlock(&rv_mu);
  return rv_atteint;
}

/* Attend (au plus 60 s, sans dormir) que le disque soit en cours de fermeture. */
static int attendre_fermeture(int64_t h) {
  struct timespec t0, t;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (;;) {
    unsigned r = 0;
    bool c = false;
    if (fj_debug_slot(h, &r, &c) == FL_OK && c) return 1;
    clock_gettime(CLOCK_MONOTONIC, &t);
    if (t.tv_sec - t0.tv_sec > 60) return 0;
    sched_yield();
  }
}

/* Un fj_mount arrêté par le crochet tient une référence sur le disque ; un fj_close_disk le marque
 * « en fermeture » et attend. Pendant cette fenêtre, ouverte exprès :
 *   - fj_unmount(v1, false) rend FL_EBUSY (le disque démontera v1 lui-même) ;
 *   - le fj_mount libéré ne peut plus enregistrer son volume : FL_EBUSY, et rien ne fuit. */
static void test_handles_ebusy_fermeture(void) {
  int64_t d, v1 = monter_plain(&d);
  TT_ASSERT(v1 > 0);
  rv_atteint = rv_libere = 0;
  tache monte = {d, 0, 0}, ferme = {d, 0, 0};
  pthread_t ta, tb;
  fj_debug_set_mount_hook(crochet_montage, NULL);
  int a_ok = pthread_create(&ta, NULL, tache_monter, &monte) == 0;
  int ok_a = a_ok && attendre_crochet();
  int b_ok = 0, ok_b = 0, um = -9999;
  if (ok_a) {
    b_ok = pthread_create(&tb, NULL, tache_fermer, &ferme) == 0;
    ok_b = b_ok && attendre_fermeture(d);
    if (ok_b) um = fj_unmount(v1, false);
  }
  liberer_montage();
  if (a_ok) pthread_join(ta, NULL);
  if (b_ok) pthread_join(tb, NULL);
  fj_debug_set_mount_hook(NULL, NULL);
  if (!b_ok) fj_close_disk(d); /* chemins d'échec : ne rien laisser ouvert */
  TT_ASSERT(ok_a && ok_b);
  TT_ASSERT_EQ(um, FL_EBUSY);
  TT_ASSERT_EQ(monte.rc, FL_EBUSY);
  TT_ASSERT_EQ(ferme.rc, FL_OK);
  TT_ASSERT_EQ(fj_unmount(v1, true), FL_EINVAL);
  TT_ASSERT_EQ(fj_unmount(v1, false), FL_EINVAL);
  TT_ASSERT_EQ(fj_debug_used(), 0);
}

/* ---------- écriture (phase 3, T10) ---------- */

static cJSON *statfs_de(int64_t v) {
  fl_json j;
  fl_json_init(&j);
  cJSON *r = json_de(fj_statfs(v, &j), &j);
  fl_json_free(&j);
  return r;
}

/* Ressources d'un test d'écriture : libérées par nettoyer_ctx même quand une assertion échoue. */
typedef struct {
  char img[4096];
  int64_t d, v, f;
  cJSON *r;
} ctx_ecr;

static void nettoyer_ctx(ctx_ecr *c) {
  if (c->r) cJSON_Delete(c->r);
  c->r = NULL;
  if (c->f > 0) fj_close_file(c->f);
  if (c->v > 0) fj_unmount(c->v, true);
  if (c->d > 0) fj_close_disk(c->d);
  c->f = c->v = c->d = 0;
  if (c->img[0]) unlink(c->img);
  c->img[0] = 0;
}

/* Création, écriture, fermeture (synchronisée), renommage, dossiers, démontage, puis e2fsck -fn
 * propre sur l'image (PC seulement). */
static void corps_ecriture(ctx_ecr *c) {
  char src[4096], out[1 << 14];
  snprintf(src, sizeof src, "%s", tt_fixture("ext4-rw-reel.img"));
  TT_ASSERT_EQ(fu_copy_image(src, c->img, sizeof c->img), 0);
  c->d = fj_open_image(c->img, false);
  TT_ASSERT(c->d > 0);
  c->v = fj_mount(c->d, 0, NULL, 0, true);
  TT_ASSERT(c->v > 0);
  int64_t d = c->d, v = c->v;
  c->r = statfs_de(v);
  TT_ASSERT(c->r != NULL);
  TT_ASSERT(cJSON_IsFalse(cJSON_GetObjectItem(c->r, "read_only")));
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(c->r, "ro_reason")->valuestring, "");
  cJSON_Delete(c->r);
  c->r = NULL;
  uint8_t b[5000];
  for (size_t i = 0; i < sizeof b; i++) b[i] = (uint8_t)(i * 7);
  c->f = fj_open_file(v, "/jni.bin", FL_O_RDWR | FL_O_CREAT);
  TT_ASSERT(c->f > 0);
  int64_t f = c->f;
  TT_ASSERT_EQ(fj_pwrite(f, b, sizeof b, 0), (int64_t)sizeof b);
  TT_ASSERT_EQ(fj_pwrite(f, b, FJ_PREAD_MAX + 1, 0), FL_EINVAL);
  TT_ASSERT_EQ(fj_truncate(f, 4000), FL_OK);
  int64_t avant = fj_debug_flushes(d);
  TT_ASSERT_EQ(fj_close_file(f), FL_OK);
  c->f = 0;
  TT_ASSERT(fj_debug_flushes(d) > avant); /* fermeture d'un fichier écrit : synchronisation */
  TT_ASSERT_EQ(fj_mkdir(v, "/jd"), FL_OK);
  TT_ASSERT_EQ(fj_mkdir(v, "/jd"), FL_EEXIST);
  TT_ASSERT_EQ(fj_rename(v, "/jni.bin", "/jd/jni2.bin"), FL_OK);
  TT_ASSERT_EQ(fj_mkdir(v, "/vide"), FL_OK);
  TT_ASSERT_EQ(fj_rmdir(v, "/vide"), FL_OK);
  TT_ASSERT_EQ(fj_unlink(v, "/hello.txt"), FL_OK);
  TT_ASSERT_EQ(fj_sync(v), FL_OK);
  TT_ASSERT_EQ(fj_mkdir(0, "/x"), FL_EINVAL);
  TT_ASSERT_EQ(fj_rename(v, NULL, "/x"), FL_EINVAL);
  avant = fj_debug_flushes(d);
  TT_ASSERT_EQ(fj_unmount(v, false), FL_OK);
  c->v = 0;
  TT_ASSERT(fj_debug_flushes(d) > avant);
  TT_ASSERT_EQ(fj_close_disk(d), FL_OK);
  c->d = 0;
  int rc = fu_e2fsck_n(c->img, out, sizeof out);
  if (rc != 0) fprintf(stderr, "%s\n", out);
  TT_ASSERT_EQ(rc, 0);
  uint8_t lu[5001];
  TT_ASSERT_EQ(fu_debugfs_cat(c->img, "/jd/jni2.bin", lu, sizeof lu), 4000);
  TT_ASSERT_MEM_EQ(lu, b, 4000);
}

/* Vrai (1) si le test a été sauté faute de e2fsck/debugfs. */
static int g_saute;

static void test_handles_ecriture(void) {
  if (!fu_tools_available()) {
    fprintf(stderr, "test_handles_ecriture sauté : e2fsck/debugfs absents\n");
    g_saute = 1;
    return;
  }
  ctx_ecr c;
  memset(&c, 0, sizeof c);
  corps_ecriture(&c);
  nettoyer_ctx(&c);
}

static void corps_lecture_seule(ctx_ecr *c) {
  c->d = fj_open_image(tt_fixture("ext4-rw-reel.img"), true);
  TT_ASSERT(c->d > 0);
  c->v = fj_mount(c->d, 0, NULL, 0, false);
  TT_ASSERT(c->v > 0);
  int64_t v = c->v;
  c->r = statfs_de(v);
  TT_ASSERT(c->r != NULL);
  TT_ASSERT(cJSON_IsTrue(cJSON_GetObjectItem(c->r, "read_only")));
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(c->r, "ro_reason")->valuestring, FL_RO_USER);
  cJSON_Delete(c->r);
  c->r = NULL;
  TT_ASSERT_EQ(fj_open_file(v, "/x", FL_O_RDWR | FL_O_CREAT), FL_EROFS);
  TT_ASSERT_EQ(fj_mkdir(v, "/x"), FL_EROFS);
  TT_ASSERT_EQ(fj_close_disk(c->d), FL_OK);
  c->d = c->v = 0;
  /* écriture demandée sur une image ouverte en lecture seule : disque protégé */
  c->d = fj_open_image(tt_fixture("ext4-rw-reel.img"), true);
  TT_ASSERT(c->d > 0);
  c->v = fj_mount(c->d, 0, NULL, 0, true);
  TT_ASSERT(c->v > 0);
  c->r = statfs_de(c->v);
  TT_ASSERT(c->r != NULL);
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(c->r, "ro_reason")->valuestring, FL_RO_WP);
}

static void test_handles_lecture_seule_demandee(void) {
  ctx_ecr c;
  memset(&c, 0, sizeof c);
  corps_lecture_seule(&c);
  nettoyer_ctx(&c);
}

/* FAT32 (phase 4) par le pont JNI : sondage, montage en écriture, écritures, démontage, puis fsck propre.
 * luks : la même image chiffrée en LUKS2 (déchiffrée par le moteur pour le juge). PC seulement. */
static void corps_fat(ctx_ecr *c, int luks) {
  char src[4096], out[1 << 14];
  snprintf(src, sizeof src, "%s", tt_fixture(luks ? "luks2-fat32.img" : "fat32-arbre.img"));
  TT_ASSERT_EQ(fu_copy_image(src, c->img, sizeof c->img), 0);
  c->d = fj_open_image(c->img, false);
  TT_ASSERT(c->d > 0);
  fl_json j;
  fl_json_init(&j);
  c->r = json_de(fj_scan(c->d, &j), &j);
  fl_json_free(&j);
  TT_ASSERT(c->r != NULL);
  cJSON *p = cJSON_GetArrayItem(cJSON_GetObjectItem(c->r, "parts"), 0);
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(p, "content")->valuestring, luks ? "LUKS2" : "FAT32");
  TT_ASSERT(cJSON_IsTrue(cJSON_GetObjectItem(p, "supported")));
  cJSON_Delete(c->r);
  c->r = NULL;
  char pass[256];
  size_t n = luks ? lire_pass(pass, sizeof pass, "pass.txt") : 0;
  c->v = fj_mount(c->d, 0, luks ? pass : NULL, n, true);
  TT_ASSERT(c->v > 0);
  int64_t v = c->v;
  c->r = statfs_de(v);
  TT_ASSERT(c->r != NULL);
  TT_ASSERT(cJSON_IsFalse(cJSON_GetObjectItem(c->r, "read_only")));
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(c->r, "label")->valuestring, "FLUFAT32");
  TT_ASSERT(cJSON_IsString(cJSON_GetObjectItem(c->r, "fs")));
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(c->r, "fs")->valuestring, "FAT32");
  cJSON_Delete(c->r);
  c->r = NULL;
  TT_ASSERT_EQ(fj_mkdir(v, "/jni"), FL_OK);
  c->f = fj_open_file(v, "/jni/f.txt", FL_O_RDWR | FL_O_CREAT);
  TT_ASSERT(c->f > 0);
  TT_ASSERT_EQ(fj_pwrite(c->f, "fat", 3, 0), 3);
  TT_ASSERT_EQ(fj_close_file(c->f), FL_OK);
  c->f = 0;
  TT_ASSERT_EQ(fj_rename(v, "/abc.txt", "/ABC.TXT"), FL_OK); /* casse seule */
  TT_ASSERT_EQ(fj_unmount(v, false), FL_OK);
  c->v = 0;
  TT_ASSERT_EQ(fj_close_disk(c->d), FL_OK);
  c->d = 0;
  char clair[4096];
  const char *juge = c->img;
  if (luks) {
    char pf[4096];
    snprintf(pf, sizeof pf, "%s", tt_fixture("pass.txt"));
    TT_ASSERT_EQ(fu_extract_luks_ext4(c->img, pf, clair, sizeof clair), 0);
    juge = clair;
  }
  int rc = fu_fsck_fat_n(juge, out, sizeof out);
  if (rc != 0) fprintf(stderr, "%s\n", out);
  if (luks) unlink(clair);
  TT_ASSERT_EQ(rc, 0);
}

static void test_handles_fat32(void) {
  if (!fu_fat_tools_available()) return;
  ctx_ecr c;
  memset(&c, 0, sizeof c);
  corps_fat(&c, 0);
  nettoyer_ctx(&c);
}

static void test_handles_luks2_fat32(void) {
  if (!fu_fat_tools_available()) return;
  ctx_ecr c;
  memset(&c, 0, sizeof c);
  corps_fat(&c, 1);
  nettoyer_ctx(&c);
}

/* NTFS (phase 5) par le pont JNI : sondage, montage en écriture, écritures, éjection, puis juges ntfsjuge.py
 * (cohérence, drapeau sale effacé) et ntfsfix -n. luks : la même image chiffrée en LUKS2. PC seulement. */
static void corps_ntfs(ctx_ecr *c, int luks) {
  char src[4096], out[1 << 14];
  snprintf(src, sizeof src, "%s", tt_fixture(luks ? "luks2-ntfs.img" : "ntfs-arbre.img"));
  TT_ASSERT_EQ(fu_copy_image(src, c->img, sizeof c->img), 0);
  c->d = fj_open_image(c->img, false);
  TT_ASSERT(c->d > 0);
  fl_json j;
  fl_json_init(&j);
  c->r = json_de(fj_scan(c->d, &j), &j);
  fl_json_free(&j);
  TT_ASSERT(c->r != NULL);
  cJSON *p = cJSON_GetArrayItem(cJSON_GetObjectItem(c->r, "parts"), 0);
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(p, "content")->valuestring, luks ? "LUKS2" : "NTFS");
  TT_ASSERT(cJSON_IsTrue(cJSON_GetObjectItem(p, "supported")));
  cJSON_Delete(c->r);
  c->r = NULL;
  char pass[256];
  size_t n = luks ? lire_pass(pass, sizeof pass, "pass.txt") : 0;
  c->v = fj_mount(c->d, 0, luks ? pass : NULL, n, true);
  TT_ASSERT(c->v > 0);
  int64_t v = c->v;
  c->r = statfs_de(v);
  TT_ASSERT(c->r != NULL);
  TT_ASSERT(cJSON_IsFalse(cJSON_GetObjectItem(c->r, "read_only")));
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(c->r, "label")->valuestring, "FLUNTFS");
  TT_ASSERT(cJSON_IsString(cJSON_GetObjectItem(c->r, "fs")));
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(c->r, "fs")->valuestring, "NTFS");
  cJSON_Delete(c->r);
  c->r = NULL;
  TT_ASSERT_EQ(fj_mkdir(v, "/jni"), FL_OK);
  c->f = fj_open_file(v, "/jni/f.txt", FL_O_RDWR | FL_O_CREAT);
  TT_ASSERT(c->f > 0);
  TT_ASSERT_EQ(fj_pwrite(c->f, "ntfs", 4, 0), 4);
  TT_ASSERT_EQ(fj_close_file(c->f), FL_OK);
  c->f = 0;
  TT_ASSERT_EQ(fj_rename(v, "/abc.txt", "/ABC.txt"), FL_OK); /* casse seule */
  TT_ASSERT_EQ(fj_mkdir(v, "/aux"), FL_EINVAL);              /* nom réservé par Windows */
  TT_ASSERT_EQ(fj_unmount(v, false), FL_OK);
  c->v = 0;
  TT_ASSERT_EQ(fj_close_disk(c->d), FL_OK);
  c->d = 0;
  char clair[4096];
  const char *juge = c->img;
  if (luks) {
    char pf[4096];
    snprintf(pf, sizeof pf, "%s", tt_fixture("pass.txt"));
    TT_ASSERT_EQ(fu_extract_luks_ext4(c->img, pf, clair, sizeof clair), 0);
    juge = clair;
  }
  int rc = fu_ntfsjuge(out, sizeof out, "check", juge, NULL);
  if (rc != 0) fprintf(stderr, "%s\n", out);
  int drapeaux = fu_ntfs_flags(juge);
  int fix = fu_ntfsfix_n(juge, out, sizeof out);
  if (fix != 0) fprintf(stderr, "%s\n", out);
  if (luks) unlink(clair);
  TT_ASSERT_EQ(rc, 0);
  TT_ASSERT_EQ(drapeaux & 1, 0); /* VOLUME_IS_DIRTY effacé à l'éjection */
  TT_ASSERT_EQ(fix, 0);
}

static void test_handles_ntfs(void) {
  if (!fu_ntfs_tools_available()) return;
  ctx_ecr c;
  memset(&c, 0, sizeof c);
  corps_ntfs(&c, 0);
  nettoyer_ctx(&c);
}

static void test_handles_luks2_ntfs(void) {
  if (!fu_ntfs_tools_available()) return;
  ctx_ecr c;
  memset(&c, 0, sizeof c);
  corps_ntfs(&c, 1);
  nettoyer_ctx(&c);
}

/* --- formatage (phase 8, T6) ----------------------------------------------------------------- */

static int g_pct[2];
static void suivre(int pct, void *arg) {
  (void)arg;
  g_pct[0] = pct;
  g_pct[1]++;
}

/* Image vierge de 64 Mio dans le dossier temporaire ; chemin dans out. */
static void image_vierge(char *out, size_t cap, const char *nom) {
  char td[4096];
  TT_ASSERT_EQ(fu_tmpdir(td, sizeof td), 0);
  snprintf(out, cap, "%s/%s", td, nom);
  FILE *f = fopen(out, "wb");
  TT_ASSERT(f != NULL);
  fclose(f);
  TT_ASSERT_EQ(truncate(out, 64 * 1024 * 1024), 0);
}

/* Index et contenu de l'unique partition du disque. */
static int partition_unique(int64_t d, char *contenu, size_t cap) {
  fl_json j;
  fl_json_init(&j);
  cJSON *r = json_de(fj_scan(d, &j), &j);
  fl_json_free(&j);
  if (!r) return -1;
  cJSON *parts = cJSON_GetObjectItem(r, "parts");
  int idx = -1;
  if (cJSON_GetArraySize(parts) == 1) {
    cJSON *p = cJSON_GetArrayItem(parts, 0);
    idx = (int)cJSON_GetObjectItem(p, "index")->valuedouble;
    snprintf(contenu, cap, "%s", cJSON_GetObjectItem(p, "content")->valuestring);
  }
  cJSON_Delete(r);
  return idx;
}

static void test_format_par_jni(void) {
  char img[4200], contenu[32];
  image_vierge(img, sizeof img, "format-jni.img");
  int64_t d = fj_open_image(img, false);
  TT_ASSERT(d > 0);
  g_pct[0] = g_pct[1] = 0;
  TT_ASSERT_EQ(fj_format(d, "{\"fs\":\"exfat\",\"encrypted\":false,\"label\":\"Clé\"}", NULL, 0, suivre, NULL),
               FL_OK);
  TT_ASSERT_EQ(g_pct[0], 100);
  TT_ASSERT(g_pct[1] >= 3);
  int idx = partition_unique(d, contenu, sizeof contenu);
  TT_ASSERT(idx >= 0);
  TT_ASSERT_STR_EQ(contenu, "exFAT");
  int64_t v = fj_mount(d, (uint32_t)idx, NULL, 0, true);
  TT_ASSERT(v > 0);
  int64_t f = fj_open_file(v, "/été.txt", FL_O_RDWR | FL_O_CREAT);
  TT_ASSERT(f > 0);
  TT_ASSERT_EQ(fj_pwrite(f, "bonjour", 7, 0), 7);
  TT_ASSERT_EQ(fj_close_file(f), FL_OK);
  /* volume ouvert : refus, rien d'écrit */
  TT_ASSERT_EQ(fj_format(d, "{\"fs\":\"ext4\"}", NULL, 0, NULL, NULL), FL_EBUSY);
  TT_ASSERT_EQ(fj_unmount(v, false), FL_OK);

  /* ext4 chiffré, puis ouvert avec la phrase */
  const char *pass = "phrase de passe";
  TT_ASSERT_EQ(fj_format(d, "{\"fs\":\"ext4\",\"encrypted\":true,\"label\":\"Données\",\"kdf_mem_kib\":32768}",
                         pass, strlen(pass), NULL, NULL),
               FL_OK);
  idx = partition_unique(d, contenu, sizeof contenu);
  TT_ASSERT_STR_EQ(contenu, "LUKS2");
  TT_ASSERT_EQ(fj_mount(d, (uint32_t)idx, "faux", 4, true), FL_EBADPASS);
  v = fj_mount(d, (uint32_t)idx, pass, strlen(pass), true);
  TT_ASSERT(v > 0);
  TT_ASSERT_EQ(fj_mkdir(v, "/Photos"), FL_OK);
  TT_ASSERT_EQ(fj_unmount(v, false), FL_OK);

  /* options invalides */
  TT_ASSERT_EQ(fj_format(d, "pas du json", NULL, 0, NULL, NULL), FL_EINVAL);
  TT_ASSERT_EQ(fj_format(d, "{\"fs\":\"btrfs\"}", NULL, 0, NULL, NULL), FL_EINVAL);
  TT_ASSERT_EQ(fj_format(d, "{\"fs\":\"ext4\",\"encrypted\":true}", NULL, 0, NULL, NULL), FL_EINVAL);
  TT_ASSERT_EQ(fj_format(d, NULL, NULL, 0, NULL, NULL), FL_EINVAL);
  TT_ASSERT_EQ(fj_close_disk(d), FL_OK);

  /* disque en lecture seule */
  d = fj_open_image(img, true);
  TT_ASSERT(d > 0);
  TT_ASSERT_EQ(fj_format(d, "{\"fs\":\"exfat\"}", NULL, 0, NULL, NULL), FL_EROFS);
  TT_ASSERT_EQ(fj_close_disk(d), FL_OK);
  TT_ASSERT_EQ(fj_format(d, "{\"fs\":\"exfat\"}", NULL, 0, NULL, NULL), FL_EINVAL); /* poignée fermée */
  TT_ASSERT_EQ(fj_debug_used(), 0);
  unlink(img);
}

static void *tache_monter_idx(void *arg) {
  tache *t = arg;
  t->rc = fj_mount(t->disk, (uint32_t)t->idx, NULL, 0, true);
  return NULL;
}

/* Un montage commencé avant le formatage (arrêté par le crochet après l'ouverture du volume, avant son
 * enregistrement) : fj_format le voit et refuse, rien n'est écrit sous le volume en cours d'ouverture. */
static void test_format_pendant_montage(void) {
  char img[4200], contenu[32];
  image_vierge(img, sizeof img, "format-montage.img");
  int64_t d = fj_open_image(img, false);
  TT_ASSERT(d > 0);
  TT_ASSERT_EQ(fj_format(d, "{\"fs\":\"exfat\"}", NULL, 0, NULL, NULL), FL_OK);
  int idx = partition_unique(d, contenu, sizeof contenu);
  TT_ASSERT(idx >= 0);
  rv_atteint = rv_libere = 0;
  tache monte = {d, 0, idx};
  pthread_t ta;
  fj_debug_set_mount_hook(crochet_montage, NULL);
  int a_ok = pthread_create(&ta, NULL, tache_monter_idx, &monte) == 0;
  int ok_a = a_ok && attendre_crochet();
  int fmt = ok_a ? fj_format(d, "{\"fs\":\"ntfs\"}", NULL, 0, NULL, NULL) : -9999;
  liberer_montage();
  if (a_ok) pthread_join(ta, NULL);
  fj_debug_set_mount_hook(NULL, NULL);
  TT_ASSERT(ok_a);
  TT_ASSERT_EQ(fmt, FL_EBUSY);
  TT_ASSERT(monte.rc > 0);
  TT_ASSERT_EQ(partition_unique(d, contenu, sizeof contenu), idx);
  TT_ASSERT_STR_EQ(contenu, "exFAT");
  if (monte.rc > 0) TT_ASSERT_EQ(fj_unmount(monte.rc, false), FL_OK);
  TT_ASSERT_EQ(fj_format(d, "{\"fs\":\"ntfs\"}", NULL, 0, NULL, NULL), FL_OK); /* plus rien en cours */
  TT_ASSERT_EQ(fj_close_disk(d), FL_OK);
  TT_ASSERT_EQ(fj_debug_used(), 0);
  unlink(img);
}

int main(void) {
  srand((unsigned)time(NULL));
  TT_RUN(test_scan_gpt_luks2);
  TT_RUN(test_scan_primaire_abime);
  TT_RUN(test_scan_ext4_et_fat);
  TT_RUN(test_lecture_plain);
  TT_RUN(test_poignees_invalides);
  TT_RUN(test_table_pleine);
  TT_RUN(test_lectures_paralleles);
  TT_RUN(test_demontage_force_pendant_lectures);
  TT_RUN(test_fermeture_disque_pendant_lectures);
  TT_RUN(test_fermeture_fichier_pendant_lectures);
  TT_RUN(test_luks_phrase);
  TT_RUN(test_handles_ebusy_fermeture);
  TT_RUN(test_handles_ecriture);
  TT_RUN(test_handles_lecture_seule_demandee);
  TT_RUN(test_handles_fat32);
  TT_RUN(test_handles_luks2_fat32);
  TT_RUN(test_handles_ntfs);
  TT_RUN(test_handles_luks2_ntfs);
  TT_RUN(test_format_par_jni);
  TT_RUN(test_format_pendant_montage);
  if (!tt_failures && g_saute) return 77; /* SKIP_RETURN_CODE de CTest */
  TT_MAIN_END();
}
