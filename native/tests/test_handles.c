#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "fluluks/error.h"
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
  *disk = fj_open_image(tt_fixture("ext4-plain.img"));
  if (*disk <= 0) return *disk;
  return fj_mount(*disk, 0, NULL, 0);
}

static void test_scan_gpt_luks2(void) {
  int64_t d = fj_open_image(tt_fixture("gpt-luks2.img"));
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
  int64_t d = fj_open_image(dst);
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
  int64_t d = fj_open_image(tt_fixture("ext4-plain.img"));
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

  d = fj_open_image(tt_fixture("fat16.img"));
  TT_ASSERT(d > 0);
  r = json_de(fj_scan(d, &j), &j);
  fl_json_free(&j);
  TT_ASSERT(r != NULL);
  p = cJSON_GetArrayItem(cJSON_GetObjectItem(r, "parts"), 0);
  TT_ASSERT_STR_EQ(cJSON_GetObjectItem(p, "content")->valuestring, "FAT16");
  TT_ASSERT(cJSON_IsFalse(cJSON_GetObjectItem(p, "supported")));
  cJSON_Delete(r);
  TT_ASSERT_EQ(fj_mount(d, 0, NULL, 0), FL_ENOTSUP);
  TT_ASSERT_EQ(fj_mount(d, 7, NULL, 0), FL_EINVAL);
  TT_ASSERT_EQ(fj_close_disk(d), FL_OK);
  TT_ASSERT_EQ(fj_open_image(tt_fixture("absent.img")) < 0, 1);
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
  TT_ASSERT(cJSON_IsTrue(cJSON_GetObjectItem(r, "read_only")));
  TT_ASSERT(cJSON_IsFalse(cJSON_GetObjectItem(r, "needs_recovery")));
  TT_ASSERT(cJSON_GetObjectItem(r, "total")->valuedouble > cJSON_GetObjectItem(r, "free")->valuedouble);
  TT_ASSERT(cJSON_IsString(cJSON_GetObjectItem(r, "ro_reason")));
  cJSON_Delete(r);
  fl_json_free(&j);

  int64_t f = fj_open_file(v, "/hello.txt");
  TT_ASSERT(f > 0);
  char buf[64];
  TT_ASSERT_EQ(fj_pread(f, buf, sizeof buf, 0), 16);
  TT_ASSERT_MEM_EQ(buf, "Bonjour FluLuks\n", 16);
  TT_ASSERT_EQ(fj_pread(f, buf, sizeof buf, 16), 0);
  TT_ASSERT_EQ(fj_pread(f, buf, 3, 8), 3);
  TT_ASSERT_MEM_EQ(buf, "Flu", 3);
  TT_ASSERT_EQ(fj_open_file(v, "/sous-dossier"), FL_EISDIR);
  TT_ASSERT_EQ(fj_close_file(f), FL_OK);
  TT_ASSERT_EQ(fj_unmount(v, false), FL_OK);
  TT_ASSERT_EQ(fj_close_disk(d), FL_OK);
  TT_ASSERT_EQ(fj_debug_used(), 0);
}

static void test_luks_phrase(void) {
  char pass[256];
  size_t n = lire_pass(pass, sizeof pass, "pass.txt");
  TT_ASSERT(n > 0);
  int64_t d = fj_open_image(tt_fixture("gpt-luks2.img"));
  TT_ASSERT(d > 0);
  TT_ASSERT_EQ(fj_mount(d, 1, "faux", 4), FL_EBADPASS);
  TT_ASSERT_EQ(fj_mount(d, 1, NULL, 0), FL_EINVAL);
  TT_ASSERT_EQ(fj_mount(d, 2, pass, n), FL_EINVAL);
  int64_t v = fj_mount(d, 1, pass, n);
  TT_ASSERT(v > 0);
  int64_t f = fj_open_file(v, "/hello.txt");
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
  int64_t f = fj_open_file(v, "/hello.txt");
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
  TT_ASSERT_EQ(fj_mount(v, 0, NULL, 0), FL_EINVAL);
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
  int64_t f2 = fj_open_file(v, "/hello.txt");
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
    int64_t f = fj_open_file(v, "/vide.txt");
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
  int64_t partage = fj_open_file(v, "/gros.bin");
  TT_ASSERT(partage > 0);
  for (int i = 0; i < N; i++) {
    memset(&ls[i], 0, sizeof ls[i]);
    /* moitié sur une poignée partagée, moitié sur des poignées propres */
    ls[i].f = i % 2 ? partage : fj_open_file(v, "/gros.bin");
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
  TT_MAIN_END();
}
