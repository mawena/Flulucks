#define _DEFAULT_SOURCE
#include "fluluks/secure.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>


void *fl_secure_alloc(size_t n) {
  if (n == 0) n = 1;
  void *p = calloc(1, n);
  if (p) (void)mlock(p, n); /* au mieux : l'échec (RLIMIT_MEMLOCK) est toléré */
  return p;
}

void fl_secure_free(void *p, size_t n) {
  if (!p) return;
  if (n == 0) n = 1;
  explicit_bzero(p, n);
  (void)munlock(p, n);
  free(p);
}
