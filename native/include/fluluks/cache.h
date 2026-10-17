#ifndef FLULUKS_CACHE_H
#define FLULUKS_CACHE_H

#include <stdint.h>

#include "fluluks/blockdev.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Nombre maximal de blocs (bloc manquant compris) lus en un seul appel parent lors d'une lecture séquentielle :
 * un défaut sur le bloc i est séquentiel si le bloc i-1 est en cache ou en chargement. */
#define FL_CACHE_READAHEAD 4u
/* Taille maximale d'un bloc de cache. */
#define FL_CACHE_MAX_BLOCK_BYTES (1024u * 1024u)

/* Cache de blocs alignés (LRU) avec lecture anticipée, au-dessus de `parent` (qui n'est pas possédé et
 * doit survivre au cache). Le cache a les mêmes sector_size / sector_count / read_only que le parent.
 * block_bytes : puissance de 2, multiple de parent->sector_size, <= FL_CACHE_MAX_BLOCK_BYTES.
 * nblocks >= FL_CACHE_READAHEAD + 1. Sinon FL_EINVAL.
 * Sûr pour plusieurs fils ; le parent est lu hors du verrou du cache. Écriture : transmise au parent,
 * puis les blocs en cache sont mis à jour ou invalidés. Les erreurs du parent ne sont pas mémorisées.
 * fl_bdev_close() ne ferme pas le parent ; aucun fil ne doit utiliser le cache pendant la fermeture. */
int fl_cache_bdev_new(fl_bdev *parent, uint32_t block_bytes, uint32_t nblocks, fl_bdev **out);

#ifdef __cplusplus
}
#endif

#endif
