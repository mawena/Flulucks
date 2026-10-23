#ifndef FL_ARC_INTERNAL_H
#define FL_ARC_INTERNAL_H
#include <locale.h>

/* Passe le fil dans une locale UTF-8 le temps d'appels à libarchive ; rend la locale à remettre. */
locale_t fl_arc_utf8_begin(void);
void fl_arc_utf8_end(locale_t old);

#endif
