#define _DEFAULT_SOURCE
#include "fluluks/secure.h"

#include <string.h>

#if defined(__GLIBC__) || (defined(__ANDROID_API__) && __ANDROID_API__ >= 28)
#include <strings.h>
void fl_memzero(void *p, size_t n) {
  if (p && n) explicit_bzero(p, n);
}
#else
/* Repli portable : memset via un pointeur volatil (le compilateur ne peut pas
 * l'éliminer), puis une barrière mémoire. */
static void *(*volatile fl_memset_v)(void *, int, size_t) = memset;

void fl_memzero(void *p, size_t n) {
  if (!p || !n) return;
  fl_memset_v(p, 0, n);
  __asm__ __volatile__("" : : "r"(p) : "memory");
}
#endif
