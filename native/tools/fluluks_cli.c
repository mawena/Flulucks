#define _GNU_SOURCE
/* fluluks-cli : lecture seule d'une image ou d'un disque LUKS/ext4, sans root pour les images. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <termios.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "fluluks/blockdev.h"
#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fluluks/luks.h"
#include "fluluks/partition.h"
#include "fluluks/probe.h"
#include "fluluks/secure.h"
#include "fs/ext4_policy.h"

#define CLI_PASS_MAX 8192u
#define CHUNK (1u << 20)

static int fail_msg(const char *msg) {
  fprintf(stderr, "fluluks-cli: %s\n", msg);
  return 1;
}
static int fail_err(int err) { return fail_msg(fl_strerror(err)); }

static int usage(void) {
  fputs("usage:\n"
        "  fluluks-cli parts <image|périphérique>\n"
        "  fluluks-cli ls    <image|périphérique> <n°> <chemin> [--pass-file F]\n"
        "  fluluks-cli cat   <image|périphérique> <n°> <chemin> [--pass-file F]\n"
        "  fluluks-cli diagnostic <image|périphérique> <n°> [--pass-file F]\n"
        "      (lecture seule : contrôles d'écriture détaillés, puis parcours de toute l'arborescence)\n",
        stderr);
  return 2;
}

/* ---- phrase de passe ---- */

typedef struct {
  unsigned char *buf; /* fl_secure_alloc(CLI_PASS_MAX + 1) */
  size_t len;
} secret;

static void secret_wipe(secret *s) {
  if (s->buf) fl_secure_free(s->buf, CLI_PASS_MAX + 1);
  s->buf = NULL;
  s->len = 0;
}

static int read_pass_file(const char *path, secret *s) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return fail_msg("impossible d'ouvrir le fichier de phrase de passe");
  size_t n = 0;
  while (n < CLI_PASS_MAX + 1) {
    ssize_t r = read(fd, s->buf + n, CLI_PASS_MAX + 1 - n);
    if (r < 0 && errno == EINTR) continue;
    if (r < 0) {
      close(fd);
      return fail_msg("lecture du fichier de phrase de passe impossible");
    }
    if (r == 0) break;
    n += (size_t)r;
  }
  close(fd);
  if (n > CLI_PASS_MAX) return fail_msg("fichier de phrase de passe trop grand (8192 octets au plus)");
  s->len = n;
  return 0;
}

static volatile sig_atomic_t g_sigint;
static void on_sigint(int sig) { (void)sig; g_sigint = 1; }

static int read_pass_tty(secret *s) {
  int fd = open("/dev/tty", O_RDWR | O_NOCTTY);
  if (fd < 0) return fail_msg("pas de terminal : utilisez --pass-file");
  struct termios old, raw;
  if (tcgetattr(fd, &old) != 0) {
    close(fd);
    return fail_msg("pas de terminal : utilisez --pass-file");
  }
  raw = old;
  raw.c_lflag &= (tcflag_t)~(ECHO | ECHONL);
  struct sigaction sa, old_int, old_term, old_hup, old_quit;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_sigint; /* sans SA_RESTART : read() rend EINTR */
  sigemptyset(&sa.sa_mask);
  g_sigint = 0;
  sigaction(SIGINT, &sa, &old_int);
  sigaction(SIGTERM, &sa, &old_term);
  sigaction(SIGHUP, &sa, &old_hup);
  sigaction(SIGQUIT, &sa, &old_quit);
  if (tcsetattr(fd, TCSAFLUSH, &raw) != 0) {
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    sigaction(SIGHUP, &old_hup, NULL);
    sigaction(SIGQUIT, &old_quit, NULL);
    close(fd);
    return fail_msg("impossible de couper l'écho du terminal");
  }
  static const char prompt[] = "Phrase de passe : ";
  ssize_t w = write(fd, prompt, sizeof prompt - 1);
  (void)w;
  size_t n = 0;
  int rc = 0;
  char c = 0;
  for (;;) {
    ssize_t r = read(fd, &c, 1);
    if (r < 0 && errno == EINTR) { rc = -1; break; }
    if (r < 0) { rc = -2; break; }
    if (r == 0 || c == '\n') break;
    if (n >= CLI_PASS_MAX) { rc = -3; break; }
    s->buf[n++] = (unsigned char)c;
  }
  fl_memzero(&c, 1);
  tcsetattr(fd, TCSAFLUSH, &old);
  w = write(fd, "\n", 1);
  (void)w;
  sigaction(SIGINT, &old_int, NULL);
  sigaction(SIGTERM, &old_term, NULL);
  sigaction(SIGHUP, &old_hup, NULL);
  sigaction(SIGQUIT, &old_quit, NULL);
  close(fd);
  if (rc == -1 || g_sigint) return fail_msg("interrompu");
  if (rc == -2) return fail_msg("lecture du terminal impossible");
  if (rc == -3) return fail_msg("phrase de passe trop longue (8192 octets au plus)");
  s->len = n;
  return 0;
}

/* ---- ouverture d'une partition ---- */

typedef struct {
  fl_bdev *disk, *slice, *plain; /* plain : bdev déchiffré (ou NULL) */
  fl_luks *luks;
  fl_fs *fs;
} ctx;

static void ctx_close(ctx *c) {
  if (c->fs) fl_fs_unmount(c->fs);
  if (c->plain) fl_bdev_close(c->plain);
  if (c->luks) fl_luks_close(c->luks);
  if (c->slice) fl_bdev_close(c->slice);
  if (c->disk) fl_bdev_close(c->disk);
  memset(c, 0, sizeof *c);
}

static int open_disk_parts(const char *path, ctx *c, fl_part_table *t) {
  int e = fl_file_bdev_open(path, 512, true, &c->disk);
  if (e != FL_OK) return fail_err(e);
  e = fl_part_scan(c->disk, t);
  if (e != FL_OK) return fail_err(e);
  return 0;
}

static int cmd_parts(const char *path) {
  ctx c = {0};
  fl_part_table *t = calloc(1, sizeof *t);
  if (!t) return fail_err(FL_ENOMEM);
  int rc = open_disk_parts(path, &c, t);
  for (uint32_t i = 0; rc == 0 && i < t->count; i++) {
    const fl_partition *p = &t->parts[i];
    fl_bdev *sl = NULL;
    fl_probe_result pr;
    memset(&pr, 0, sizeof pr);
    char extra[96] = "";
    const char *cname = NULL;
    if (fl_slice_bdev_new(c.disk, p->first_lba, p->sector_count, &sl) == FL_OK) {
      if (fl_probe(sl, &pr) != FL_OK) memset(&pr, 0, sizeof pr);
      if (pr.content == FL_CONTENT_LUKS1 || pr.content == FL_CONTENT_LUKS2) {
        fl_luks *l = NULL;
        if (fl_luks_open(sl, &l) == FL_OK) {
          snprintf(extra, sizeof extra, "\tLUKS%d %s", fl_luks_version(l), fl_luks_cipher(l));
          fl_luks_close(l);
        }
      }
      if (pr.content == FL_CONTENT_UNKNOWN) {
        /* palliatif : en-tête LUKS2 principal abîmé mais secondaire valide */
        fl_luks *l = NULL;
        if (fl_luks_open(sl, &l) == FL_OK) {
          cname = "LUKS2 (en-tête principal abîmé)";
          pr.content = fl_luks_version(l) == 1 ? FL_CONTENT_LUKS1 : FL_CONTENT_LUKS2;
          snprintf(extra, sizeof extra, "\tLUKS%d %s", fl_luks_version(l), fl_luks_cipher(l));
          fl_luks_close(l);
        }
      }
      fl_bdev_close(sl);
    }
    unsigned long long mo = (unsigned long long)((p->sector_count / 1000000u) * c.disk->sector_size +
                         (p->sector_count % 1000000u) * c.disk->sector_size / 1000000u);
    printf("%u\t%llu\t%llu\t%s\t%s\t%s%s\n", (unsigned)p->index, (unsigned long long)p->first_lba, mo,
           cname ? cname : fl_content_name(pr.content), pr.label, fl_content_supported(pr.content) ? "oui" : "non", extra);
  }
  free(t);
  ctx_close(&c);
  return rc;
}

static int open_fs(const char *path, const char *idx_s, const char *passfile, ctx *c, bool explain) {
  char *end = NULL;
  errno = 0;
  unsigned long idx = strtoul(idx_s, &end, 10);
  if (errno || end == idx_s || *end || idx > UINT32_MAX) return 2;
  fl_part_table *t = calloc(1, sizeof *t);
  if (!t) return fail_err(FL_ENOMEM);
  int rc = open_disk_parts(path, c, t);
  if (rc) { free(t); return rc; }
  const fl_partition *p = NULL;
  for (uint32_t i = 0; i < t->count; i++)
    if (t->parts[i].index == idx) { p = &t->parts[i]; break; }
  if (!p) { free(t); return fail_msg("partition introuvable"); }
  int e = fl_slice_bdev_new(c->disk, p->first_lba, p->sector_count, &c->slice);
  free(t);
  if (e != FL_OK) return fail_err(e);

  fl_probe_result pr;
  if ((e = fl_probe(c->slice, &pr)) != FL_OK) return fail_err(e);
  fl_bdev *target = c->slice;
  bool is_luks = pr.content == FL_CONTENT_LUKS1 || pr.content == FL_CONTENT_LUKS2;
  if (is_luks) {
    if ((e = fl_luks_open(c->slice, &c->luks)) != FL_OK) return fail_err(e);
  } else if (pr.content == FL_CONTENT_UNKNOWN && fl_luks_open(c->slice, &c->luks) == FL_OK) {
    /* palliatif : magie de l'en-tête principal détruite, mais l'en-tête LUKS2 secondaire est valide */
    is_luks = true;
  }
  if (is_luks) {
    secret s = {fl_secure_alloc(CLI_PASS_MAX + 1), 0};
    if (!s.buf) return fail_err(FL_ENOMEM);
    rc = passfile ? read_pass_file(passfile, &s) : read_pass_tty(&s);
    if (rc) { secret_wipe(&s); return rc; }
    e = fl_luks_unlock(c->luks, s.buf, s.len, &c->plain);
    secret_wipe(&s);
    if (e != FL_OK) return fail_err(e);
    if ((e = fl_probe(c->plain, &pr)) != FL_OK) return fail_err(e);
    target = c->plain;
  }
  if (pr.content != FL_CONTENT_EXT4) return fail_err(FL_ENOTSUP);
  if (explain) {
    puts("== contrôles faits au montage en écriture (§ 4.2) ==");
    if ((e = fl_ext4_policy_explain(target, NULL, stdout)) != FL_OK) fprintf(stderr, "fluluks-cli: diagnostic : %s\n", fl_strerror(e));
    fflush(stdout);
  }
  if ((e = fl_fs_mount(target, FL_CONTENT_EXT4, false, &c->fs)) != FL_OK) return fail_err(e);
  fl_statfs sf;
  if (fl_fs_statfs(c->fs, &sf) == FL_OK && sf.needs_recovery)
    fputs("fluluks-cli: attention : le journal du volume n'a pas été rejoué (volume mal démonté ou encore "
          "ouvert ailleurs) ; le contenu peut être incomplet\n",
          stderr);
  return 0;
}

/* ---- ls ---- */

typedef struct {
  fl_dirent *v;
  size_t n, cap;
} dirlist;

static int collect(void *vctx, const fl_dirent *e) {
  dirlist *l = vctx;
  if (l->n == l->cap) {
    size_t nc = l->cap ? l->cap * 2 : 64;
    fl_dirent *nv = realloc(l->v, nc * sizeof *nv);
    if (!nv) return FL_ENOMEM;
    l->v = nv;
    l->cap = nc;
  }
  l->v[l->n++] = *e;
  return 0;
}

static int cmp_name(const void *a, const void *b) {
  return strcmp(((const fl_dirent *)a)->name, ((const fl_dirent *)b)->name);
}

static int cmd_ls(ctx *c, const char *path) {
  dirlist l = {0};
  int e = fl_fs_readdir(c->fs, path, collect, &l);
  if (e != FL_OK) { free(l.v); return fail_err(e); }
  qsort(l.v, l.n, sizeof *l.v, cmp_name);
  for (size_t i = 0; i < l.n; i++) {
    char t = l.v[i].st.type == FL_FT_DIR ? 'd' : l.v[i].st.type == FL_FT_FILE ? 'f'
             : l.v[i].st.type == FL_FT_SYMLINK ? 'l' : 'o';
    printf("%c\t%llu\t%s\n", t, (unsigned long long)l.v[i].st.size, l.v[i].name);
  }
  free(l.v);
  if (fflush(stdout) != 0) return 1;
  return 0;
}

/* ---- diagnostic : parcours de l'arborescence ---- */

typedef struct {
  unsigned long long dirs, files, others, errors;
} walk_stats; /* un dossier sous la borne de profondeur compte comme une erreur (non parcouru) */

static void walk_err(walk_stats *w, const char *what, const char *path, int e) {
  if (w->errors++ < 100) printf("  ERREUR %s %s : %s\n", what, path, fl_strerror(e));
}

/* Lit les inodes (stat), les blocs de dossier (readdir) et, pour chaque fichier, son premier et son
 * dernier octet (arbre d'extents) : chaque structure que lwext4 vérifie par somme est touchée. */
static void walk(ctx *c, char *path, size_t len, unsigned depth, walk_stats *w) {
  dirlist l = {0};
  int e = fl_fs_readdir(c->fs, len ? path : "/", collect, &l);
  if (e != FL_OK) walk_err(w, "dossier", len ? path : "/", e);
  w->dirs++;
  for (size_t i = 0; i < l.n; i++) {
    size_t nl = strlen(l.v[i].name);
    if (len + 1 + nl >= 4096) { walk_err(w, "chemin trop long sous", path, FL_EINVAL); continue; }
    path[len] = '/';
    memcpy(path + len + 1, l.v[i].name, nl + 1);
    fl_stat st;
    e = fl_fs_stat(c->fs, path, &st);
    if (e != FL_OK) walk_err(w, "inode", path, e);
    else if (st.type == FL_FT_DIR) {
      if (depth < 256) walk(c, path, len + 1 + nl, depth + 1, w);
      else walk_err(w, "dossier trop profond (non parcouru)", path, FL_EINVAL);
    } else if (st.type == FL_FT_FILE) {
      w->files++;
      fl_file *f = NULL;
      if ((e = fl_fs_open(c->fs, path, FL_O_RDONLY, &f)) != FL_OK) walk_err(w, "ouverture", path, e);
      else {
        unsigned char b;
        int64_t r = fl_file_pread(f, &b, 1, 0);
        if (r >= 0 && st.size > 1) r = fl_file_pread(f, &b, 1, st.size - 1);
        if (r < 0) walk_err(w, "lecture", path, (int)r);
        fl_file_close(f);
      }
    } else w->others++;
    path[len] = 0;
  }
  free(l.v);
}

static int cmd_diagnostic(ctx *c) {
  fl_statfs sf;
  if (fl_fs_statfs(c->fs, &sf) == FL_OK)
    printf("== montage en lecture seule : réussi%s ==\n", sf.needs_recovery ? " (journal à rejouer)" : "");
  puts("== parcours de l'arborescence (inodes, dossiers, extents) ==");
  fflush(stdout);
  char *path = calloc(1, 4096);
  if (!path) return fail_err(FL_ENOMEM);
  walk_stats w = {0};
  walk(c, path, 0, 0, &w);
  free(path);
  printf("dossiers %llu, fichiers %llu, autres %llu, erreurs %llu%s\n", w.dirs, w.files, w.others, w.errors,
         w.errors > 100 ? " (100 premières affichées)" : "");
  return fflush(stdout) == 0 && w.errors == 0 ? 0 : 1;
}

/* ---- cat ---- */

static int write_all(int fd, const unsigned char *p, size_t n) {
  while (n) {
    ssize_t w = write(fd, p, n);
    if (w < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    p += w;
    n -= (size_t)w;
  }
  return 0;
}

static int cmd_cat(ctx *c, const char *path) {
  fl_file *f = NULL;
  int e = fl_fs_open(c->fs, path, FL_O_RDONLY, &f);
  if (e != FL_OK) return fail_err(e);
  unsigned char *buf = malloc(CHUNK);
  if (!buf) { fl_file_close(f); return fail_err(FL_ENOMEM); }
  uint64_t off = 0;
  int rc = 0;
  for (;;) {
    int64_t r = fl_file_pread(f, buf, CHUNK, off);
    if (r < 0) { rc = fail_err((int)r); break; }
    if (r == 0) break;
    if (write_all(STDOUT_FILENO, buf, (size_t)r) != 0) {
      rc = (errno == EPIPE) ? 1 : fail_msg("écriture sur la sortie impossible");
      break;
    }
    off += (uint64_t)r;
  }
  free(buf);
  fl_file_close(f);
  return rc;
}

int main(int argc, char **argv) {
  struct rlimit nocore = {0, 0};
  setrlimit(RLIMIT_CORE, &nocore);
#ifdef __linux__
  prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
#endif
  signal(SIGPIPE, SIG_IGN);
  if (argc < 3) return usage();
  const char *cmd = argv[1];
  if (!strcmp(cmd, "parts")) return argc == 3 ? cmd_parts(argv[2]) : usage();
  if (!strcmp(cmd, "diagnostic")) {
    const char *pf = NULL;
    if (argc == 6 && !strcmp(argv[4], "--pass-file")) pf = argv[5];
    else if (argc != 4) return usage();
    ctx c = {0};
    int rc = open_fs(argv[2], argv[3], pf, &c, true);
    if (rc == 0) rc = cmd_diagnostic(&c);
    else if (rc == 2) usage();
    ctx_close(&c);
    return rc;
  }
  bool is_ls = !strcmp(cmd, "ls"), is_cat = !strcmp(cmd, "cat");
  if (!is_ls && !is_cat) return usage();
  const char *passfile = NULL;
  if (argc == 7 && !strcmp(argv[5], "--pass-file")) passfile = argv[6];
  else if (argc != 5) return usage();
  ctx c = {0};
  int rc = open_fs(argv[2], argv[3], passfile, &c, false);
  if (rc == 0) rc = is_ls ? cmd_ls(&c, argv[4]) : cmd_cat(&c, argv[4]);
  else if (rc == 2) usage();
  ctx_close(&c);
  return rc;
}
