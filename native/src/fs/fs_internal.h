#ifndef FL_FS_INTERNAL_H
#define FL_FS_INTERNAL_H

/* Aiguillage de l'API fichiers vers chaque système de fichiers.
 *
 * fs.c vérifie les arguments (pointeurs, chemins, drapeaux) et la lecture seule avant
 * d'appeler une opération : les implémentations reçoivent des chemins déjà validés
 * (absolus, sans composant vide, "." ni "..", ≤ 4095 octets, composants ≤ 255 octets).
 * Une opération d'écriture NULL signifie « non prise en charge » (FL_EROFS). */

#include <stdatomic.h>

#include "fluluks/fs.h"

typedef int (*fl_readdir_cb)(void *ctx, const fl_dirent *e);

typedef struct {
  /* Remplit total_bytes, free_bytes, label, needs_recovery (fs.c remplit le reste). */
  int (*statfs)(fl_fs *fs, fl_statfs *out);
  int (*stat)(fl_fs *fs, const char *path, fl_stat *out);
  int (*readdir)(fl_fs *fs, const char *path, fl_readdir_cb cb, void *ctx);
  /* Ouverture d'un fichier ordinaire ; *out alloué par l'implémentation. */
  int (*open)(fl_fs *fs, const char *path, int flags, fl_file **out);
  /* off < taille et len > 0 ne sont pas garantis : à vérifier par l'implémentation. */
  int64_t (*pread)(fl_file *f, void *buf, size_t len, uint64_t off);
  int (*close)(fl_file *f);
  /* Écriture ; NULL si le système de fichiers ne l'implémente pas. */
  int64_t (*pwrite)(fl_file *f, const void *buf, size_t len, uint64_t off);
  int (*truncate)(fl_file *f, uint64_t size);
  int (*mkdir)(fl_fs *fs, const char *path);
  int (*unlink)(fl_fs *fs, const char *path);
  int (*rmdir)(fl_fs *fs, const char *path);
  int (*rename)(fl_fs *fs, const char *from, const char *to);
  int (*sync)(fl_fs *fs);
  /* FL_EINVAL si des fichiers sont ouverts (rien n'est alors libéré) ; sinon libère fs. */
  int (*unmount)(fl_fs *fs);
} fl_fs_ops;

struct fl_fs {
  const fl_fs_ops *ops;
  fl_bdev *dev;
  /* Peut passer à vrai en cours de session (lecture seule collante, fl_fs_go_read_only), lu sans
   * verrou ; ro_reason est écrit avant, et n'est plus modifié ensuite. */
  _Atomic bool read_only;
  /* Lecture seule prise en cours de session (et non au montage) : fl_fs_sync rend alors FL_EIO, des
   * données ont pu ne pas être écrites. Posé une seule fois, par fl_fs_go_read_only. */
  _Atomic bool went_ro;
  char ro_reason[128];
  void *priv;
};

struct fl_file {
  fl_fs *fs;
  int flags;
  void *priv;
};

/* Montage d'un système de fichiers particulier : alloue *out (ops, dev, priv remplis). */
typedef int (*fl_fs_mount_fn)(fl_bdev *dev, bool want_rw, fl_fs **out);

int fl_ext4_mount(fl_bdev *dev, bool want_rw, fl_fs **out);
/* FAT12, FAT16, FAT32 et exFAT par FatFs (fat_glue.c, phase 4). */
int fl_fat_mount(fl_bdev *dev, bool want_rw, fl_fs **out);
/* NTFS par libntfs-3g (ntfs_glue.c, phase 5). */
int fl_ntfs_mount(fl_bdev *dev, bool want_rw, fl_fs **out);

/* Lecture seule collante : pose la raison puis read_only (une seule fois, même entre fils : les
 * suivantes sont sans effet). */
void fl_fs_go_read_only(fl_fs *fs, const char *reason);

/* Heure des horodatages (secondes Unix) : l'horloge de fl_fs_set_clock, sinon time(). */
int64_t fl_fs_now(void);

#endif
