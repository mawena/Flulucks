/* lwext4 write-path spike harness (throwaway).
 * usage: harness IMG SHADOWDIR SCEN[,SCEN...] [opts]
 *   opts: nojournal  wb  fail=N  noumount  sector=N  norecover  ro
 * SCEN: s1..s11, all (s1..s11 in one session), mountonly
 */
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include "ext4.h"
#include "ext4_blockdev.h"
#include "ext4_fs.h"
#include "ext4_super.h"
#include "ext4_types.h"

#define MP "/mp/"

static int g_fd = -1;
static long g_writes, g_fail_after = -1, g_failed_writes;
static long g_reads;
static uint32_t g_ss = 512;
static const char *g_shadow;
static int g_errs;

static int bd_open(struct ext4_blockdev *bd) { (void)bd; return EOK; }
static int bd_close(struct ext4_blockdev *bd) { (void)bd; return EOK; }
static int bd_read(struct ext4_blockdev *bd, void *buf, uint64_t blk, uint32_t cnt) {
  (void)bd;
  g_reads++;
  size_t n = (size_t)cnt * g_ss;
  ssize_t r = pread(g_fd, buf, n, (off_t)(blk * g_ss));
  return r == (ssize_t)n ? EOK : EIO;
}
static int bd_write(struct ext4_blockdev *bd, const void *buf, uint64_t blk, uint32_t cnt) {
  (void)bd;
  if (g_fail_after >= 0 && g_writes >= g_fail_after) { g_failed_writes++; return EIO; }
  g_writes++;
  if (getenv("TRACEW")) fprintf(stderr, "W %llu %u\n", (unsigned long long)blk, cnt);
  size_t n = (size_t)cnt * g_ss;
  ssize_t r = pwrite(g_fd, buf, n, (off_t)(blk * g_ss));
  return r == (ssize_t)n ? EOK : EIO;
}

static struct ext4_blockdev_iface g_if = {
    .open = bd_open, .bread = bd_read, .bwrite = bd_write, .close = bd_close};
static struct ext4_blockdev g_bd = {.bdif = &g_if};

#define CK(e) do { int _r = (e); if (_r != EOK) { g_errs++; \
  fprintf(stderr, "ERR %s:%d %s -> %d (%s)\n", __func__, __LINE__, #e, _r, strerror(_r)); } } while (0)

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

/* ---- deterministic data + shadow copy ---- */
static void fill(uint8_t *b, size_t n, uint64_t seed) {
  uint64_t x = seed * 0x9E3779B97F4A7C15ull + 1;
  for (size_t i = 0; i < n; i += 8) {
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    size_t k = n - i < 8 ? n - i : 8;
    memcpy(b + i, &x, k);
  }
}
static int shadow_fd(const char *p, int flags) {
  if (!g_shadow) return -1;
  char s[8192], *q;
  snprintf(s, sizeof s, "%s/", g_shadow);
  q = s + strlen(s);
  for (const char *c = p + strlen(MP); *c && q < s + sizeof s - 1; c++) *q++ = *c == '/' ? '#' : *c;
  *q = 0;
  return open(s, flags, 0644);
}
static void shadow_rm(const char *p) {
  int fd = shadow_fd(p, O_RDONLY);
  if (fd < 0) return;
  close(fd);
  char s[8192], *q;
  snprintf(s, sizeof s, "%s/", g_shadow);
  q = s + strlen(s);
  for (const char *c = p + strlen(MP); *c; c++) *q++ = *c == '/' ? '#' : *c;
  *q = 0;
  unlink(s);
}

/* write `len` bytes at `off` of file at path (opened by caller) mirrored to shadow */
static int wr(ext4_file *f, int sfd, uint64_t off, size_t len, uint64_t seed) {
  static uint8_t *buf; static size_t cap;
  if (len > cap) { buf = realloc(buf, len); cap = len; }
  fill(buf, len, seed);
  int r = ext4_fseek(f, (int64_t)off, SEEK_SET);
  if (r != EOK) return r;
  size_t w = 0;
  r = ext4_fwrite(f, buf, len, &w);
  if (r != EOK || w != len) fprintf(stderr, "FWRITE off=%llu len=%zu -> r=%d wcnt=%zu\n", (unsigned long long)off, len, r, w);
  if (r == EOK && w != len) r = EIO;
  if (sfd >= 0) pwrite(sfd, buf, len, (off_t)off);
  return r;
}

static int mkfile(const char *p, size_t len, uint64_t seed) {
  ext4_file f;
  int r = ext4_fopen2(&f, p, O_CREAT | O_RDWR | O_TRUNC);
  if (r != EOK) return r;
  int sfd = shadow_fd(p, O_CREAT | O_RDWR | O_TRUNC);
  if (len) r = wr(&f, sfd, 0, len, seed);
  if (sfd >= 0) close(sfd);
  int r2 = ext4_fclose(&f);
  return r ? r : r2;
}

static int bigfile(const char *p, uint64_t mib, double *secs) {
  ext4_file f;
  double t0 = now();
  int r = ext4_fopen2(&f, p, O_CREAT | O_RDWR | O_TRUNC);
  if (r != EOK) return r;
  int sfd = shadow_fd(p, O_CREAT | O_RDWR | O_TRUNC);
  for (uint64_t i = 0; i < mib && r == EOK; i++) r = wr(&f, sfd, i << 20, 1 << 20, 1000 + i);
  if (sfd >= 0) close(sfd);
  int r2 = ext4_fclose(&f);
  if (secs) *secs = now() - t0;
  return r ? r : r2;
}

/* ---- scenarios ---- */
static void s1(void) { CK(mkfile(MP "s1.txt", 1024, 1)); }

static void s2(void) {
  char p[256], d[64];
  const char *dn = getenv("S2DIR") ? getenv("S2DIR") : "s2";
  snprintf(d, sizeof d, MP "%s", dn);
  CK(ext4_dir_mk(d));
  for (int i = 0; i < 200; i++) { snprintf(p, sizeof p, MP "%s/f%03d.dat", dn, i); CK(mkfile(p, 100 + i * 15, 200 + i)); }
}

static void s3(void) {
  char p[256];
  CK(ext4_dir_mk(MP "s3"));
  for (int i = 0; i < 3000; i++) {
    snprintf(p, sizeof p, MP "s3/entry_with_a_longish_name_%05d.txt", i);
    int r = mkfile(p, (i % 7) * 37, 5000 + i);
    if (r != EOK) { CK(r); if (g_fail_after >= 0) return; }
  }
}

static double g_s4_secs;
static void s4(void) {
  int r = bigfile(MP "big4.bin", 300, &g_s4_secs);
  CK(r);
  fprintf(stderr, "INFO s4 write 300 MiB: %.2f s (%.1f MB/s, before umount flush)\n", g_s4_secs, 300.0 / g_s4_secs);
}

static void s5(void) {
  /* overwrite inside pre-existing (mkfs -d) file and a new file; then write past EOF */
  const char *files[2] = {MP "gros.bin", MP "s5.bin"};
  CK(mkfile(MP "s5.bin", 4 << 20, 55));
  /* the shadow for gros.bin must start from its original content: make_tree pattern */
  {
    int sfd = shadow_fd(MP "gros.bin", O_CREAT | O_RDWR | O_TRUNC);
    if (sfd >= 0) { size_t n = 5 << 20; uint8_t *b = malloc(n); for (size_t i = 0; i < n; i++) b[i] = (uint8_t)((i * 31 + 7) & 0xFF); write(sfd, b, n); free(b); close(sfd); }
  }
  for (int k = 0; k < 2; k++) {
    ext4_file f;
    int r = ext4_fopen2(&f, files[k], O_RDWR);
    if (r != EOK) { CK(r); continue; }
    int sfd = shadow_fd(files[k], O_RDWR);
    uint64_t sz = ext4_fsize(&f);
    uint64_t x = 12345 + k;
    for (int i = 0; i < 64; i++) {
      x ^= x << 13; x ^= x >> 7; x ^= x << 17;
      uint64_t off = x % sz; size_t len = 1 + (x >> 40) % 20000;
      if (off + len > sz) len = sz - off;
      CK(wr(&f, sfd, off, len, 7000 + i + k * 100));
    }
    /* past EOF: hole of 3 MiB + unaligned */
    CK(wr(&f, sfd, sz + (3 << 20) + 123, 10000, 7999 + k));
    if (sfd >= 0) close(sfd);
    CK(ext4_fclose(&f));
  }
}

static void s6(void) {
  ext4_file f;
  /* A: 300 MiB -> 10 MiB (when combined, use 100 MiB to fit) */
  extern int g_combined;
  CK(bigfile(MP "s6a.bin", g_combined ? 100 : 300, NULL));
  CK(mkfile(MP "s6b.bin", 10 << 20, 61));
  CK(mkfile(MP "s6c.bin", 5 << 20, 62));
  int sfd;
  if (ext4_fopen2(&f, MP "s6a.bin", O_RDWR) == EOK) {
    CK(ext4_ftruncate(&f, 10 << 20));
    fprintf(stderr, "INFO s6a size after truncate down: %llu\n", (unsigned long long)ext4_fsize(&f));
    CK(ext4_fclose(&f));
    if ((sfd = shadow_fd(MP "s6a.bin", O_RDWR)) >= 0) { ftruncate(sfd, 10 << 20); close(sfd); }
  }
  if (ext4_fopen2(&f, MP "s6b.bin", O_RDWR) == EOK) {
    CK(ext4_ftruncate(&f, 64 << 20));
    fprintf(stderr, "INFO s6b size after truncate UP to 64MiB: %llu\n", (unsigned long long)ext4_fsize(&f));
    uint64_t got = ext4_fsize(&f);
    CK(ext4_fclose(&f));
    if ((sfd = shadow_fd(MP "s6b.bin", O_RDWR)) >= 0) { ftruncate(sfd, (off_t)got); close(sfd); }
  }
  if (ext4_fopen2(&f, MP "s6c.bin", O_RDWR) == EOK) {
    CK(ext4_ftruncate(&f, 0));
    CK(ext4_fclose(&f));
    if ((sfd = shadow_fd(MP "s6c.bin", O_RDWR)) >= 0) { ftruncate(sfd, 0); close(sfd); }
  }
  /* truncate to unaligned size inside a block */
  if (ext4_fopen2(&f, MP "s6b.bin", O_RDWR) == EOK) {
    CK(ext4_ftruncate(&f, (3 << 20) + 777));
    CK(ext4_fclose(&f));
    if ((sfd = shadow_fd(MP "s6b.bin", O_RDWR)) >= 0) { ftruncate(sfd, (3 << 20) + 777); close(sfd); }
  }
}

static void s7(void) {
  CK(ext4_dir_mk(MP "s7"));
  CK(ext4_dir_mk(MP "s7/sub"));
  CK(ext4_dir_mk(MP "s7/movedir"));
  CK(mkfile(MP "s7/movedir/inner.txt", 500, 71));
  CK(mkfile(MP "s7/a", 300, 72));
  CK(mkfile(MP "s7/b", 400, 73));
  CK(mkfile(MP "s7/c", 500, 74));
  CK(ext4_frename(MP "s7/a", MP "s7/a2"));             /* within dir */
  CK(ext4_frename(MP "s7/a2", MP "s7/sub/a3"));        /* across dirs */
  int r = ext4_frename(MP "s7/b", MP "s7/c");          /* onto existing */
  fprintf(stderr, "INFO s7 rename onto existing -> %d (%s)\n", r, strerror(r));
  if (r == EEXIST) {                                    /* emulate replace */
    CK(ext4_fremove(MP "s7/c"));
    CK(ext4_frename(MP "s7/b", MP "s7/c"));
  } else CK(r);
  CK(ext4_frename(MP "s7/movedir", MP "s7/sub/movedir")); /* dir across dirs ('..' update) */
  CK(ext4_frename(MP "hello.txt", MP "sous-dossier/hello.txt")); /* pre-existing (mkfs) entry */
  /* shadow bookkeeping */
  if (g_shadow) {
    char a[8192], b[8192];
#define SH(x) (snprintf(a, sizeof a, "%s/%s", g_shadow, x), a)
    snprintf(b, sizeof b, "%s/s7#sub#a3", g_shadow); rename(SH("s7#a"), b);
    snprintf(b, sizeof b, "%s/s7#c", g_shadow); rename(SH("s7#b"), b);
    snprintf(b, sizeof b, "%s/s7#sub#movedir#inner.txt", g_shadow); rename(SH("s7#movedir#inner.txt"), b);
  }
}

static void s8(void) {
  char p[256];
  /* delete small files */
  CK(ext4_dir_mk(MP "s8"));
  for (int i = 0; i < 50; i++) { snprintf(p, sizeof p, MP "s8/f%02d", i); CK(mkfile(p, 2000, 800 + i)); }
  for (int i = 0; i < 50; i += 2) { snprintf(p, sizeof p, MP "s8/f%02d", i); CK(ext4_fremove(p)); shadow_rm(p); }
  /* 3000-file dir: create (if not there), empty it, rmdir */
  if (ext4_inode_exist(MP "s3", EXT4_DE_DIR) != EOK) {
    CK(ext4_dir_mk(MP "s3"));
    for (int i = 0; i < 3000; i++) { snprintf(p, sizeof p, MP "s3/entry_with_a_longish_name_%05d.txt", i); CK(mkfile(p, (i % 7) * 37, 5000 + i)); }
  }
  for (int i = 0; i < 3000; i++) { snprintf(p, sizeof p, MP "s3/entry_with_a_longish_name_%05d.txt", i); CK(ext4_fremove(p)); shadow_rm(p); }
  CK(ext4_dir_rm(MP "s3"));
  /* big file */
  if (ext4_inode_exist(MP "big4.bin", EXT4_DE_REG_FILE) != EOK) CK(bigfile(MP "big4.bin", 300, NULL));
  CK(ext4_fremove(MP "big4.bin")); shadow_rm(MP "big4.bin");
  /* pre-existing (mkfs) content: a file, and a 300-file dir removed recursively */
  CK(ext4_fremove(MP "vide.txt"));
  CK(ext4_dir_rm(MP "beaucoup"));
}

static void s9(void) {
  char p[4096] = MP "s9";
  CK(ext4_dir_mk(p));
  for (int i = 0; i < 40; i++) { strcat(p, "/d"); CK(ext4_dir_mk(p)); }
  strcat(p, "/leaf.txt");
  CK(mkfile(p, 4097, 91));
}

static void s10(void) {
  char p[1024], name[300];
  CK(ext4_dir_mk(MP "s10"));
  CK(mkfile(MP "s10/caf\xc3\xa9_\xc3\xa0\xc3\xa7\xc3\xa8\xc3\xaa.txt", 100, 101));        /* café_àçèê */
  CK(mkfile(MP "s10/emoji_\xf0\x9f\x98\x80\xf0\x9f\x8e\x89.txt", 200, 102));               /* 😀🎉 */
  memset(name, 'x', 255); name[255] = 0;
  snprintf(p, sizeof p, MP "s10/%s", name); CK(mkfile(p, 300, 103));                       /* 255 ASCII */
  name[0] = 0; for (int i = 0; i < 127; i++) strcat(name, "\xc3\xa9"); strcat(name, "a");  /* 255 bytes UTF-8 */
  snprintf(p, sizeof p, MP "s10/%s", name); CK(mkfile(p, 400, 104));
  memset(name, 'y', 256); name[256] = 0;
  snprintf(p, sizeof p, MP "s10/%s", name);
  int r = mkfile(p, 1, 105);
  fprintf(stderr, "INFO s10 256-byte name -> %d (%s)\n", r, strerror(r));
  if (r == EOK) fprintf(stderr, "INFO s10 WARNING 256-byte name accepted\n");
  else shadow_rm(p);
  CK(ext4_dir_mk(MP "s10/dossier \xe2\x98\x80 \xe6\x97\xa5\xe6\x9c\xac"));                  /* ☀ 日本 */
  CK(mkfile(MP "s10/dossier \xe2\x98\x80 \xe6\x97\xa5\xe6\x9c\xac/f.txt", 50, 106));
}

static void s11(void) {
  CK(ext4_dir_mk(MP "s11"));
  CK(mkfile(MP "s11/file644", 123, 111));
  CK(ext4_owner_set(MP "s11/file644", 1000, 1000));
  CK(ext4_mode_set(MP "s11/file644", 0644));
  CK(ext4_mtime_set(MP "s11/file644", 1700000000));
  CK(ext4_atime_set(MP "s11/file644", 1700000001));
  CK(ext4_ctime_set(MP "s11/file644", 1700000002));
  CK(mkfile(MP "s11/exec755", 10, 112));
  CK(ext4_owner_set(MP "s11/exec755", 1000, 1000));
  CK(ext4_mode_set(MP "s11/exec755", 0755));
  CK(ext4_dir_mk(MP "s11/dir755"));
  CK(ext4_owner_set(MP "s11/dir755", 1000, 1000));
  CK(ext4_mode_set(MP "s11/dir755", 0755));
  CK(ext4_dir_mk(MP "s11/sgid"));
  CK(ext4_owner_set(MP "s11/sgid", 1000, 1234));
  CK(ext4_mode_set(MP "s11/sgid", 02775));
  CK(mkfile(MP "s11/sgid/child", 10, 113));
  CK(ext4_dir_mk(MP "s11/sgid/childdir"));
  uint32_t m = 0, u = 0, g = 0;
  CK(ext4_mode_get(MP "s11/sgid", &m));
  CK(ext4_owner_get(MP "s11/sgid/child", &u, &g));
  fprintf(stderr, "INFO s11 sgid mode=%o child uid=%u gid=%u\n", m, u, g);
  CK(ext4_mode_get(MP "s11/file644", &m));
  fprintf(stderr, "INFO s11 file644 mode=%o\n", m);
}

int g_combined;
typedef void (*sfn)(void);
static struct { const char *n; sfn f; } SC[] = {
    {"s1", s1}, {"s2", s2}, {"s3", s3}, {"s4", s4}, {"s5", s5}, {"s6", s6},
    {"s7", s7}, {"s8", s8}, {"s9", s9}, {"s10", s10}, {"s11", s11}};

static void dump_sb(const char *tag) {
  struct ext4_sblock *sb;
  if (ext4_get_sblock(MP, &sb) != EOK) return;
  fprintf(stderr, "SB[%s] state=%u feat_incompat=0x%x feat_ro=0x%x mnt_count=%u kbytes_written=%llu wtime=%u mtime=%u free_blocks=%u free_inodes=%u\n",
          tag, ext4_get16(sb, state), ext4_get32(sb, features_incompatible), ext4_get32(sb, features_read_only),
          ext4_get16(sb, mount_count), (unsigned long long)0, ext4_get32(sb, write_time),
          ext4_get32(sb, mount_time), ext4_get32(sb, free_blocks_count_lo), ext4_get32(sb, free_inodes_count));
}

int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "usage\n"); return 2; }
  bool journal = true, wb = false, umount = true, recover = true, ro = false;
  for (int i = 4; i < argc; i++) {
    if (!strcmp(argv[i], "nojournal")) journal = false;
    else if (!strcmp(argv[i], "wb")) wb = true;
    else if (!strcmp(argv[i], "noumount")) umount = false;
    else if (!strcmp(argv[i], "norecover")) recover = false;
    else if (!strcmp(argv[i], "ro")) ro = true;
    else if (!strncmp(argv[i], "fail=", 5)) g_fail_after = atol(argv[i] + 5);
    else if (!strncmp(argv[i], "sector=", 7)) g_ss = (uint32_t)atoi(argv[i] + 7);
    else { fprintf(stderr, "bad opt %s\n", argv[i]); return 2; }
  }
  if (strcmp(argv[2], "-")) { g_shadow = argv[2]; mkdir(g_shadow, 0755); }
  g_fd = open(argv[1], ro ? O_RDONLY : O_RDWR);
  if (g_fd < 0) { perror("open"); return 2; }
  struct stat st; fstat(g_fd, &st);
  g_if.ph_bsize = g_ss;
  g_if.ph_bcnt = (uint64_t)st.st_size / g_ss;
  g_if.ph_bbuf = calloc(1, g_ss);
  g_bd.part_size = (uint64_t)st.st_size;

  double t0 = now();
  int r = ext4_device_register(&g_bd, "spk");
  if (r) { fprintf(stderr, "register %d\n", r); return 3; }
  r = ext4_mount("spk", MP, ro);
  fprintf(stderr, "MOUNT -> %d (%s)\n", r, strerror(r));
  if (r) return 4;
  dump_sb("after-mount");
  if (recover) { r = ext4_recover(MP); fprintf(stderr, "RECOVER -> %d (%s)\n", r, strerror(r)); }
  if (journal) { r = ext4_journal_start(MP); fprintf(stderr, "JOURNAL_START -> %d (%s)\n", r, strerror(r)); }
  if (wb) CK(ext4_cache_write_back(MP, true));
  dump_sb("before-ops");

  char *list = strdup(argv[3]);
  for (char *tok = strtok(list, ","); tok; tok = strtok(NULL, ",")) {
    if (!strcmp(tok, "mountonly")) continue;
    if (!strcmp(tok, "all")) {
      g_combined = 1;
      for (size_t k = 0; k < sizeof SC / sizeof SC[0]; k++) { fprintf(stderr, "RUN %s\n", SC[k].n); SC[k].f(); }
      continue;
    }
    size_t k;
    for (k = 0; k < sizeof SC / sizeof SC[0]; k++) if (!strcmp(SC[k].n, tok)) break;
    if (k == sizeof SC / sizeof SC[0]) { fprintf(stderr, "unknown %s\n", tok); return 2; }
    fprintf(stderr, "RUN %s\n", tok);
    SC[k].f();
  }
  double t_ops = now() - t0;
  if (!umount) {
    fprintf(stderr, "CRASH: exiting without umount. writes_ok=%ld failed_writes=%ld errs=%d\n", g_writes, g_failed_writes, g_errs);
    _exit(0);
  }
  if (wb) CK(ext4_cache_write_back(MP, false));
  if (journal) CK(ext4_journal_stop(MP));
  dump_sb("before-umount");
  CK(ext4_umount(MP));
  double t_all = now() - t0;
  fprintf(stderr, "DONE errs=%d writes=%ld reads=%ld t_ops=%.2fs t_total=%.2fs\n", g_errs, g_writes, g_reads, t_ops, t_all);
  if (g_s4_secs > 0) fprintf(stderr, "TIMING s4 incl. flush+umount: %.1f MB/s\n", 300.0 / (t_all));
  return g_errs ? 1 : 0;
}
