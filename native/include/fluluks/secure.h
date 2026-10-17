#ifndef FLULUKS_SECURE_H
#define FLULUKS_SECURE_H

#include <stddef.h>

/* Zone zéroïsée, verrouillée en mémoire (mlock) au mieux. NULL si échec. */
void *fl_secure_alloc(size_t n);
/* Efface (explicit_bzero), déverrouille et libère. NULL accepté. */
void fl_secure_free(void *p, size_t n);

#endif
