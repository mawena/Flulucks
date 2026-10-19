#define _GNU_SOURCE
#include "fsck_util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef __ANDROID__
#include <spawn.h>
#endif

#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/luks.h"

extern char **environ;

#ifndef __ANDROID__
static int dans_path(const char *outil) {
  const char *p = getenv("PATH");
  if (!p) p = "/usr/bin:/bin";
  char cand[4096];
  while (*p) {
    const char *fin = strchr(p, ':');
    size_t n = fin ? (size_t)(fin - p) : strlen(p);
    if (n > 0 && n + strlen(outil) + 2 < sizeof cand) {
      memcpy(cand, p, n);
      snprintf(cand + n, sizeof cand - n, "/%s", outil);
      if (access(cand, X_OK) == 0) return 1;
    }
    if (!fin) break;
    p = fin + 1;
  }
  return 0;
}

#endif

int fu_tools_available(void) {
#ifdef __ANDROID__
  return 0;
#else
  return dans_path("e2fsck") && dans_path("debugfs") && dans_path("dumpe2fs");
#endif
}

int fu_run(char *const argv[], char *out, size_t cap) { return fu_run_timeout(argv, out, cap, 900); }

int fu_run_timeout(char *const argv[], char *out, size_t cap, unsigned seconds) {
  if (out && cap) out[0] = 0;
#ifdef __ANDROID__
  (void)argv;
  (void)seconds;
  return -1;
#else
  int pfd[2];
  if (pipe2(pfd, O_CLOEXEC) != 0) return -1;
  posix_spawn_file_actions_t fa;
  if (posix_spawn_file_actions_init(&fa) != 0) {
    close(pfd[0]);
    close(pfd[1]);
    return -1;
  }
  int fa_rc = posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
  fa_rc |= posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);
  fa_rc |= posix_spawn_file_actions_adddup2(&fa, pfd[1], 2);
  char pathenv[4096];
  const char *p = getenv("PATH");
  snprintf(pathenv, sizeof pathenv, "PATH=%s", p ? p : "/usr/bin:/bin");
  char *envp[] = {(char *)"LC_ALL=C", pathenv, NULL};
  pid_t pid = 0;
  int rc = fa_rc ? fa_rc : posix_spawnp(&pid, argv[0], &fa, NULL, argv, envp);
  posix_spawn_file_actions_destroy(&fa);
  close(pfd[1]);
  if (rc != 0) {
    close(pfd[0]);
    return -1;
  }
  struct timespec t0;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  size_t len = 0;
  char tmp[4096];
  int timeout = 0;
  for (;;) {
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long long ecoule = (long long)(t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    long long reste = (long long)seconds * 1000 - ecoule;
    if (reste <= 0) {
      timeout = 1;
      break;
    }
    struct pollfd pf = {pfd[0], POLLIN, 0};
    int pr = poll(&pf, 1, reste > 1000000 ? 1000000 : (int)reste);
    if (pr < 0 && errno == EINTR) continue;
    if (pr < 0) break;
    if (pr == 0) continue; /* le délai est revérifié en haut de boucle */
    ssize_t n = read(pfd[0], tmp, sizeof tmp);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    if (out && cap > 1 && len < cap - 1) {
      size_t take = (size_t)n < cap - 1 - len ? (size_t)n : cap - 1 - len;
      memcpy(out + len, tmp, take);
      len += take;
    }
  }
  close(pfd[0]);
  if (out && cap) out[len] = 0;
  if (timeout) kill(pid, SIGKILL);
  int st;
  while (waitpid(pid, &st, 0) < 0)
    if (errno != EINTR) return -1;
  if (timeout) return FU_RC_TIMEOUT;
  if (WIFEXITED(st)) return WEXITSTATUS(st);
  if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
  return -1;
#endif
}

int fu_tmpdir(char *out, size_t cap) {
  const char *fx = getenv("FL_FIXTURES");
  char d[4096];
  if (fx && *fx) {
    /* <build>/native/fixtures -> <build>/native/tmp */
    snprintf(d, sizeof d, "%s", fx);
    char *s = strrchr(d, '/');
    if (s) {
      snprintf(s, sizeof d - (size_t)(s - d), "/tmp");
    } else {
      snprintf(d, sizeof d, "tmp");
    }
  } else {
    const char *t = getenv("TMPDIR");
    snprintf(d, sizeof d, "%s", t && *t ? t : "/tmp");
  }
  if (mkdir(d, 0700) != 0 && errno != EEXIST) return -1;
  if (strlen(d) + 1 > cap) return -1;
  strcpy(out, d);
  return 0;
}

#define GRANDE_SORTIE ((size_t)1 << 20)

static void copier_tronque(char *dst, size_t cap, const char *src) {
  if (!dst || !cap) return;
  size_t n = strlen(src);
  if (n > cap - 1) n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = 0;
}

int fu_e2fsck_n(const char *img, char *out, size_t cap) {
  char *big = malloc(GRANDE_SORTIE);
  if (!big) return -1;
  char *const argv[] = {(char *)"e2fsck", (char *)"-fn", (char *)img, NULL};
  int rc = fu_run(argv, big, GRANDE_SORTIE);
  copier_tronque(out, cap, big);
  if (rc == 0) {
    int rejouer = strstr(big, "skipping journal recovery") != NULL;
    if (!rejouer) {
      char *const dargv[] = {(char *)"dumpe2fs", (char *)"-h", (char *)img, NULL};
      if (fu_run(dargv, big, GRANDE_SORTIE) == 0) {
        const char *f = strstr(big, "Filesystem features:");
        const char *fin = f ? strchr(f, '\n') : NULL;
        const char *nr = f ? strstr(f, "needs_recovery") : NULL;
        rejouer = nr && fin && nr < fin;
      }
    }
    if (rejouer) rc = FU_RC_JOURNAL;
  }
  free(big);
  return rc;
}

static int finit_par(const char *l, const char *suffixe) {
  size_t a = strlen(l), b = strlen(suffixe);
  return a >= b && !strcmp(l + a - b, suffixe);
}

int fu_fy_repairs_benign(const char *fy_out) {
  char *copie = strdup(fy_out);
  if (!copie) return 0;
  char prev[1024] = "";
  int ok = 1;
  char *save = NULL;
  for (char *l = strtok_r(copie, "\n", &save); l && ok; l = strtok_r(NULL, "\n", &save)) {
    size_t n = strlen(l);
    while (n && (l[n - 1] == ' ' || l[n - 1] == '\r')) l[--n] = 0;
    if (!n) continue;
    if (finit_par(l, "FIXED.") || strstr(l, "orphaned inode")) {
      ok = 0;
      break;
    }
    if (finit_par(l, "? yes")) {
      const char *desc = l;
      const char *q = strchr(l, '?');
      int nue = q && !memchr(l, ' ', (size_t)(q - l)); /* « Fix? yes » : la description est la ligne d'avant */
      if (nue) desc = prev;
      if (strncmp(desc, "Free blocks count wrong (", 25) && strncmp(desc, "Free inodes count wrong (", 25)) ok = 0;
    }
    snprintf(prev, sizeof prev, "%s", l);
  }
  free(copie);
  return ok;
}

int fu_e2fsck_recover(const char *img, int *replayed, int *fy_rc, char *fy_out, size_t fy_cap, char *out,
                      size_t cap) {
  char *big = malloc(GRANDE_SORTIE);
  if (!big) return -1;
  char *const a1[] = {(char *)"e2fsck", (char *)"-fy", (char *)img, NULL};
  int rc1 = fu_run(a1, big, GRANDE_SORTIE);
  if (fy_rc) *fy_rc = rc1;
  if (replayed) *replayed = strstr(big, "recovering journal") != NULL;
  copier_tronque(fy_out, fy_cap, big);
  int benin = rc1 >= 0 && rc1 < 4 && fu_fy_repairs_benign(big);
  free(big);
  int fn = fu_e2fsck_n(img, out, cap);
  if (fn != 0) return fn;
  return benin ? 0 : FU_RC_REPAIRED;
}

/* Chemin sûr pour une commande debugfs : pas de guillemet ni de saut de ligne. */
static int chemin_ok(const char *p) {
  return p && p[0] == '/' && !strpbrk(p, "\"\n\r");
}

int64_t fu_debugfs_cat(const char *img, const char *path, void *buf, size_t cap) {
  if (!chemin_ok(path)) return -1;
  char td[4096], dir[4200], file[4300], cmd[8192], out[4096];
  if (fu_tmpdir(td, sizeof td) != 0) return -1;
  snprintf(dir, sizeof dir, "%s/cat-XXXXXX", td);
  if (strchr(dir, '"') || !mkdtemp(dir)) return -1;
  snprintf(file, sizeof file, "%s/f", dir);
  snprintf(cmd, sizeof cmd, "dump \"%s\" \"%s\"", path, file);
  char *const argv[] = {(char *)"debugfs", (char *)"-R", cmd, (char *)img, NULL};
  fu_run(argv, out, sizeof out);
  int64_t res = -1;
  FILE *f = fopen(file, "rb");
  if (f) {
    struct stat st;
    if (fstat(fileno(f), &st) == 0 && (uint64_t)st.st_size <= cap) {
      size_t n = st.st_size ? fread(buf, 1, (size_t)st.st_size, f) : 0;
      if (n == (size_t)st.st_size) res = (int64_t)n;
    }
    fclose(f);
  }
  unlink(file);
  rmdir(dir);
  return res;
}

int fu_debugfs_inode_field(const char *img, const char *path, const char *field, uint64_t *out) {
  if (!chemin_ok(path)) return -1;
  char cmd[8192], txt[8192];
  snprintf(cmd, sizeof cmd, "stat \"%s\"", path);
  char *const argv[] = {(char *)"debugfs", (char *)"-R", cmd, (char *)img, NULL};
  if (fu_run(argv, txt, sizeof txt) != 0) return -1;
  if (strstr(txt, "File not found")) return -1;
  const char *p = NULL;
  int base = 10;
  if (!strcmp(field, "uid")) p = strstr(txt, "User:") ? strstr(txt, "User:") + 5 : NULL;
  else if (!strcmp(field, "gid")) p = strstr(txt, "Group:") ? strstr(txt, "Group:") + 6 : NULL;
  else if (!strcmp(field, "size")) p = strstr(txt, "Size:") ? strstr(txt, "Size:") + 5 : NULL;
  else if (!strcmp(field, "mode")) {
    p = strstr(txt, "Mode:") ? strstr(txt, "Mode:") + 5 : NULL;
    base = 8;
  } else if (!strcmp(field, "mtime") || !strcmp(field, "ctime") || !strcmp(field, "atime") ||
             !strcmp(field, "crtime")) {
    /* « mtime: 0x<secondes sur 32 bits>:<extra> -- date » ; les 2 bits bas de l'extra prolongent l'époque
     * (secondes = int32 signé + (extra & 3) << 32). Sans extra (inode de 128 octets) : 32 bits seuls. */
    char key[16];
    snprintf(key, sizeof key, "%s: 0x", field);
    const char *k = strstr(txt, key);
    if (!k) return -1;
    char *e1;
    errno = 0;
    unsigned long long lo = strtoull(k + strlen(key), &e1, 16);
    if (e1 == k + strlen(key) || errno) return -1;
    unsigned long long extra = 0;
    if (*e1 == ':') extra = strtoull(e1 + 1, NULL, 16);
    int64_t sec = (int64_t)(int32_t)(uint32_t)lo + (int64_t)((extra & 3) << 32);
    if (sec < 0) return -1;
    *out = (uint64_t)sec;
    return 0;
  }
  if (!p) return -1;
  char *end;
  errno = 0;
  unsigned long long v = strtoull(p, &end, base);
  if (end == p || errno) return -1;
  *out = v;
  return 0;
}

/* Copie [0, taille) de src vers dst en sautant trous et blocs nuls ; dst est ensuite étendu à `taille`. */
static int copie_creuse_fd(int in, int outfd, off_t taille) {
  static char buf[1 << 20];
  off_t pos = 0;
  while (pos < taille) {
    off_t data = lseek(in, pos, SEEK_DATA);
    if (data < 0) {
      if (errno == ENXIO) break; /* plus que des trous */
      data = pos;                /* SEEK_DATA non géré : lecture complète */
    }
    off_t trou = lseek(in, data, SEEK_HOLE);
    if (trou < 0 || trou > taille) trou = taille;
    off_t o = data;
    while (o < trou) {
      size_t want = (size_t)(trou - o) < sizeof buf ? (size_t)(trou - o) : sizeof buf;
      ssize_t n = pread(in, buf, want, o);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return -1;
      int nul = 1;
      for (ssize_t i = 0; i < n && nul; i += 8) {
        size_t k = (size_t)(n - i) < 8 ? (size_t)(n - i) : 8;
        static const char z[8];
        if (memcmp(buf + i, z, k) != 0) nul = 0;
      }
      if (!nul) {
        ssize_t w = 0;
        while (w < n) {
          ssize_t r = pwrite(outfd, buf + w, (size_t)(n - w), o + w);
          if (r < 0 && errno == EINTR) continue;
          if (r <= 0) return -1;
          w += r;
        }
      }
      o += n;
    }
    pos = trou;
  }
  return ftruncate(outfd, taille);
}

static int nouvelle_copie(char *out, size_t cap, int *fd) {
  char td[4096], nom[4200];
  if (fu_tmpdir(td, sizeof td) != 0) return -1;
  snprintf(nom, sizeof nom, "%s/copie-XXXXXX.img", td);
  *fd = mkstemps(nom, 4);
  if (*fd < 0) return -1;
  if (strlen(nom) + 1 > cap) {
    close(*fd);
    unlink(nom);
    return -1;
  }
  strcpy(out, nom);
  return 0;
}

int fu_copy_image(const char *src, char *out, size_t cap) {
  int in = open(src, O_RDONLY | O_CLOEXEC), fd;
  if (in < 0) return -1;
  struct stat st;
  if (fstat(in, &st) != 0 || nouvelle_copie(out, cap, &fd) != 0) {
    close(in);
    return -1;
  }
  int rc = copie_creuse_fd(in, fd, st.st_size);
  close(in);
  if (close(fd) != 0) rc = -1;
  if (rc != 0) unlink(out);
  return rc;
}

int fu_extract_luks_ext4(const char *luks_img, const char *pass_file, char *out, size_t cap) {
  char pass[256];
  FILE *pf = fopen(pass_file, "rb");
  if (!pf) return -1;
  size_t pn = fread(pass, 1, sizeof pass, pf);
  fclose(pf);
  fl_bdev *d = NULL, *dec = NULL;
  fl_luks *l = NULL;
  int fd = -1, rc = -1;
  if (fl_file_bdev_open(luks_img, 512, true, &d) != FL_OK) return -1;
  if (fl_luks_open(d, &l) != FL_OK || fl_luks_unlock(l, pass, pn, &dec) != FL_OK) goto fin;
  if (nouvelle_copie(out, cap, &fd) != 0) goto fin;
  {
    enum { CH = 2048 }; /* secteurs par lecture */
    uint32_t ss = dec->sector_size;
    uint8_t *buf = malloc((size_t)CH * ss);
    if (!buf) goto fin2;
    uint64_t n = dec->sector_count, lba = 0;
    rc = 0;
    while (lba < n && rc == 0) {
      uint32_t c = n - lba < CH ? (uint32_t)(n - lba) : CH;
      if (fl_bdev_read(dec, lba, c, buf) != FL_OK) {
        rc = -1;
        break;
      }
      size_t len = (size_t)c * ss;
      int nul = 1;
      for (size_t i = 0; i < len && nul; i++)
        if (buf[i]) nul = 0;
      if (!nul) {
        size_t w = 0;
        while (w < len) {
          ssize_t r = pwrite(fd, buf + w, len - w, (off_t)(lba * ss + w));
          if (r < 0 && errno == EINTR) continue;
          if (r <= 0) {
            rc = -1;
            break;
          }
          w += (size_t)r;
        }
      }
      lba += c;
    }
    free(buf);
    if (rc == 0 && ftruncate(fd, (off_t)(n * ss)) != 0) rc = -1;
  }
fin2:
  if (close(fd) != 0) rc = -1;
  fd = -1;
  if (rc != 0) unlink(out);
fin:
  if (dec) fl_bdev_close(dec);
  fl_luks_close(l);
  fl_bdev_close(d);
  memset(pass, 0, sizeof pass);
  return rc;
}

/* --- FAT / exFAT (phase 4) ------------------------------------------------------------------- */

int fu_fat_tools_available(void) {
#ifdef __ANDROID__
  return 0;
#else
  return dans_path("mkfs.vfat") && dans_path("fsck.fat") && dans_path("mkfs.exfat") && dans_path("fsck.exfat") &&
         dans_path("mcopy") && dans_path("python3");
#endif
}

/* 1 si l'image commence par un secteur de démarrage exFAT. */
static int est_exfat(const char *img) {
  char b[11] = {0};
  int fd = open(img, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return 0;
  ssize_t r = pread(fd, b, sizeof b, 0);
  close(fd);
  return r == (ssize_t)sizeof b && !memcmp(b + 3, "EXFAT   ", 8);
}

/* Lignes attendues d'un fsck propre : en-tête de version et ligne de bilan. Toute autre ligne compte comme
 * une erreur, même si le code de sortie vaut 0. */
static int ligne_propre(const char *l, size_t n) {
  if (n == 0) return 1;
  if (!strncmp(l, "fsck.fat ", 9) || !strncmp(l, "exfatprogs version", 18)) return 1;
  for (size_t i = 0; i + 1 < n; i++) {
    if (l[i] != ':' || l[i + 1] != ' ') continue;
    const char *r = l + i + 2;
    size_t k = n - i - 2;
    if (k >= 6 && !strncmp(r, "clean.", 6)) return 1;                      /* fsck.exfat */
    const char *f = memmem(r, k, " files, ", 8), *c = memmem(r, k, " clusters", 9);
    if (f && c && c > f) return 1;                                            /* fsck.fat */
  }
  return 0;
}

int fu_fsck_fat_n(const char *img, char *out, size_t cap) {
  char *big = malloc(GRANDE_SORTIE);
  if (!big) return -1;
  int ex = est_exfat(img);
  char *const argv[] = {(char *)(ex ? "fsck.exfat" : "fsck.fat"), (char *)"-n", (char *)img, NULL};
  int rc = fu_run(argv, big, GRANDE_SORTIE);
  if (rc == 0) {
    for (const char *l = big; *l;) {
      const char *e = strchr(l, '\n');
      size_t n = e ? (size_t)(e - l) : strlen(l);
      if (!ligne_propre(l, n)) {
        rc = 1;
        break;
      }
      l += n + (e ? 1 : 0);
    }
  }
  copier_tronque(out, cap, big);
  free(big);
  return rc;
}

int fu_fsck_fat_repair(const char *img, char *out, size_t cap) {
  char *big = malloc(GRANDE_SORTIE);
  if (!big) return -1;
  int ex = est_exfat(img);
  char *const argv[] = {(char *)(ex ? "fsck.exfat" : "fsck.fat"), (char *)(ex ? "-y" : "-a"), (char *)img, NULL};
  int rc = fu_run(argv, big, GRANDE_SORTIE);
  free(big);
  if (rc < 0 || rc >= 128) return rc < 0 ? -1 : rc;
  return fu_fsck_fat_n(img, out, cap);
}

int fu_fatjuge(char *out, size_t cap, const char *cmd, const char *img, const char *arg) {
  char *const argv[] = {(char *)"python3", (char *)"-I", (char *)FL_TOOLS_DIR "/fixtures/fatjuge.py", (char *)cmd,
                        (char *)img, (char *)arg, NULL};
  return fu_run(argv, out, cap);
}

int fu_fat_manifest(const char *img, const char *out_path) {
  char out[4096];
  int rc = fu_fatjuge(out, sizeof out, "manifest", img, out_path);
  if (rc != 0) fprintf(stderr, "fatjuge manifest %s : %d\n%s", img, rc, out);
  return rc == 0 || rc == 3 ? rc : -1;
}

int64_t fu_fat_free(const char *img) {
  char out[256];
  if (fu_fatjuge(out, sizeof out, "free", img, NULL) != 0) return -1;
  char *fin;
  long long v = strtoll(out, &fin, 10);
  return fin != out && v >= 0 ? v : -1;
}
