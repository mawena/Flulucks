#ifndef FLULUKS_ARCHIVE_H
#define FLULUKS_ARCHIVE_H
/* Archives (phase 9) : lecture de ZIP, 7z, RAR4, RAR5, TAR, TAR.GZ, TAR.XZ, GZ et XZ seuls, écriture de ZIP,
 * sur libarchive. Une archive est hostile : le lecteur rend les noms tels quels (octets bruts), la sûreté
 * des chemins est décidée par l'appelant. Les noms sont lus dans une locale UTF-8 propre au fil. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Entrée à accès aléatoire : pread rend le nombre d'octets lus (0 à la fin), ou un code FL_E* négatif. */
typedef struct {
  void *ctx;
  int64_t size;
  int64_t (*pread)(void *ctx, void *buf, size_t len, uint64_t off);
} fl_arc_in;

typedef enum { FL_ARC_FILE = 1, FL_ARC_DIR, FL_ARC_SYMLINK, FL_ARC_HARDLINK, FL_ARC_OTHER } fl_arc_type;

typedef struct {
  char name[4096];  /* octets bruts de l'archive (UTF-8 s'ils le sont), terminés par NUL */
  size_t name_len;  /* 0 : nom vide ou trop long (l'entrée est alors FL_ARC_OTHER) */
  fl_arc_type type;
  int64_t size;     /* taille annoncée, -1 si inconnue (flux .gz ou .xz seul) */
  int64_t mtime;    /* secondes depuis l'époque Unix, 0 si inconnue */
  bool encrypted;   /* données ou métadonnées chiffrées : fl_arc_read échoue */
} fl_arc_entry;

typedef struct fl_arc_reader fl_arc_reader;

/* Ouvre l'archive (lit déjà le premier en-tête). FL_ENOTSUP : format inconnu (un fichier quelconque n'est
 * pas une archive « brute » : il faut un filtre gzip, xz ou lzma). `in` doit vivre jusqu'à fl_arc_close. */
int fl_arc_open(const fl_arc_in *in, fl_arc_reader **out);
/* Entrée suivante : 1 entrée, 0 fin de l'archive, < 0 erreur (FL_ECORRUPT, ou l'erreur de l'entrée). */
int fl_arc_next(fl_arc_reader *r, fl_arc_entry *e);
/* Données de l'entrée courante : octets lus, 0 à la fin de l'entrée, < 0 erreur (FL_ENOTSUP si chiffrée). */
int64_t fl_arc_read(fl_arc_reader *r, void *buf, size_t len);
/* Nom du format (« ZIP 2.0 (deflation) », « 7-Zip », « RAR5 »…), valable jusqu'à l'entrée suivante. */
const char *fl_arc_format(const fl_arc_reader *r);
void fl_arc_close(fl_arc_reader *r);

/* Sortie séquentielle : write écrit tout `len` (FL_OK) ou rend un code FL_E* négatif. */
typedef struct {
  void *ctx;
  int (*write)(void *ctx, const void *buf, size_t len);
} fl_arc_out;

typedef struct fl_arc_writer fl_arc_writer;

/* ZIP : deflate niveau 6 ou stockage (par entrée), noms UTF-8 avec le drapeau UTF-8, ZIP64 au besoin. */
int fl_arcw_open(const fl_arc_out *out, fl_arc_writer **w);
/* Nouvelle entrée (FL_ARC_FILE de `size` octets, ou FL_ARC_DIR). FL_EINVAL : nom vide, absolu ou avec un
 * segment vide, « . » ou « .. », autre type, entrée précédente incomplète. */
int fl_arcw_add(fl_arc_writer *w, const char *name_utf8, fl_arc_type type, int64_t size, int64_t mtime, bool store);
/* Données de l'entrée courante ; FL_EINVAL au-delà de la taille annoncée. */
int fl_arcw_write(fl_arc_writer *w, const void *buf, size_t len);
/* commit : termine l'archive (répertoire central) ; sinon abandon, plus rien n'est écrit. Libère `w`. */
int fl_arcw_close(fl_arc_writer *w, bool commit);

#ifdef __cplusplus
}
#endif

#endif
