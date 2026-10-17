#ifndef FLULUKS_FS_H
#define FLULUKS_FS_H

/* API fichiers unifiée, commune à tous les systèmes de fichiers (ext4 en phase 1 ;
 * FAT/exFAT et NTFS viendront s'y brancher).
 *
 * Chemins : absolus, en UTF-8, "/" pour la racine, sans "/" final, sans composant vide,
 * "." ni "..", 4095 octets au plus, 255 octets au plus par composant ; sinon FL_EINVAL.
 *
 * Fils d'exécution : chaque appel est sûr vis-à-vis des autres volumes et des autres
 * appels sur le même volume (sérialisation interne). Démonter un volume pendant qu'un
 * autre fil l'utilise reste interdit. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fluluks/blockdev.h"
#include "fluluks/probe.h"

typedef enum { FL_FT_FILE = 1, FL_FT_DIR = 2, FL_FT_SYMLINK = 3, FL_FT_OTHER = 4 } fl_ftype;

typedef struct {
  fl_ftype type;
  uint64_t size;
  int64_t mtime;             /* secondes depuis l'époque Unix */
  uint32_t mode, uid, gid;   /* mode : type + droits, comme st_mode */
} fl_stat;

typedef struct {
  char name[256];            /* UTF-8, terminé par NUL */
  fl_stat st;
} fl_dirent;

typedef struct {
  uint64_t total_bytes, free_bytes;
  char label[64];            /* terminé par NUL ; "" si absent */
  bool read_only;
  char ro_reason[128];       /* "" si écriture possible */
  bool needs_recovery;       /* journal non rejoué : le contenu peut être en retard */
} fl_statfs;

typedef struct fl_fs fl_fs;
typedef struct fl_file fl_file;

enum { FL_O_RDONLY = 0, FL_O_RDWR = 1, FL_O_CREAT = 2, FL_O_TRUNC = 4 };

/* Monte `dev` (qui doit rester ouvert jusqu'au démontage). kind non géré → FL_ENOTSUP.
 * Phase 1 : toujours en lecture seule, même si want_rw est vrai. */
int fl_fs_mount(fl_bdev *dev, fl_content kind, bool want_rw, fl_fs **out);
int fl_fs_statfs(fl_fs *fs, fl_statfs *out);
/* FL_ENOENT si absent. */
int fl_fs_stat(fl_fs *fs, const char *path, fl_stat *out);
/* Sans "." ni ".." ; un cb non nul arrête le parcours et sa valeur est renvoyée.
 * FL_ENOTDIR si path n'est pas un dossier. Le callback peut rappeler l'API. */
int fl_fs_readdir(fl_fs *fs, const char *path, int (*cb)(void *ctx, const fl_dirent *e), void *ctx);
/* Fichiers ordinaires seulement : FL_EISDIR pour un dossier, FL_ENOTSUP pour un lien
 * symbolique ou un fichier spécial. RDWR/CREAT/TRUNC sur un volume en lecture seule → FL_EROFS. */
int fl_fs_open(fl_fs *fs, const char *path, int flags, fl_file **out);
/* Octets lus, 0 = fin, <0 = fl_err. Indépendant de tout curseur : sûr en appels entrelacés. */
int64_t fl_file_pread(fl_file *f, void *buf, size_t len, uint64_t off);
int64_t fl_file_pwrite(fl_file *f, const void *buf, size_t len, uint64_t off);
int fl_file_truncate(fl_file *f, uint64_t size);
/* NULL accepté. */
int fl_file_close(fl_file *f);
int fl_fs_mkdir(fl_fs *fs, const char *path);
int fl_fs_unlink(fl_fs *fs, const char *path);
int fl_fs_rmdir(fl_fs *fs, const char *path);
int fl_fs_rename(fl_fs *fs, const char *from, const char *to);
/* FL_OK sans effet sur un volume en lecture seule. */
int fl_fs_sync(fl_fs *fs);
/* NULL accepté. FL_EINVAL (rien n'est démonté) tant qu'un fichier reste ouvert. */
int fl_fs_unmount(fl_fs *fs);

#endif
