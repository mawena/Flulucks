/* Périphérique de libntfs-3g sur fl_bdev (voir ntfs_dev.h). */

#define _GNU_SOURCE /* struct stat complet (st_mtime) pour les en-têtes de libntfs-3g */

#include "fs/ntfs_dev.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "fluluks/error.h"
#include "fs/wbuf.h"
#include "ntfs-3g/device.h"

#define BIG_WRITE (64u * 1024)  /* écriture alignée d'au moins cette taille : tampon contigu */
#define TABLE_BYTES (4u << 20)  /* table des secteurs en attente ou lus : 4 Mio au plus */
#define SMALL_READ (64u * 1024) /* lecture d'au plus cette taille : secteurs gardés dans la table */

/* Secteur de la table : numéro, place de ses octets dans slab, en attente d'écriture (dirty) ou simple copie
 * du disque (lecture). */
typedef struct {
  uint64_t lba;
  uint32_t slot;
  bool dirty;
} pend;

struct ntfs_dev {
  fl_bdev *bd;
  struct ntfs_device *dev;
  bool writable;
  bool direct; /* écritures envoyées aussitôt au disque */
  int err;     /* première erreur d'écriture ou de vidage */
  s64 pos;     /* curseur de seek/read/write */
  fl_wbuf wb;  /* grandes écritures contiguës */
  /* petites écritures et petites lectures : secteurs triés par lba ; jamais en recouvrement avec wb */
  pend *tab;
  uint32_t n, cap;
  uint8_t *slab;     /* cap secteurs */
  uint32_t *free_sl; /* emplacements libres de slab (pile) */
  uint32_t nfree;
};

static int to_errno(int rc) {
  switch (rc) {
    case FL_EROFS: return EROFS;
    case FL_ENODEV: return ENODEV;
    case FL_ETIMEDOUT: return ETIMEDOUT;
    case FL_ENOMEM: return ENOMEM;
    case FL_EINVAL: return EINVAL;
    default: return EIO;
  }
}

static ntfs_dev *st(struct ntfs_device *dev) { return dev->d_private; }

/* --- écritures en attente ------------------------------------------------------------------------ */

static int fail(ntfs_dev *d, int rc) {
  if (rc != FL_ENOMEM && d->err == FL_OK) d->err = rc;
  return rc;
}

/* Rappel de fl_wbuf : [p, p + len) à off, aligné sur le secteur. */
static int wb_flush_fn(void *ctx, const void *p, size_t len, uint64_t off) {
  ntfs_dev *d = ctx;
  uint32_t ss = d->bd->sector_size;
  const uint8_t *q = p;
  uint64_t lba = off / ss, n = len / ss;
  while (n) {
    uint32_t c = n > (1u << 16) ? 1u << 16 : (uint32_t)n;
    int rc = fl_bdev_write(d->bd, lba, c, q);
    if (rc != FL_OK) return rc;
    q += (size_t)c * ss;
    lba += c;
    n -= c;
  }
  return FL_OK;
}

static int drain_wb(ntfs_dev *d) {
  int rc = d->wb.err != FL_OK ? d->wb.err : fl_wbuf_flush(&d->wb, wb_flush_fn, d);
  return rc == FL_OK ? FL_OK : fail(d, rc);
}

/* Premier indice de tab dont le lba est ≥ lba. */
static uint32_t lower(const ntfs_dev *d, uint64_t lba) {
  uint32_t a = 0, b = d->n;
  while (a < b) {
    uint32_t m = (a + b) / 2;
    if (d->tab[m].lba < lba) a = m + 1;
    else b = m;
  }
  return a;
}

static uint8_t *sec_data(const ntfs_dev *d, uint32_t i) { return d->slab + (size_t)d->tab[i].slot * d->bd->sector_size; }

/* Retire les secteurs en attente de [lba, lba + n) (remplacés par une écriture plus récente). */
static void drop_range(ntfs_dev *d, uint64_t lba, uint64_t n) {
  if (!d->n) return;
  uint32_t i = lower(d, lba), j = i;
  while (j < d->n && d->tab[j].lba < lba + n) d->free_sl[d->nfree++] = d->tab[j++].slot;
  memmove(d->tab + i, d->tab + j, (d->n - j) * sizeof *d->tab);
  d->n -= j - i;
}

/* Écrit les secteurs en attente dans l'ordre, en regroupant les voisins ; ils restent dans la table, propres. */
static int drain_tab(ntfs_dev *d) {
  uint32_t ss = d->bd->sector_size;
  int rc = FL_OK;
  uint8_t *run = NULL;
  for (uint32_t i = 0; rc == FL_OK && i < d->n;) {
    if (!d->tab[i].dirty) {
      i++;
      continue;
    }
    uint32_t j = i + 1;
    while (j < d->n && d->tab[j].dirty && d->tab[j].lba == d->tab[j - 1].lba + 1 && j - i < 256) j++;
    if (j - i == 1) {
      rc = fl_bdev_write(d->bd, d->tab[i].lba, 1, sec_data(d, i));
    } else {
      if (!run && !(run = malloc((size_t)256 * ss))) {
        rc = FL_ENOMEM;
        break;
      }
      for (uint32_t k = i; k < j; k++) memcpy(run + (size_t)(k - i) * ss, sec_data(d, k), ss);
      rc = fl_bdev_write(d->bd, d->tab[i].lba, j - i, run);
    }
    for (uint32_t k = i; rc == FL_OK && k < j; k++) d->tab[k].dirty = false;
    i = j;
  }
  free(run);
  return rc == FL_OK ? FL_OK : fail(d, rc);
}

/* Retire les secteurs propres (copies du disque) de la table. */
static void drop_clean(ntfs_dev *d) {
  uint32_t k = 0;
  for (uint32_t i = 0; i < d->n; i++) {
    if (d->tab[i].dirty) d->tab[k++] = d->tab[i];
    else d->free_sl[d->nfree++] = d->tab[i].slot;
  }
  d->n = k;
}

/* Tout ce qui attend, sur le disque (sans vider son cache). La table n'a jamais de secteur de wb. */
static int drain_all(ntfs_dev *d) {
  if (d->err != FL_OK) return d->err;
  int rc = drain_wb(d);
  if (rc == FL_OK) rc = drain_tab(d);
  return rc;
}

/* Vrai si [off, off + n) recouvre le tampon contigu. */
static bool in_wb(const ntfs_dev *d, uint64_t off, uint64_t n) {
  return d->wb.data && off < d->wb.off + d->wb.len && d->wb.off < off + n;
}

/* Table allouée (à la première utilisation) et au moins un emplacement libre : propres retirés d'abord,
 * sinon tout est écrit puis retiré. */
static int room(ntfs_dev *d) {
  uint32_t ss = d->bd->sector_size;
  if (!d->slab) {
    d->cap = TABLE_BYTES / ss;
    d->slab = malloc((size_t)d->cap * ss);
    d->tab = malloc((size_t)d->cap * sizeof *d->tab);
    d->free_sl = malloc((size_t)d->cap * sizeof *d->free_sl);
    if (!d->slab || !d->tab || !d->free_sl) {
      free(d->slab);
      free(d->tab);
      free(d->free_sl);
      d->slab = NULL;
      d->tab = NULL;
      d->free_sl = NULL;
      return FL_ENOMEM;
    }
    for (uint32_t s = 0; s < d->cap; s++) d->free_sl[s] = d->cap - 1 - s;
    d->nfree = d->cap;
  }
  if (d->nfree) return FL_OK;
  drop_clean(d);
  if (d->nfree) return FL_OK;
  int rc = drain_all(d);
  if (rc == FL_OK) drop_clean(d);
  return rc;
}

/* Insère le secteur lba (b : ss octets) à l'indice i de tab (place libre garantie). */
static void insert(ntfs_dev *d, uint32_t i, uint64_t lba, const uint8_t *b, bool dirty) {
  uint32_t ss = d->bd->sector_size;
  uint32_t slot = d->free_sl[--d->nfree];
  memcpy(d->slab + (size_t)slot * ss, b, ss);
  memmove(d->tab + i + 1, d->tab + i, (d->n - i) * sizeof *d->tab);
  d->tab[i] = (pend){lba, slot, dirty};
  d->n++;
}

/* Met en attente le secteur lba, modifié aux octets [o, o + k) par p (secteur lu d'abord s'il est
 * partiel et absent). */
static int put_sector(ntfs_dev *d, uint64_t lba, uint32_t o, const uint8_t *p, uint32_t k) {
  uint32_t ss = d->bd->sector_size;
  uint32_t i = lower(d, lba);
  if (i < d->n && d->tab[i].lba == lba) {
    memcpy(sec_data(d, i) + o, p, k);
    d->tab[i].dirty = true;
    return FL_OK;
  }
  int rc = room(d);
  if (rc != FL_OK) return rc;
  uint8_t *b = malloc(ss);
  if (!b) return FL_ENOMEM;
  if (k < ss) rc = fl_bdev_read(d->bd, lba, 1, b);
  if (rc == FL_OK) {
    memcpy(b + o, p, k);
    insert(d, lower(d, lba), lba, b, true);
  }
  free(b);
  return rc;
}

/* Petite lecture alignée [lba, lba + n) dans out : secteurs de la table, sinon disque ; les secteurs lus
 * du disque sont gardés (propres). */
static int read_small(ntfs_dev *d, uint64_t lba, uint32_t n, uint8_t *out) {
  uint32_t ss = d->bd->sector_size;
  uint32_t i = lower(d, lba), have = 0;
  while (i + have < d->n && have < n && d->tab[i + have].lba == lba + have) have++;
  if (have == n) {
    for (uint32_t k = 0; k < n; k++) memcpy(out + (size_t)k * ss, sec_data(d, i + k), ss);
    return FL_OK;
  }
  int rc = fl_bdev_read(d->bd, lba, n, out);
  if (rc != FL_OK) return rc;
  /* Secteurs en attente recopiés d'abord (plus récents que le disque) : faire de la place ensuite peut
   * écrire et retirer ceux de la suite de la lecture, que out doit déjà contenir. */
  for (uint32_t j = lower(d, lba); j < d->n && d->tab[j].lba < lba + n; j++)
    memcpy(out + (size_t)(d->tab[j].lba - lba) * ss, sec_data(d, j), ss);
  for (uint32_t k = 0; k < n; k++) {
    uint32_t j = lower(d, lba + k);
    if ((j >= d->n || d->tab[j].lba != lba + k) && room(d) == FL_OK)
      insert(d, lower(d, lba + k), lba + k, out + (size_t)k * ss, false);
  }
  return FL_OK;
}

/* Écriture immédiate de [p, p + n) à off (mode direct : rien n'attend) ; secteurs de bord relus puis
 * réécrits. */
static int write_now(ntfs_dev *d, const uint8_t *p, uint64_t n, uint64_t off) {
  uint32_t ss = d->bd->sector_size;
  uint64_t lba = off / ss;
  uint32_t head = (uint32_t)(off % ss);
  drop_range(d, lba, (off + n + ss - 1) / ss - lba); /* écrits ici, plus récents : la table ne doit plus les écraser */
  uint8_t *b = NULL;
  int rc = FL_OK;
  if (head || n < ss) {
    if (!(b = malloc(ss))) return FL_ENOMEM;
    rc = fl_bdev_read(d->bd, lba, 1, b);
    uint64_t k = n < ss - head ? n : ss - head;
    if (rc == FL_OK) {
      memcpy(b + head, p, (size_t)k);
      rc = fl_bdev_write(d->bd, lba, 1, b);
    }
    p += k;
    n -= k;
    lba++;
  }
  while (rc == FL_OK && n >= ss) {
    uint64_t sec = n / ss;
    uint32_t c = sec > (1u << 16) ? (1u << 16) : (uint32_t)sec;
    rc = fl_bdev_write(d->bd, lba, c, p);
    p += (uint64_t)c * ss;
    n -= (uint64_t)c * ss;
    lba += c;
  }
  if (rc == FL_OK && n) {
    if (!b && !(b = malloc(ss))) return FL_ENOMEM;
    rc = fl_bdev_read(d->bd, lba, 1, b);
    if (rc == FL_OK) {
      memcpy(b, p, (size_t)n);
      rc = fl_bdev_write(d->bd, lba, 1, b);
    }
  }
  free(b);
  return rc;
}

/* Écriture différée de [p, p + n) à off. */
static int write_later(ntfs_dev *d, const uint8_t *p, uint64_t n, uint64_t off) {
  uint32_t ss = d->bd->sector_size;
  if (off % ss == 0 && n % ss == 0 && n >= BIG_WRITE) {
    drop_range(d, off / ss, n / ss);
    int rc = fl_wbuf_write(&d->wb, p, (size_t)n, off, wb_flush_fn, d);
    return rc;
  }
  if (in_wb(d, off, n)) {
    int rc = drain_wb(d);
    if (rc != FL_OK) return rc;
  }
  while (n) {
    uint64_t lba = off / ss;
    uint32_t o = (uint32_t)(off % ss), k = n < ss - o ? (uint32_t)n : ss - o;
    int rc = put_sector(d, lba, o, p, k);
    if (rc != FL_OK) return rc;
    p += k;
    off += k;
    n -= k;
  }
  return FL_OK;
}

/* --- opérations de libntfs-3g --------------------------------------------------------------------- */

static int d_open(struct ntfs_device *dev, int flags) {
  ntfs_dev *d = st(dev);
  if ((flags & O_RDWR) == O_RDWR) {
    if (!d->writable) {
      errno = EROFS;
      return -1;
    }
  } else {
    NDevSetReadOnly(dev);
  }
  d->pos = 0;
  NDevSetOpen(dev);
  return 0;
}

static int d_sync(struct ntfs_device *dev) {
  ntfs_dev *d = st(dev);
  if (NDevReadOnly(dev) || !NDevDirty(dev)) return 0;
  int rc = d->writable ? drain_all(d) : FL_OK;
  if (rc == FL_OK) rc = fl_bdev_flush(d->bd);
  if (rc != FL_OK) {
    fail(d, rc);
    errno = to_errno(rc);
    return -1;
  }
  NDevClearDirty(dev);
  return 0;
}

static int d_close(struct ntfs_device *dev) {
  int rc = d_sync(dev);
  NDevClearOpen(dev);
  return rc;
}

static s64 d_pread(struct ntfs_device *dev, void *buf, s64 count, s64 off) {
  ntfs_dev *d = st(dev);
  if (count < 0 || off < 0) {
    errno = EINVAL;
    return -1;
  }
  uint64_t size = fl_bdev_size_bytes(d->bd);
  if ((uint64_t)off >= size) return 0;
  if ((uint64_t)count > size - (uint64_t)off) count = (s64)(size - (uint64_t)off);
  if (count == 0) return 0;
  int rc = in_wb(d, (uint64_t)off, (uint64_t)count) ? drain_wb(d) : FL_OK;
  uint32_t ss = d->bd->sector_size;
  uint64_t first = (uint64_t)off / ss, end = ((uint64_t)off + (uint64_t)count + ss - 1) / ss;
  if (rc == FL_OK && (end - first) * ss <= SMALL_READ) { /* bitmap, enregistrements, index : par la table */
    uint8_t *b = malloc((size_t)(end - first) * ss);
    rc = b ? read_small(d, first, (uint32_t)(end - first), b) : FL_ENOMEM;
    if (rc == FL_OK) memcpy(buf, b + ((uint64_t)off - first * ss), (size_t)count);
    free(b);
    if (rc != FL_OK) {
      errno = to_errno(rc);
      return -1;
    }
    return count;
  }
  if (rc == FL_OK) rc = fl_bdev_read_bytes(d->bd, (uint64_t)off, (size_t)count, buf);
  /* secteurs de la table par-dessus (seuls ceux en attente diffèrent du disque) */
  for (uint32_t i = lower(d, first); rc == FL_OK && i < d->n && d->tab[i].lba < end; i++) {
    if (!d->tab[i].dirty) continue;
    uint64_t s0 = d->tab[i].lba * ss, s1 = s0 + ss;
    uint64_t a = s0 > (uint64_t)off ? s0 : (uint64_t)off, b = s1 < (uint64_t)off + (uint64_t)count ? s1 : (uint64_t)off + (uint64_t)count;
    memcpy((uint8_t *)buf + (a - (uint64_t)off), sec_data(d, i) + (a - s0), (size_t)(b - a));
  }
  if (rc != FL_OK) {
    errno = to_errno(rc);
    return -1;
  }
  return count;
}

static s64 d_pwrite(struct ntfs_device *dev, const void *buf, s64 count, s64 off) {
  ntfs_dev *d = st(dev);
  if (count < 0 || off < 0) {
    errno = EINVAL;
    return -1;
  }
  if (!d->writable || NDevReadOnly(dev)) {
    errno = EROFS;
    return -1;
  }
  if (d->err != FL_OK) {
    errno = to_errno(d->err);
    return -1;
  }
  uint64_t size = fl_bdev_size_bytes(d->bd);
  if ((uint64_t)off > size || (uint64_t)count > size - (uint64_t)off) {
    fail(d, FL_EIO); /* volume plus grand que le disque : l'opération en cours resterait à moitié faite */
    errno = EIO;
    return -1;
  }
  if (count == 0) return 0;
  NDevSetDirty(dev);
  int rc;
  if (d->direct) {
    rc = in_wb(d, (uint64_t)off, (uint64_t)count) ? drain_wb(d) : FL_OK;
    if (rc == FL_OK) rc = write_now(d, buf, (uint64_t)count, (uint64_t)off);
  } else {
    rc = write_later(d, buf, (uint64_t)count, (uint64_t)off);
  }
  if (rc != FL_OK) {
    fail(d, rc);
    errno = to_errno(rc);
    return -1;
  }
  return count;
}

static s64 d_seek(struct ntfs_device *dev, s64 off, int whence) {
  ntfs_dev *d = st(dev);
  s64 base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? d->pos : (s64)fl_bdev_size_bytes(d->bd);
  if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
    errno = EINVAL;
    return -1;
  }
  if (base + off < 0) {
    errno = EINVAL;
    return -1;
  }
  d->pos = base + off;
  return d->pos;
}

static s64 d_read(struct ntfs_device *dev, void *buf, s64 count) {
  s64 r = d_pread(dev, buf, count, st(dev)->pos);
  if (r > 0) st(dev)->pos += r;
  return r;
}

static s64 d_write(struct ntfs_device *dev, const void *buf, s64 count) {
  s64 r = d_pwrite(dev, buf, count, st(dev)->pos);
  if (r > 0) st(dev)->pos += r;
  return r;
}

static int d_stat(struct ntfs_device *dev, struct stat *sb) {
  memset(sb, 0, sizeof *sb);
  sb->st_mode = S_IFBLK | 0600;
  sb->st_size = (off_t)fl_bdev_size_bytes(st(dev)->bd);
  sb->st_blksize = st(dev)->bd->sector_size;
  return 0;
}

static int d_ioctl(struct ntfs_device *dev, unsigned long req, void *arg) {
  ntfs_dev *d = st(dev);
  if (req == BLKGETSIZE64) {
    *(uint64_t *)arg = fl_bdev_size_bytes(d->bd);
    return 0;
  }
  if (req == BLKSSZGET) {
    *(int *)arg = (int)d->bd->sector_size;
    return 0;
  }
  errno = ENOTTY;
  return -1;
}

static struct ntfs_device_operations k_ops = {
    .open = d_open, .close = d_close, .seek = d_seek, .read = d_read, .write = d_write,
    .pread = d_pread, .pwrite = d_pwrite, .sync = d_sync, .stat = d_stat, .ioctl = d_ioctl,
};

/* --- interface ---------------------------------------------------------------------------------- */

int ntfs_dev_new(fl_bdev *bd, bool writable, ntfs_dev **out) {
  *out = NULL;
  ntfs_dev *d = calloc(1, sizeof *d);
  if (!d) return FL_ENOMEM;
  d->bd = bd;
  d->writable = writable && !bd->read_only;
  d->dev = ntfs_device_alloc("fluluks", 0, &k_ops, d);
  if (!d->dev) {
    free(d);
    return FL_ENOMEM;
  }
  *out = d;
  return FL_OK;
}

struct ntfs_device *ntfs_dev_device(ntfs_dev *d) { return d->dev; }

/* Abandonne ce qui attend (sans l'écrire). */
static void drop_all(ntfs_dev *d) {
  fl_wbuf_free(&d->wb);
  for (uint32_t i = 0; i < d->n; i++) d->free_sl[d->nfree++] = d->tab[i].slot;
  d->n = 0;
}

void ntfs_dev_free(ntfs_dev *d, bool device_too) {
  if (!d) return;
  if (device_too) ntfs_device_free(d->dev);
  drop_all(d);
  free(d->slab);
  free(d->tab);
  free(d->free_sl);
  free(d);
}

void ntfs_dev_set_writable(ntfs_dev *d, bool writable) {
  d->writable = writable && !d->bd->read_only;
  if (!d->writable) drop_all(d); /* lecture seule collante : plus rien n'est écrit */
}

int ntfs_dev_set_direct(ntfs_dev *d, bool direct) {
  int rc = direct && d->writable ? drain_all(d) : FL_OK;
  d->direct = direct;
  return rc;
}

int ntfs_dev_drain(ntfs_dev *d) { return d->writable ? drain_all(d) : FL_OK; }

int ntfs_dev_error(const ntfs_dev *d) { return d->err != FL_OK ? d->err : d->wb.err; }
