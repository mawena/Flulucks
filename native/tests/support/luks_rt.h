#ifndef FL_LUKS_RT_H
#define FL_LUKS_RT_H
/* Aller-retour d'écriture LUKS2 sur une copie d'une image (partagé par test_luks2 et test_luks2_lent).
 * Inclure après tt.h, stdio, string, unistd et luks/luks_internal.h. */

static size_t rt_lire_pass(char *buf, size_t cap) {
  FILE *f = fopen(tt_fixture("pass.txt"), "rb");
  if (!f) return 0;
  size_t n = fread(buf, 1, cap, f);
  fclose(f);
  return n;
}

static int copier_fichier(const char *src, const char *dst) {
  FILE *a = fopen(src, "rb"), *b = fopen(dst, "wb");
  int ok = a && b;
  static unsigned char buf[1u << 20];
  size_t n;
  while (ok && (n = fread(buf, 1, sizeof buf, a)) > 0) ok = fwrite(buf, 1, n, b) == n;
  if (a) fclose(a);
  if (b && fclose(b) != 0) ok = 0;
  return ok;
}

static void remplir(uint8_t *p, size_t n, uint32_t graine) {
  for (size_t i = 0; i < n; i++) {
    graine = graine * 1103515245u + 12345u;
    p[i] = (uint8_t)(graine >> 16);
  }
}

static fl_bdev *ouvrir_copie(const char *copie, bool lecture_seule, fl_luks **l) {
  char pass[256];
  size_t n = rt_lire_pass(pass, sizeof pass);
  fl_bdev *d = NULL, *dec = NULL;
  *l = NULL;
  if (n == 0 || fl_file_bdev_open(copie, 512, lecture_seule, &d) != FL_OK) return NULL;
  if (fl_luks_open(d, l) != FL_OK || fl_luks_unlock(*l, pass, n, &dec) != FL_OK) {
    fl_luks_close(*l);
    *l = NULL;
    fl_bdev_close(d);
    return NULL;
  }
  return dec;
}

/* Copie `img`, écrit 3 fois (un secteur au LBA 0, un au LBA 1000, trois secteurs terminant le disque),
 * vérifie le texte chiffré brut, rouvre, redéverrouille, relit. La copie n'est pas supprimée ici. */
static void aller_retour(const char *img, const char *copie) {
  char orig[4096]; /* tt_fixture rend un tampon statique */
  snprintf(orig, sizeof orig, "%s", tt_fixture(img));
  TT_ASSERT(copier_fichier(orig, copie));
  fl_luks *l = NULL;
  fl_bdev *dec = ouvrir_copie(copie, false, &l);
  TT_ASSERT(dec != NULL);
  fl_bdev *parent = l->dev;
  TT_ASSERT(!dec->read_only);
  const uint32_t ss = dec->sector_size;
  uint64_t n = dec->sector_count;
  TT_ASSERT(n > 1002 && ss <= 4096);
  static const uint32_t cnt[3] = {1, 1, 3};
  uint64_t lba[3] = {0, 1000, n - 3};
  static uint8_t data[3][3 * 4096], avant[3][3 * 4096];
  for (int i = 0; i < 3; i++) {
    TT_ASSERT_EQ(fl_bdev_read(dec, lba[i], cnt[i], avant[i]), FL_OK);
    remplir(data[i], sizeof data[i], 77u + (uint32_t)i);
    TT_ASSERT(memcmp(avant[i], data[i], cnt[i] * ss) != 0);
    TT_ASSERT_EQ(fl_bdev_write(dec, lba[i], cnt[i], data[i]), FL_OK);
  }
  TT_ASSERT_EQ(fl_bdev_flush(dec), FL_OK);
  fl_bdev_close(dec);
  fl_luks_close(l);
  fl_bdev_close(parent);

  /* le texte chiffré a changé, et seulement aux 5 secteurs écrits */
  FILE *a = fopen(orig, "rb"), *b = fopen(copie, "rb");
  TT_ASSERT(a && b);
  static uint8_t sa[4096], sb[4096];
  uint64_t diff[16], nd = 0;
  for (uint64_t s = 0; fread(sa, 1, ss, a) == ss && fread(sb, 1, ss, b) == ss; s++)
    if (memcmp(sa, sb, ss) != 0 && nd < 16) diff[nd++] = s;
  fclose(a);
  fclose(b);
  TT_ASSERT_EQ(nd, 5);
  uint64_t base = diff[0]; /* secteur brut du LBA 0 : début de la zone de données */
  TT_ASSERT(base > 0);
  TT_ASSERT_EQ(diff[1], base + 1000);
  TT_ASSERT_EQ(diff[2], base + n - 3);
  TT_ASSERT_EQ(diff[3], base + n - 2);
  TT_ASSERT_EQ(diff[4], base + n - 1);
  FILE *f = fopen(copie, "rb");
  TT_ASSERT(f != NULL);
  for (int i = 0; i < 3; i++) {
    static uint8_t raw[3 * 4096];
    size_t len = (size_t)cnt[i] * ss;
    int ok = fseeko(f, (off_t)((base + lba[i]) * ss), SEEK_SET) == 0 && fread(raw, 1, len, f) == len;
    if (!ok || memcmp(raw, data[i], len) == 0 || memcmp(raw, avant[i], len) == 0) {
      fprintf(stderr, "texte chiffré inattendu au LBA %llu\n", (unsigned long long)lba[i]);
      tt_failures++;
      fclose(f);
      return;
    }
  }
  fclose(f);

  /* on rouvre, on redéverrouille : les mêmes octets */
  dec = ouvrir_copie(copie, true, &l);
  TT_ASSERT(dec != NULL);
  parent = l->dev;
  TT_ASSERT_EQ(dec->sector_count, n);
  for (int i = 0; i < 3; i++) {
    static uint8_t rd[3 * 4096];
    TT_ASSERT_EQ(fl_bdev_read(dec, lba[i], cnt[i], rd), FL_OK);
    TT_ASSERT_MEM_EQ(rd, data[i], (size_t)cnt[i] * ss);
  }
  fl_bdev_close(dec);
  fl_luks_close(l);
  fl_bdev_close(parent);
}

#endif
