#define _GNU_SOURCE
#include "tree_model.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "crypto/fl_crypto.h"
#include "fluluks/error.h"
#include "fsck_util.h"

typedef struct {
  char *path;
  int is_dir;
  uint64_t size;
  uint8_t sha[32];
  uint32_t uid, gid, mode; /* mode = st_mode complet */
} ent;

struct tm {
  ent *e;
  size_t n, cap;
};

static void ent_free(ent *x) { free(x->path); }

tm *tm_new(void) {
  tm *m = calloc(1, sizeof *m);
  if (!m) return NULL;
  m->cap = 16;
  m->e = calloc(m->cap, sizeof *m->e);
  if (!m->e) {
    free(m);
    return NULL;
  }
  m->e[0].path = strdup("/");
  m->e[0].is_dir = 1;
  m->e[0].mode = S_IFDIR | 0755;
  m->n = 1;
  return m;
}

static uint64_t debugfs_limit = TM_DEBUGFS_FULL_DEFAULT;
void tm_set_debugfs_full_limit(uint64_t limit) { debugfs_limit = limit; }

tm *tm_clone(const tm *m) {
  tm *c = tm_new();
  if (!c) return NULL;
  ent_free(&c->e[0]);
  c->n = 0;
  while (c->cap < m->n) {
    ent *ne = realloc(c->e, c->cap * 2 * sizeof *ne);
    if (!ne) {
      tm_free(c);
      return NULL;
    }
    c->e = ne;
    c->cap *= 2;
  }
  for (size_t i = 0; i < m->n; i++) {
    c->e[i] = m->e[i];
    c->e[i].path = strdup(m->e[i].path);
    c->n = i + 1;
    if (!c->e[i].path) {
      tm_free(c);
      return NULL;
    }
  }
  return c;
}

void tm_free(tm *m) {
  if (!m) return;
  for (size_t i = 0; i < m->n; i++) ent_free(&m->e[i]);
  free(m->e);
  free(m);
}

size_t tm_count(const tm *m) { return m->n; }

static int cmp_chemin(const void *a, const void *b) {
  return strcmp((*(const ent *const *)a)->path, (*(const ent *const *)b)->path);
}

int tm_write_manifest(const tm *m, const char *fichier) {
  const ent **t = malloc(m->n * sizeof *t);
  FILE *f = t ? fopen(fichier, "w") : NULL;
  if (!f) {
    free(t);
    return -1;
  }
  for (size_t i = 0; i < m->n; i++) t[i] = &m->e[i];
  qsort(t, m->n, sizeof *t, cmp_chemin);
  int rc = 0;
  for (size_t i = 0; i < m->n && rc == 0; i++) {
    const ent *x = t[i];
    char hex[65] = "-";
    if (!x->is_dir)
      for (int k = 0; k < 32; k++) snprintf(hex + 2 * k, 3, "%02x", x->sha[k]);
    if (fprintf(f, "%c\t%llu\t%s\t%u\t%u\t%o\t%s\n", x->is_dir ? 'd' : 'f', (unsigned long long)x->size, hex,
                (unsigned)x->uid, (unsigned)x->gid, (unsigned)x->mode, x->path) < 0)
      rc = -1;
  }
  if (fclose(f) != 0) rc = -1;
  free(t);
  return rc;
}

static ent *trouver(const tm *m, const char *path) {
  for (size_t i = 0; i < m->n; i++)
    if (!strcmp(m->e[i].path, path)) return &m->e[i];
  return NULL;
}

static char *parent_de(const char *path) {
  char *p = strdup(path);
  if (!p) return NULL;
  char *s = strrchr(p, '/');
  if (s == p) p[1] = 0;
  else *s = 0;
  return p;
}

static ent *ajouter(tm *m, const char *path) {
  if (m->n == m->cap) {
    ent *ne = realloc(m->e, m->cap * 2 * sizeof *ne);
    if (!ne) return NULL;
    m->e = ne;
    m->cap *= 2;
  }
  ent *x = &m->e[m->n];
  memset(x, 0, sizeof *x);
  x->path = strdup(path);
  if (!x->path) return NULL;
  m->n++;
  return x;
}

/* Condensat d'un contenu : sha256 de la suite des sha256 des tranches de TM_CHUNK octets. */
typedef struct {
  uint8_t *dig;
  size_t n, cap;
  uint8_t zero[32];
  int zero_ok;
} cdig;

static const uint8_t *zeros_chunk(void) {
  static uint8_t z[TM_CHUNK];
  return z;
}

static int cdig_add(cdig *c, const uint8_t *data, size_t len) {
  if (c->n == c->cap) {
    size_t nc = c->cap ? c->cap * 2 : 256;
    uint8_t *nd = realloc(c->dig, nc * 32);
    if (!nd) return -1;
    c->dig = nd;
    c->cap = nc;
  }
  uint8_t *d = c->dig + c->n * 32;
  if (len == TM_CHUNK && !memcmp(data, zeros_chunk(), TM_CHUNK)) {
    if (!c->zero_ok) {
      if (fl_hash("sha256", data, len, c->zero) != 0) return -1;
      c->zero_ok = 1;
    }
    memcpy(d, c->zero, 32);
  } else if (fl_hash("sha256", data, len, d) != 0) {
    return -1;
  }
  c->n++;
  return 0;
}

static int cdig_fin(cdig *c, uint8_t out[32]) {
  int rc = fl_hash("sha256", c->dig ? c->dig : (const uint8_t *)"", c->n * 32, out);
  free(c->dig);
  c->dig = NULL;
  return rc;
}

static int hash_contenu(const void *data, size_t len, uint8_t out[32]) {
  cdig c = {0};
  for (size_t o = 0; o < len; o += TM_CHUNK) {
    size_t k = len - o < TM_CHUNK ? len - o : TM_CHUNK;
    if (cdig_add(&c, (const uint8_t *)data + o, k) != 0) {
      free(c.dig);
      return -1;
    }
  }
  return cdig_fin(&c, out);
}

static int nom_ok(const char *path) {
  if (path[0] != '/' || path[1] == 0 || strstr(path, "//")) return 0;
  return path[strlen(path) - 1] != '/';
}

static int creer(tm *m, const char *path, int dir, uint32_t uid, uint32_t gid, uint32_t perm, ent **out) {
  if (!nom_ok(path) || trouver(m, path)) return -1;
  char *par = parent_de(path);
  ent *pe = par ? trouver(m, par) : NULL;
  free(par);
  if (!pe || !pe->is_dir) return -1;
  ent *x = ajouter(m, path);
  if (!x) return -1;
  x->is_dir = dir;
  x->uid = uid;
  x->gid = gid;
  x->mode = (dir ? S_IFDIR : S_IFREG) | (perm & 07777);
  *out = x;
  return 0;
}

int tm_apply_mkdir(tm *m, const char *path, uint32_t uid, uint32_t gid, uint32_t perm) {
  ent *x;
  return creer(m, path, 1, uid, gid, perm, &x);
}

int tm_apply_file(tm *m, const char *path, const void *data, size_t len, uint32_t uid, uint32_t gid,
                  uint32_t perm) {
  ent *x = trouver(m, path);
  if (x && x->is_dir) return -1;
  if (!x && creer(m, path, 0, uid, gid, perm, &x) != 0) return -1;
  x->size = len;
  return hash_contenu(data, len, x->sha) == 0 ? 0 : -1;
}

static size_t index_de(const tm *m, const ent *x) { return (size_t)(x - m->e); }

static void retirer(tm *m, size_t i) {
  ent_free(&m->e[i]);
  m->e[i] = m->e[m->n - 1];
  m->n--;
}

static int a_des_enfants(const tm *m, const char *dir) {
  size_t l = strlen(dir);
  for (size_t i = 0; i < m->n; i++)
    if (!strncmp(m->e[i].path, dir, l) && m->e[i].path[l] == '/') return 1;
  return 0;
}

int tm_apply_unlink(tm *m, const char *path) {
  ent *x = nom_ok(path) ? trouver(m, path) : NULL;
  if (!x || x->is_dir) return -1;
  retirer(m, index_de(m, x));
  return 0;
}

int tm_apply_rmdir(tm *m, const char *path) {
  ent *x = nom_ok(path) ? trouver(m, path) : NULL;
  if (!x || !x->is_dir || a_des_enfants(m, path)) return -1;
  retirer(m, index_de(m, x));
  return 0;
}

int tm_apply_rename(tm *m, const char *from, const char *to) {
  ent *x = nom_ok(from) && nom_ok(to) ? trouver(m, from) : NULL;
  if (!x) return -1;
  size_t lf = strlen(from);
  if (!strncmp(to, from, lf) && (to[lf] == '/' || to[lf] == 0)) return -1; /* dans soi-même */
  char *par = parent_de(to);
  ent *pe = par ? trouver(m, par) : NULL;
  free(par);
  if (!pe || !pe->is_dir) return -1;
  ent *cible = trouver(m, to);
  if (cible) {
    if (cible->is_dir != x->is_dir) return -1;
    if (cible->is_dir && a_des_enfants(m, to)) return -1;
    retirer(m, index_de(m, cible));
    x = trouver(m, from); /* retirer() peut avoir déplacé l'entrée */
    if (!x) return -1;
  }
  for (size_t i = 0; i < m->n; i++) {
    ent *y = &m->e[i];
    if (strncmp(y->path, from, lf) || (y->path[lf] != '/' && y->path[lf] != 0)) continue;
    char *np = malloc(strlen(to) + strlen(y->path + lf) + 1);
    if (!np) return -1;
    sprintf(np, "%s%s", to, y->path + lf);
    free(y->path);
    y->path = np;
  }
  return 0;
}

int tm_apply_attrs(tm *m, const char *path, uint32_t uid, uint32_t gid, uint32_t perm) {
  ent *x = trouver(m, path);
  if (!x) return -1;
  x->uid = uid;
  x->gid = gid;
  x->mode = (x->mode & ~07777u) | (perm & 07777);
  return 0;
}

/* --- construction depuis un dossier ------------------------------------------------------ */

/* Condensat d'un fichier du disque local, lu par tranches. */
static int hash_fichier(const char *chemin, uint64_t size, uint8_t sha[32]) {
  FILE *f = fopen(chemin, "rb");
  if (!f) return -1;
  uint8_t *b = malloc(TM_CHUNK);
  cdig c = {0};
  int rc = b ? 0 : -1;
  for (uint64_t o = 0; rc == 0 && o < size; o += TM_CHUNK) {
    size_t k = size - o < TM_CHUNK ? (size_t)(size - o) : TM_CHUNK;
    if (fread(b, 1, k, f) != k || cdig_add(&c, b, k) != 0) rc = -1;
  }
  fclose(f);
  free(b);
  if (rc != 0) {
    free(c.dig);
    return -1;
  }
  return cdig_fin(&c, sha);
}

static int parcourir(tm *m, const char *dirreel, const char *dirvirt) {
  DIR *d = opendir(dirreel);
  if (!d) return -1;
  struct dirent *de;
  int rc = 0;
  while (rc == 0 && (de = readdir(d))) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
    char reel[8192], virt[8192];
    snprintf(reel, sizeof reel, "%s/%s", dirreel, de->d_name);
    snprintf(virt, sizeof virt, "%s%s%s", dirvirt, dirvirt[1] ? "/" : "", de->d_name);
    struct stat st;
    if (lstat(reel, &st) != 0) {
      rc = -1;
      break;
    }
    if (S_ISDIR(st.st_mode)) {
      ent *x;
      if (creer(m, virt, 1, st.st_uid, st.st_gid, st.st_mode, &x) != 0) rc = -1;
      else rc = parcourir(m, reel, virt);
    } else if (S_ISREG(st.st_mode)) {
      ent *x;
      if (creer(m, virt, 0, st.st_uid, st.st_gid, st.st_mode, &x) != 0) {
        rc = -1;
        break;
      }
      x->size = (uint64_t)st.st_size;
      rc = hash_fichier(reel, x->size, x->sha);
    } /* autres types : ignorés, mkfs -d les crée mais le banc n'en utilise pas */
  }
  closedir(d);
  return rc;
}

int tm_from_dir(tm *m, const char *dir) { return parcourir(m, dir, "/"); }

/* --- comparaison --------------------------------------------------------------------------- */

static int cmp_ent(const void *a, const void *b) { return strcmp(((const ent *)a)->path, ((const ent *)b)->path); }

static ent *copie_triee(const tm *m) {
  ent *c = malloc(m->n * sizeof *c);
  if (!c) return NULL;
  memcpy(c, m->e, m->n * sizeof *c);
  qsort(c, m->n, sizeof *c, cmp_ent);
  return c;
}

static size_t nb_enfants(const tm *m, const char *dir) {
  size_t l = strlen(dir), k = 0;
  if (l == 1) l = 0; /* racine */
  for (size_t i = 0; i < m->n; i++) {
    const char *p = m->e[i].path;
    if (!strcmp(p, "/")) continue;
    if (strncmp(p, dir, l) || p[l] != '/') continue;
    if (!strchr(p + l + 1, '/')) k++;
  }
  return k;
}

static int tm_quiet;
void tm_set_quiet(int quiet) { tm_quiet = quiet; }

typedef struct {
  int n;
} rapport;

static void ecart(rapport *r, const char *fmt, const char *chemin, unsigned long long a, unsigned long long b) {
  if (r->n < 10 && !tm_quiet) {
    fprintf(stderr, "tree_model: %s : ", chemin);
    fprintf(stderr, fmt, a, b);
    fputc('\n', stderr);
  }
  r->n++;
}

typedef struct {
  size_t n;
  int lostfound;
} compte;

static int cb_compte(void *ctx, const fl_dirent *e) {
  compte *c = ctx;
  c->n++;
  if (!strcmp(e->name, "lost+found")) c->lostfound = 1;
  return 0;
}

int tm_check_fl(const tm *m, fl_fs *fs) {
  rapport r = {0};
  ent *t = copie_triee(m);
  if (!t) return -1;
  for (size_t i = 0; i < m->n; i++) {
    const ent *x = &t[i];
    fl_stat st;
    if (fl_fs_stat(fs, x->path, &st) != FL_OK) {
      ecart(&r, "absent du volume (%llu %llu)", x->path, 0, 0);
      continue;
    }
    if (st.mode != x->mode) ecart(&r, "mode %llo, attendu %llo", x->path, st.mode, x->mode);
    if (st.uid != x->uid) ecart(&r, "uid %llu, attendu %llu", x->path, st.uid, x->uid);
    if (st.gid != x->gid) ecart(&r, "gid %llu, attendu %llu", x->path, st.gid, x->gid);
    if (x->is_dir) {
      compte c = {0, 0};
      if (fl_fs_readdir(fs, x->path, cb_compte, &c) != FL_OK) {
        ecart(&r, "readdir impossible (%llu %llu)", x->path, 0, 0);
        continue;
      }
      size_t attendu = nb_enfants(m, x->path);
      size_t recu = c.n - (!strcmp(x->path, "/") && c.lostfound ? 1 : 0);
      if (recu != attendu) ecart(&r, "%llu entrées, attendu %llu", x->path, recu, attendu);
      continue;
    }
    if (st.size != x->size) {
      ecart(&r, "taille %llu, attendu %llu", x->path, st.size, x->size);
      continue;
    }
    fl_file *f = NULL;
    if (fl_fs_open(fs, x->path, FL_O_RDONLY, &f) != FL_OK) {
      ecart(&r, "ouverture impossible (%llu %llu)", x->path, 0, 0);
      continue;
    }
    uint8_t *b = malloc(TM_CHUNK);
    cdig c = {0};
    int ok = b != NULL;
    for (uint64_t o = 0; ok && o < x->size; o += TM_CHUNK) {
      size_t want = x->size - o < TM_CHUNK ? (size_t)(x->size - o) : TM_CHUNK, got = 0;
      while (got < want) { /* un pread court n'est pas une erreur : on continue à off + got */
        int64_t n = fl_file_pread(f, b + got, want - got, o + got);
        if (n <= 0) break;
        got += (size_t)n;
      }
      if (got != want || cdig_add(&c, b, want) != 0) ok = 0;
    }
    uint8_t sha[32];
    if (!ok) free(c.dig);
    if (!ok || cdig_fin(&c, sha) != 0) ecart(&r, "lecture impossible (%llu %llu)", x->path, 0, 0);
    else if (memcmp(sha, x->sha, 32) != 0) ecart(&r, "contenu différent (%llu %llu)", x->path, 0, 0);
    free(b);
    fl_file_close(f);
  }
  free(t);
  return r.n;
}

/* --- debugfs ------------------------------------------------------------------------------- */

int tm_check_debugfs(const tm *m, const char *img) {
  rapport r = {0};
  char td[4096], dir[4200], cmdf[4300], tmp[4300];
  if (fu_tmpdir(td, sizeof td) != 0) return -1;
  snprintf(dir, sizeof dir, "%s/tm-XXXXXX", td);
  if (!mkdtemp(dir)) return -1;
  snprintf(cmdf, sizeof cmdf, "%s/cmd", dir);
  ent *t = copie_triee(m);
  char *sortie = malloc((size_t)16 << 20);
  FILE *cf = fopen(cmdf, "wb");
  int res = -1;
  if (!t || !sortie || !cf) goto fin;
  /* Un seul appel debugfs : « ls -p » de chaque dossier, « dump » de chaque fichier de taille raisonnable. */
  for (size_t i = 0; i < m->n; i++) {
    if (strpbrk(t[i].path, "\"\n\r")) goto fin;
    if (t[i].is_dir) fprintf(cf, "ls -p \"%s\"\n", t[i].path);
    else if (t[i].size <= debugfs_limit) fprintf(cf, "dump \"%s\" %s/f%zu\n", t[i].path, dir, i);
  }
  if (fclose(cf) != 0) {
    cf = NULL;
    goto fin;
  }
  cf = NULL;
  char *const argv[] = {(char *)"debugfs", (char *)"-f", cmdf, (char *)img, NULL};
  if (fu_run(argv, sortie, (size_t)16 << 20) != 0) goto fin;
  res = 0;

  /* Découpe de la sortie : après chaque « debugfs: ls -p », les lignes « /ino/mode/uid/gid/nom/taille/ ». */
  size_t cur = 0;       /* index (dans t) du dossier en cours */
  size_t *dirs = malloc(m->n * sizeof *dirs), nd = 0, di = 0;
  size_t *vus = calloc(m->n, sizeof *vus); /* nb d'entrées vues par dossier (indexé comme t) */
  unsigned char *trouve = calloc(m->n, 1);
  if (!dirs || !vus || !trouve) {
    free(dirs);
    free(vus);
    free(trouve);
    res = -1;
    goto fin;
  }
  for (size_t i = 0; i < m->n; i++)
    if (t[i].is_dir) dirs[nd++] = i;
  int en_dossier = 0;
  for (char *l = strtok(sortie, "\n"); l; l = strtok(NULL, "\n")) {
    if (!strncmp(l, "debugfs: ls -p ", 15)) {
      if (di >= nd) break;
      cur = dirs[di++];
      en_dossier = 1;
      if (!strcmp(t[cur].path, "/")) trouve[cur] = 1;
      continue;
    }
    if (!strncmp(l, "debugfs: ", 9)) {
      en_dossier = 0;
      continue;
    }
    if (!en_dossier || l[0] != '/') continue;
    unsigned long ino, mode, uid, gid;
    int off = 0;
    if (sscanf(l, "/%lu/%lo/%lu/%lu/%n", &ino, &mode, &uid, &gid, &off) != 4 || off == 0) continue;
    /* entrée libre (inode 0) : debugfs liste celle qui ouvre un bloc, avec son ancien nom */
    if (ino == 0) continue;
    char *nom = l + off;
    char *fin_nom = strrchr(nom, '/');
    if (!fin_nom) continue;
    *fin_nom = 0;
    char *fin2 = strrchr(nom, '/');
    if (!fin2) continue;
    *fin2 = 0;
    unsigned long long taille = strtoull(fin2 + 1, NULL, 10);
    if (!strcmp(nom, ".") || !strcmp(nom, "..")) {
      if (!strcmp(nom, ".")) {
        /* attributs du dossier lui-même (la racine n'a pas d'autre source) */
        if (mode != t[cur].mode) ecart(&r, "mode %llo, attendu %llo", t[cur].path, mode, t[cur].mode);
        if (uid != t[cur].uid) ecart(&r, "uid %llu, attendu %llu", t[cur].path, uid, t[cur].uid);
        if (gid != t[cur].gid) ecart(&r, "gid %llu, attendu %llu", t[cur].path, gid, t[cur].gid);
        trouve[cur] = 1;
      }
      continue;
    }
    if (!strcmp(t[cur].path, "/") && !strcmp(nom, "lost+found")) continue;
    char chemin[8192];
    snprintf(chemin, sizeof chemin, "%s%s%s", t[cur].path, t[cur].path[1] ? "/" : "", nom);
    vus[cur]++;
    ent key = {.path = chemin};
    ent *x = bsearch(&key, t, m->n, sizeof *t, cmp_ent);
    if (!x) {
      ecart(&r, "en trop sur le volume (%llu %llu)", chemin, 0, 0);
      continue;
    }
    size_t xi = (size_t)(x - t);
    trouve[xi] = 1;
    if (x->is_dir) continue; /* attributs vérifiés par son propre « . » */
    if (mode != x->mode) ecart(&r, "mode %llo, attendu %llo", chemin, mode, x->mode);
    if (uid != x->uid) ecart(&r, "uid %llu, attendu %llu", chemin, uid, x->uid);
    if (gid != x->gid) ecart(&r, "gid %llu, attendu %llu", chemin, gid, x->gid);
    if (taille != x->size) {
      ecart(&r, "taille %llu, attendu %llu", chemin, taille, x->size);
      continue;
    }
    if (x->size <= debugfs_limit) {
      snprintf(tmp, sizeof tmp, "%s/f%zu", dir, (size_t)(x - t));
      uint8_t sha[32];
      if (hash_fichier(tmp, x->size, sha) != 0) ecart(&r, "dump debugfs incomplet (%llu %llu)", chemin, 0, 0);
      else if (memcmp(sha, x->sha, 32) != 0) ecart(&r, "contenu différent (%llu %llu)", chemin, 0, 0);
      unlink(tmp);
    }
  }
  for (size_t i = 0; i < m->n; i++) {
    if (!trouve[i]) ecart(&r, "absent du volume (%llu %llu)", t[i].path, 0, 0);
    if (t[i].is_dir && vus[i] != nb_enfants(m, t[i].path))
      ecart(&r, "%llu entrées, attendu %llu", t[i].path, vus[i], nb_enfants(m, t[i].path));
  }
  free(dirs);
  free(vus);
  free(trouve);
  if (res == 0) res = r.n;
fin:
  if (cf) fclose(cf);
  free(t);
  free(sortie);
  for (size_t i = 0; i < m->n; i++) { /* restes éventuels des dump (l'index est celui du tableau trié) */
    snprintf(tmp, sizeof tmp, "%s/f%zu", dir, i);
    unlink(tmp);
  }
  unlink(cmdf);
  rmdir(dir);
  return res;
}

void tm_set_fat_attrs(tm *m) {
  for (size_t i = 0; i < m->n; i++) {
    m->e[i].uid = m->e[i].gid = 0;
    m->e[i].mode = m->e[i].is_dir ? 040755 : 0100644;
  }
}

/* Lignes d'un fichier (sans les fins de ligne) ; NULL si illisible. */
static char **lignes(const char *fichier, size_t *n) {
  FILE *f = fopen(fichier, "r");
  if (!f) return NULL;
  char **t = NULL, *l = NULL;
  size_t cap = 0, lcap = 0;
  ssize_t k;
  *n = 0;
  while ((k = getline(&l, &lcap, f)) >= 0) {
    if (k > 0 && l[k - 1] == '\n') l[k - 1] = 0;
    if (*n == cap) {
      cap = cap ? cap * 2 : 256;
      char **nt = realloc(t, cap * sizeof *t);
      if (!nt) break;
      t = nt;
    }
    t[(*n)++] = strdup(l);
  }
  free(l);
  fclose(f);
  return t ? t : calloc(1, sizeof *t);
}

static void liberer_lignes(char **t, size_t n) {
  for (size_t i = 0; i < n; i++) free(t[i]);
  free(t);
}

/* Chemin d'une ligne de manifeste (dernier champ). */
static const char *chemin_ligne(const char *l) {
  const char *c = strrchr(l, '\t');
  return c ? c + 1 : l;
}

/* Manifeste du modèle comparé à celui d'un juge (manifest(img, chemin) : 0 si écrit). */
static int compare_judge(const tm *m, const char *img, int (*manifest)(const char *, const char *)) {
  char dir[4096], a[4200], b[4200];
  if (fu_tmpdir(dir, sizeof dir) != 0) return -1;
  snprintf(a, sizeof a, "%s/modele-XXXXXX", dir);
  snprintf(b, sizeof b, "%s/juge-XXXXXX", dir);
  int fa = mkstemp(a), fb = fa >= 0 ? mkstemp(b) : -1;
  if (fa >= 0) close(fa);
  if (fb >= 0) close(fb);
  int rc = -1;
  if (fa >= 0 && fb >= 0 && tm_write_manifest(m, a) == 0 && manifest(img, b) == 0) {
    size_t na = 0, nb = 0;
    char **la = lignes(a, &na), **lb = lignes(b, &nb);
    if (la && lb) {
      rapport r = {0};
      size_t i = 0, j = 0;
      while (i < na || j < nb) {
        int c = i == na ? 1 : j == nb ? -1 : strcmp(chemin_ligne(la[i]), chemin_ligne(lb[j]));
        if (c == 0 && !strcmp(la[i], lb[j])) {
          i++, j++;
          continue;
        }
        if (r.n < 10 && !tm_quiet) {
          if (c <= 0) fprintf(stderr, "tree_model: modèle : %s\n", la[i]);
          if (c >= 0) fprintf(stderr, "tree_model: juge   : %s\n", lb[j]);
        }
        r.n++;
        if (c <= 0) i++;
        if (c >= 0) j++;
      }
      rc = r.n;
    }
    if (la) liberer_lignes(la, na);
    if (lb) liberer_lignes(lb, nb);
  }
  if (fa >= 0) unlink(a);
  if (fb >= 0) unlink(b);
  return rc;
}

int tm_check_fat_judge(const tm *m, const char *img) { return compare_judge(m, img, fu_fat_manifest); }

void tm_set_ntfs_attrs(tm *m) { tm_set_fat_attrs(m); }

int tm_check_ntfs_judge(const tm *m, const char *img) {
  char out[8192];
  int c = fu_ntfsjuge(out, sizeof out, "check", img, NULL);
  if (c != 0 && c != 3) return -1;
  if (c == 3 && !tm_quiet) fprintf(stderr, "tree_model: ntfsjuge check %s :\n%s", img, out);
  int d = compare_judge(m, img, fu_ntfs_manifest);
  return d < 0 ? -1 : d + (c == 3);
}
