/* ntfs-remplir : remplit une image NTFS vide (mkntfs) avec une arborescence, pour les images de test de la
 * phase 5. PC seulement, sans root : libntfs-3g directement, avec ses propres opérations de périphérique
 * sur un descripteur de fichier (indépendant de la colle du moteur).
 *
 * Usage : ntfs-remplir <img> <dossier> [--speciaux] [--hiber] [--casse]
 *   <dossier>    copié à la racine : dossiers, fichiers ordinaires, mtime de la source (atime, ctime et
 *                date de création identiques) ; entrées triées par nom (image reproductible).
 *   --speciaux   lien dur, flux nommé, fichier compressé, fichier creux de 5 Gio, lien symbolique, jonction,
 *                point d'analyse inconnu, nom de 600 octets UTF-8 (voir le plan, T1 Step 2).
 *   --hiber      /hiberfil.sys de 64 Kio commençant par « hibr » (Windows en veille prolongée).
 *   --casse      /Casse.txt et /casse.txt (noms qui ne diffèrent que par la casse, espace POSIX).
 * Sortie 0 si tout est écrit, 1 sinon (message sur la sortie d'erreur). */

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ntfs-3g/attrib.h"
#include "ntfs-3g/cache.h"
#include "ntfs-3g/device.h"
#include "ntfs-3g/dir.h"
#include "ntfs-3g/inode.h"
#include "ntfs-3g/layout.h"
#include "ntfs-3g/ntfstime.h"
#include "ntfs-3g/reparse.h"
#include "ntfs-3g/unistr.h"
#include "ntfs-3g/volume.h"

/* --- périphérique sur un descripteur de fichier ------------------------------------------------- */

typedef struct {
  int fd;
  off_t pos;
} fdev;

static int d_open(struct ntfs_device *dev, int flags) {
  fdev *f = dev->d_private;
  f->fd = open(dev->d_name, (flags & O_RDWR) == O_RDWR ? O_RDWR : O_RDONLY);
  if (f->fd < 0) return -1;
  if ((flags & O_RDWR) != O_RDWR) NDevSetReadOnly(dev);
  NDevSetOpen(dev);
  return 0;
}

static int d_close(struct ntfs_device *dev) {
  fdev *f = dev->d_private;
  int rc = fsync(f->fd) | close(f->fd);
  NDevClearOpen(dev);
  return rc ? -1 : 0;
}

static s64 d_seek(struct ntfs_device *dev, s64 off, int whence) {
  fdev *f = dev->d_private;
  off_t r = lseek(f->fd, off, whence);
  if (r >= 0) f->pos = r;
  return r;
}

static s64 d_read(struct ntfs_device *dev, void *b, s64 n) {
  fdev *f = dev->d_private;
  ssize_t r = pread(f->fd, b, (size_t)n, f->pos);
  if (r > 0) f->pos += r;
  return r;
}

static s64 d_write(struct ntfs_device *dev, const void *b, s64 n) {
  fdev *f = dev->d_private;
  NDevSetDirty(dev);
  ssize_t r = pwrite(f->fd, b, (size_t)n, f->pos);
  if (r > 0) f->pos += r;
  return r;
}

static s64 d_pread(struct ntfs_device *dev, void *b, s64 n, s64 off) {
  return pread(((fdev *)dev->d_private)->fd, b, (size_t)n, off);
}

static s64 d_pwrite(struct ntfs_device *dev, const void *b, s64 n, s64 off) {
  NDevSetDirty(dev);
  return pwrite(((fdev *)dev->d_private)->fd, b, (size_t)n, off);
}

static int d_sync(struct ntfs_device *dev) {
  NDevClearDirty(dev);
  return fsync(((fdev *)dev->d_private)->fd);
}

static int d_stat(struct ntfs_device *dev, struct stat *st) { return fstat(((fdev *)dev->d_private)->fd, st); }

static int d_ioctl(struct ntfs_device *dev, unsigned long req, void *arg) {
  (void)dev;
  (void)req;
  (void)arg;
  errno = ENOTTY;
  return -1;
}

static struct ntfs_device_operations k_ops = {
    .open = d_open, .close = d_close, .seek = d_seek, .read = d_read, .write = d_write,
    .pread = d_pread, .pwrite = d_pwrite, .sync = d_sync, .stat = d_stat, .ioctl = d_ioctl,
};

/* --- écriture dans le volume ------------------------------------------------------------------- */

static int fail(const char *what, const char *name) {
  fprintf(stderr, "ntfs-remplir : %s %s : %s\n", what, name, strerror(errno));
  return -1;
}

/* Crée `name` (UTF-8) dans dir_ni ; type S_IFREG ou S_IFDIR. Un inode dont le parent est ouvert se ferme par
 * ntfs_inode_close_in_dir (sinon libntfs-3g rouvre le parent pour mettre à jour l'index : EBUSY). */
static ntfs_inode *create(ntfs_inode *dir_ni, const char *name, mode_t type) {
  ntfschar *u = NULL;
  int len = ntfs_mbstoucs(name, &u);
  if (len < 0) {
    fail("nom", name);
    return NULL;
  }
  ntfs_inode *ni = ntfs_create(dir_ni, 0, u, (u8)len, type);
  free(u);
  if (!ni) fail("création de", name);
  return ni;
}

static int write_stream(ntfs_inode *ni, ntfschar *sname, int slen, const void *buf, s64 len, s64 off,
                        const char *what) {
  ntfs_attr *na = ntfs_attr_open(ni, AT_DATA, sname, (u32)slen);
  if (!na) return fail("ouverture du flux de", what);
  s64 done = 0;
  while (done < len) {
    s64 w = ntfs_attr_pwrite(na, off + done, len - done, (const char *)buf + done);
    if (w <= 0) {
      ntfs_attr_close(na);
      return fail("écriture de", what);
    }
    done += w;
  }
  ntfs_attr_close(na);
  return 0;
}

static void set_times(ntfs_inode *ni, time_t t) {
  struct timespec ts = {.tv_sec = t, .tv_nsec = 0};
  ntfs_time nt = timespec2ntfs(ts);
  ni->creation_time = ni->last_data_change_time = ni->last_mft_change_time = ni->last_access_time = nt;
  NInoFileNameSetDirty(ni);
  ntfs_inode_mark_dirty(ni);
}

static int by_name(const struct dirent **a, const struct dirent **b) { return strcmp((*a)->d_name, (*b)->d_name); }

static int copy_tree(ntfs_inode *dir_ni, const char *src) {
  struct dirent **ents;
  int n = scandir(src, &ents, NULL, by_name);
  if (n < 0) return fail("lecture du dossier", src);
  int rc = 0;
  for (int i = 0; i < n; i++) {
    const char *nm = ents[i]->d_name;
    if (rc || !strcmp(nm, ".") || !strcmp(nm, "..")) continue;
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", src, nm);
    struct stat st;
    if (lstat(path, &st)) {
      rc = fail("stat de", path);
      continue;
    }
    if (S_ISDIR(st.st_mode)) {
      ntfs_inode *ni = create(dir_ni, nm, S_IFDIR);
      if (!ni) {
        rc = -1;
        continue;
      }
      rc = copy_tree(ni, path);
      set_times(ni, st.st_mtime);
      if (ntfs_inode_close_in_dir(ni, dir_ni)) rc = fail("fermeture de", path);
    } else if (S_ISREG(st.st_mode)) {
      ntfs_inode *ni = create(dir_ni, nm, S_IFREG);
      if (!ni) {
        rc = -1;
        continue;
      }
      int fd = open(path, O_RDONLY);
      if (fd < 0) rc = fail("ouverture de", path);
      char *buf = malloc(1 << 20);
      s64 off = 0;
      while (!rc && buf) {
        ssize_t r = read(fd, buf, 1 << 20);
        if (r < 0) rc = fail("lecture de", path);
        if (r <= 0) break;
        rc = write_stream(ni, AT_UNNAMED, 0, buf, r, off, path);
        off += r;
      }
      if (!buf) rc = fail("mémoire pour", path);
      free(buf);
      if (fd >= 0) close(fd);
      set_times(ni, st.st_mtime);
      if (ntfs_inode_close_in_dir(ni, dir_ni)) rc = fail("fermeture de", path);
    }
  }
  for (int i = 0; i < n; i++) free(ents[i]);
  free(ents);
  return rc;
}

static ntfs_inode *open_path(ntfs_volume *vol, const char *path) {
  ntfs_inode *ni = ntfs_pathname_to_inode(vol, NULL, path);
  if (!ni) fail("introuvable :", path);
  return ni;
}

/* Fichier `name` à la racine, contenu `data` (len octets). */
static int simple_file(ntfs_inode *root, const char *name, const void *data, s64 len) {
  ntfs_inode *ni = create(root, name, S_IFREG);
  if (!ni) return -1;
  int rc = len ? write_stream(ni, AT_UNNAMED, 0, data, len, 0, name) : 0;
  if (ntfs_inode_close_in_dir(ni, root)) rc = fail("fermeture de", name);
  return rc;
}

static void put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
  put16(p, (uint16_t)v);
  put16(p + 2, (uint16_t)(v >> 16));
}

/* Copie l'ASCII `s` en UTF-16LE à p ; rend le nombre d'octets écrits. */
static size_t utf16(uint8_t *p, const char *s) {
  size_t n = strlen(s);
  for (size_t i = 0; i < n; i++) put16(p + 2 * i, (uint8_t)s[i]);
  return 2 * n;
}

/* Point d'analyse lien symbolique (flags 1 = relatif) ou jonction : en-tête REPARSE_POINT + tampon. */
static int reparse_link(ntfs_inode *root, const char *name, bool symlink, const char *subst, const char *print,
                        mode_t type) {
  uint8_t b[512] = {0};
  size_t hdr = symlink ? 12 : 8; /* SubstituteNameOffset … PrintNameLength (+ Flags) */
  uint8_t *pb = b + 8 + hdr;
  size_t ls = utf16(pb, subst);
  size_t lp = utf16(pb + ls + 2, print);
  size_t data = hdr + ls + 2 + lp + 2;
  put32(b, symlink ? 0xA000000Cu : 0xA0000003u);
  put16(b + 4, (uint16_t)data);
  put16(b + 8, 0);
  put16(b + 10, (uint16_t)ls);
  put16(b + 12, (uint16_t)(ls + 2));
  put16(b + 14, (uint16_t)lp);
  if (symlink) put32(b + 16, 1);
  ntfs_inode *ni = create(root, name, type);
  if (!ni) return -1;
  int rc = ntfs_set_ntfs_reparse_data(ni, (const char *)b, 8 + data, 0) ? fail("point d'analyse de", name) : 0;
  if (ntfs_inode_close_in_dir(ni, root)) rc = fail("fermeture de", name);
  return rc;
}

static int speciaux(ntfs_volume *vol, ntfs_inode *root) {
  int rc = 0;
  /* lien dur : second nom de /abc.txt */
  ntfs_inode *abc = open_path(vol, "/abc.txt");
  if (!abc) return -1;
  ntfschar *u = NULL;
  int len = ntfs_mbstoucs("lien-dur.txt", &u);
  if (len < 0 || ntfs_link(abc, root, u, (u8)len)) rc = fail("lien dur", "lien-dur.txt");
  free(u);
  /* flux nommé /abc.txt:secret */
  ntfschar *sn = NULL;
  int sl = ntfs_mbstoucs("secret", &sn);
  if (!rc && (sl < 0 || ntfs_attr_add(abc, AT_DATA, sn, (u8)sl, NULL, 0))) rc = fail("flux nommé", "secret");
  if (!rc) rc = write_stream(abc, sn, sl, "caché\n", 7, 0, "abc.txt:secret");
  free(sn);
  if (ntfs_inode_close_in_dir(abc, root)) rc = fail("fermeture de", "abc.txt");
  if (rc) return rc;

  /* fichier compressé : attribut posé avant l'écriture, compression activée le temps de l'écrire */
  static const char motif[] = "FluLuks NTFS\n";
  size_t clen = 256 * 1024;
  char *c = malloc(clen);
  if (!c) return fail("mémoire pour", "compressé.txt");
  for (size_t i = 0; i < clen; i++) c[i] = motif[i % (sizeof motif - 1)];
  NVolSetCompression(vol);
  ntfs_inode *ni = create(root, "compressé.txt", S_IFREG);
  if (ni) {
    ni->flags |= FILE_ATTR_COMPRESSED;
    ntfs_inode_mark_dirty(ni);
    rc = write_stream(ni, AT_UNNAMED, 0, c, (s64)clen, 0, "compressé.txt");
    if (ntfs_inode_close_in_dir(ni, root)) rc = fail("fermeture de", "compressé.txt");
  } else {
    rc = -1;
  }
  NVolClearCompression(vol);
  free(c);
  if (rc) return rc;

  /* fichier creux de 5 Gio, « FIN-FLULUKS » à la fin */
  ni = create(root, "clairsemé.bin", S_IFREG);
  if (!ni) return -1;
  rc = write_stream(ni, AT_UNNAMED, 0, "FIN-FLULUKS", 11, 5368709120LL - 11, "clairsemé.bin");
  if (ntfs_inode_close_in_dir(ni, root)) rc = fail("fermeture de", "clairsemé.bin");
  if (rc) return rc;

  if (reparse_link(root, "lien", true, "abc.txt", "abc.txt", S_IFREG)) return -1;
  if (reparse_link(root, "jonction", false, "\\??\\C:\\p1", "C:\\p1", S_IFDIR)) return -1;

  /* point d'analyse inconnu (déduplication) sur un fichier de 4 Kio */
  char *d = calloc(1, 4096);
  if (!d) return fail("mémoire pour", "dédup.bin");
  memset(d, 'D', 4096);
  ni = create(root, "dédup.bin", S_IFREG);
  if (!ni) {
    free(d);
    return -1;
  }
  rc = write_stream(ni, AT_UNNAMED, 0, d, 4096, 0, "dédup.bin");
  free(d);
  uint8_t rp[8 + 16] = {0};
  put32(rp, 0x80000013u);
  put16(rp + 4, 16);
  memset(rp + 8, 0x5A, 16);
  if (!rc && ntfs_set_ntfs_reparse_data(ni, (const char *)rp, sizeof rp, 0)) rc = fail("point d'analyse de", "dédup.bin");
  if (ntfs_inode_close_in_dir(ni, root)) rc = fail("fermeture de", "dédup.bin");
  if (rc) return rc;

  /* nom de 200 « 中 » : 600 octets UTF-8, 200 unités UTF-16 (valide sous Windows) */
  char nom[700] = "nom-trop-long-";
  size_t p = strlen(nom);
  for (int i = 0; i < 200; i++, p += 3) memcpy(nom + p, "\xe4\xb8\xad", 3);
  memcpy(nom + p, ".txt", 5);
  return simple_file(root, nom, "long\n", 5);
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage : ntfs-remplir <img> <dossier> [--speciaux] [--hiber] [--casse]\n");
    return 2;
  }
  bool sp = false, hiber = false, casse = false;
  for (int i = 3; i < argc; i++) {
    if (!strcmp(argv[i], "--speciaux")) sp = true;
    else if (!strcmp(argv[i], "--hiber")) hiber = true;
    else if (!strcmp(argv[i], "--casse")) casse = true;
    else {
      fprintf(stderr, "ntfs-remplir : option inconnue %s\n", argv[i]);
      return 2;
    }
  }
  fdev f = {.fd = -1};
  struct ntfs_device *dev = ntfs_device_alloc(argv[1], 0, &k_ops, &f);
  if (!dev) {
    fail("périphérique", argv[1]);
    return 1;
  }
  ntfs_volume *vol = ntfs_device_mount(dev, NTFS_MNT_NONE);
  if (!vol) {
    fail("montage de", argv[1]);
    ntfs_device_free(dev);
    return 1;
  }
  /* Comme ntfs_mount() : sans ces caches, ntfs_inode_lookup_by_mbsname échoue (EINVAL) en mode sensible à
   * la casse (il convertit alors un nom NULL). */
  ntfs_create_lru_caches(vol);
  ntfs_inode *root = ntfs_inode_open(vol, FILE_root);
  int rc = root ? 0 : fail("racine de", argv[1]);
  if (!rc) rc = copy_tree(root, argv[2]);
  if (!rc && sp) rc = speciaux(vol, root);
  if (!rc && hiber) {
    char *h = calloc(1, 65536);
    if (!h) rc = fail("mémoire pour", "hiberfil.sys");
    else {
      memcpy(h, "hibr", 4);
      rc = simple_file(root, "hiberfil.sys", h, 65536);
      free(h);
    }
  }
  if (!rc && casse) rc = simple_file(root, "Casse.txt", "A\n", 2) || simple_file(root, "casse.txt", "b\n", 2);
  if (root && ntfs_inode_close(root)) rc = fail("fermeture de", "/");
  if (ntfs_umount(vol, FALSE)) rc = fail("démontage de", argv[1]);
  return rc ? 1 : 0;
}
