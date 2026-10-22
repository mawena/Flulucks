#ifndef FL_FORMAT_INTERNAL_H
#define FL_FORMAT_INTERNAL_H

/* Nombre de caractères (unités UTF-16) d'une étiquette UTF-8 valide sans caractère de contrôle ; -1 sinon
 * (mkfs_fat.c). */
int fl_label_utf16_units(const char *s);

#endif
