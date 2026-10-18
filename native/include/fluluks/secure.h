#ifndef FLULUKS_SECURE_H
#define FLULUKS_SECURE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Zone zéroïsée, verrouillée en mémoire (mlock) au mieux. NULL si échec. */
void *fl_secure_alloc(size_t n);
/* Efface (fl_memzero), déverrouille et libère. NULL accepté. */
void fl_secure_free(void *p, size_t n);

/* Met n octets à zéro sans que le compilateur puisse éliminer l'écriture.
 * explicit_bzero sur glibc et Android >= 28, repli memset volatil sinon. */
void fl_memzero(void *p, size_t n);

#ifdef __cplusplus
}
#endif

#endif
