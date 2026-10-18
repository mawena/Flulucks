#ifndef FL_FS_WBUF_H
#define FL_FS_WBUF_H

/* Tampon d'écriture différée d'un fichier (interne, sans dépendance au système de fichiers).
 *
 * Un seul intervalle contigu [off, off + len) d'au plus FL_WBUF_CAP octets : les écritures qui le
 * prolongent exactement y sont copiées ; toute autre écriture le vide d'abord (rappel fn, chemin
 * d'écriture réel du système de fichiers) puis recommence. Une écriture d'au moins FL_WBUF_CAP octets
 * est passée directement à fn (après vidage). La mémoire est allouée à la première écriture tamponnée
 * et libérée à chaque vidage ; si l'allocation échoue, l'écriture est passée directement à fn.
 *
 * Erreur différée : un vidage qui échoue a perdu des données déjà acquittées. Son erreur est mémorisée
 * (err, collante) ; fl_wbuf_write la rend alors à chaque appel, sans rien écrire, et l'appelant la rend
 * aux opérations suivantes sur le fichier (fermeture, synchronisation). Un échec de l'écriture directe
 * des données de l'appelant est seulement rendu (rien n'a été acquitté).
 *
 * Aucun verrou : l'appelant sérialise (le glue appelle sous son verrou global). Codes FL_*. */

#include <stddef.h>
#include <stdint.h>

#define FL_WBUF_CAP (4u << 20)

/* Écrit [p, p + len) à l'offset off par le chemin réel ; FL_OK seulement si tout est écrit. */
typedef int (*fl_wbuf_flush_fn)(void *ctx, const void *p, size_t len, uint64_t off);

typedef struct {
  uint8_t *data; /* NULL si vide */
  uint64_t off;
  size_t len;
  int err; /* première erreur d'un vidage (collante), FL_OK sinon */
} fl_wbuf;

/* FL_OK (données tamponnées ou écrites), l'erreur collante, celle du vidage (devenue collante) ou celle
 * de l'écriture directe. len = 0 : FL_OK sans effet. */
int fl_wbuf_write(fl_wbuf *b, const void *p, size_t len, uint64_t off, fl_wbuf_flush_fn fn, void *ctx);
/* Vide le tampon (sans effet s'il est vide) : FL_OK ou l'erreur de ce vidage (mémorisée dans err). Les
 * données sont abandonnées et la mémoire libérée dans tous les cas. */
int fl_wbuf_flush(fl_wbuf *b, fl_wbuf_flush_fn fn, void *ctx);
/* Fin des données tamponnées (off + len), 0 si vide. */
uint64_t fl_wbuf_end(const fl_wbuf *b);
/* Abandonne les données tamponnées (sans les écrire) et libère la mémoire ; err est conservée. */
void fl_wbuf_free(fl_wbuf *b);

#endif
