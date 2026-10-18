#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "fluluks/error.h"
#include "fluluks/fs.h"
#include "fs/fs_internal.h"
#include "fs/ro_reasons.h"

#define FL_PATH_MAX 4095
#define FL_NAME_MAX 255

static const struct {
  fl_content kind;
  fl_fs_mount_fn mount;
} k_backends[] = {
    {FL_CONTENT_EXT4, fl_ext4_mount},
};

/* Horloge des horodatages : lue par les systèmes de fichiers sous leur propre verrou, posée par les
 * tests depuis n'importe quel fil. */
static _Atomic(int64_t (*)(void)) g_clock;

void fl_fs_set_clock(int64_t (*now)(void)) { atomic_store(&g_clock, now); }

int64_t fl_fs_now(void) {
  int64_t (*now)(void) = atomic_load(&g_clock);
  return now ? now() : (int64_t)time(NULL);
}

void fl_fs_go_read_only(fl_fs *fs, const char *reason) {
  if (atomic_load(&fs->read_only)) return;
  snprintf(fs->ro_reason, sizeof fs->ro_reason, "%s", reason);
  atomic_store(&fs->read_only, true);
}

/* Chemin absolu, "/" ou "/a/b" : ni composant vide, ni ".", ni "..", ni "/" final. */
static bool path_ok(const char *p) {
  if (!p || p[0] != '/') return false;
  size_t len = 0;
  while (len <= FL_PATH_MAX && p[len]) len++;
  if (len > FL_PATH_MAX) return false;
  if (len == 1) return true;
  const char *c = p + 1;
  for (;;) {
    const char *e = c;
    while (*e && *e != '/') e++;
    size_t n = (size_t)(e - c);
    if (n == 0 || n > FL_NAME_MAX) return false;
    if ((n == 1 && c[0] == '.') || (n == 2 && c[0] == '.' && c[1] == '.')) return false;
    if (!*e) return true;
    c = e + 1;
  }
}

int fl_fs_mount(fl_bdev *dev, fl_content kind, bool want_rw, fl_fs **out) {
  if (!out) return FL_EINVAL;
  *out = NULL;
  if (!dev) return FL_EINVAL;
  for (size_t i = 0; i < sizeof k_backends / sizeof k_backends[0]; i++) {
    if (k_backends[i].kind != kind) continue;
    fl_fs *fs = NULL;
    /* Lecture seule demandée, ou imposée par le périphérique (disque protégé en écriture, ou
     * image ouverte ainsi) : raison posée ici. Sinon le système de fichiers décide, et pose
     * sa raison s'il refuse l'écriture. */
    bool rw = want_rw && !dev->read_only;
    int rc = k_backends[i].mount(dev, rw, &fs);
    if (rc != FL_OK) return rc;
    if (!rw) {
      fs->read_only = true;
      snprintf(fs->ro_reason, sizeof fs->ro_reason, "%s", want_rw ? FL_RO_WP : FL_RO_USER);
    }
    *out = fs;
    return FL_OK;
  }
  return FL_ENOTSUP;
}

int fl_fs_statfs(fl_fs *fs, fl_statfs *out) {
  if (!fs || !out) return FL_EINVAL;
  memset(out, 0, sizeof *out);
  int rc = fs->ops->statfs(fs, out);
  if (rc != FL_OK) return rc;
  /* la raison n'est lue qu'une fois read_only vu vrai : elle n'est plus modifiée ensuite */
  out->read_only = atomic_load(&fs->read_only);
  if (out->read_only) {
    memcpy(out->ro_reason, fs->ro_reason, sizeof out->ro_reason);
    out->ro_reason[sizeof out->ro_reason - 1] = 0;
  }
  return FL_OK;
}

int fl_fs_stat(fl_fs *fs, const char *path, fl_stat *out) {
  if (!fs || !out || !path_ok(path)) return FL_EINVAL;
  return fs->ops->stat(fs, path, out);
}

int fl_fs_readdir(fl_fs *fs, const char *path, int (*cb)(void *ctx, const fl_dirent *e), void *ctx) {
  if (!fs || !cb || !path_ok(path)) return FL_EINVAL;
  return fs->ops->readdir(fs, path, cb, ctx);
}

int fl_fs_open(fl_fs *fs, const char *path, int flags, fl_file **out) {
  if (!out) return FL_EINVAL;
  *out = NULL;
  if (!fs || !path_ok(path)) return FL_EINVAL;
  if (flags & ~(FL_O_RDWR | FL_O_CREAT | FL_O_TRUNC)) return FL_EINVAL;
  if (flags != FL_O_RDONLY && fs->read_only) return FL_EROFS;
  return fs->ops->open(fs, path, flags, out);
}

int64_t fl_file_pread(fl_file *f, void *buf, size_t len, uint64_t off) {
  if (!f || (!buf && len)) return FL_EINVAL;
  if (len == 0) return 0;
  return f->fs->ops->pread(f, buf, len, off);
}

int64_t fl_file_pwrite(fl_file *f, const void *buf, size_t len, uint64_t off) {
  if (!f || (!buf && len)) return FL_EINVAL;
  if (f->fs->read_only || !(f->flags & FL_O_RDWR) || !f->fs->ops->pwrite) return FL_EROFS;
  return f->fs->ops->pwrite(f, buf, len, off);
}

int fl_file_truncate(fl_file *f, uint64_t size) {
  if (!f) return FL_EINVAL;
  if (f->fs->read_only || !(f->flags & FL_O_RDWR) || !f->fs->ops->truncate) return FL_EROFS;
  return f->fs->ops->truncate(f, size);
}

int fl_file_close(fl_file *f) {
  if (!f) return FL_OK;
  return f->fs->ops->close(f);
}

/* Opérations d'écriture sur un chemin : validation commune, puis lecture seule. */
static int write_op(fl_fs *fs, const char *path, int (*op)(fl_fs *, const char *)) {
  if (!fs || !path_ok(path)) return FL_EINVAL;
  if (fs->read_only || !op) return FL_EROFS;
  return op(fs, path);
}

int fl_fs_mkdir(fl_fs *fs, const char *path) { return write_op(fs, path, fs ? fs->ops->mkdir : NULL); }

int fl_fs_unlink(fl_fs *fs, const char *path) { return write_op(fs, path, fs ? fs->ops->unlink : NULL); }

int fl_fs_rmdir(fl_fs *fs, const char *path) { return write_op(fs, path, fs ? fs->ops->rmdir : NULL); }

int fl_fs_rename(fl_fs *fs, const char *from, const char *to) {
  if (!fs || !path_ok(from) || !path_ok(to)) return FL_EINVAL;
  if (fs->read_only || !fs->ops->rename) return FL_EROFS;
  return fs->ops->rename(fs, from, to);
}

int fl_fs_sync(fl_fs *fs) {
  if (!fs) return FL_EINVAL;
  if (fs->read_only || !fs->ops->sync) return FL_OK;
  return fs->ops->sync(fs);
}

int fl_fs_unmount(fl_fs *fs) {
  if (!fs) return FL_OK;
  return fs->ops->unmount(fs);
}
