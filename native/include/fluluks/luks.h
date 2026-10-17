#ifndef FLULUKS_LUKS_H
#define FLULUKS_LUKS_H

#include <stddef.h>

#include "fluluks/blockdev.h"

typedef struct fl_luks fl_luks;

/* Lit et valide l'en-tête. FL_ENOTSUP si ce n'est pas du LUKS (ou version inconnue),
 * FL_ECORRUPT si l'en-tête est incohérent. `dev` doit rester ouvert tant que `l` ou un
 * périphérique déchiffré en dépend. */
int fl_luks_open(fl_bdev *dev, fl_luks **out);
/* 1 ou 2. */
int fl_luks_version(const fl_luks *l);
/* Ex. "aes-xts-plain64". Valable jusqu'à fl_luks_close. */
const char *fl_luks_cipher(const fl_luks *l);
/* Essaie chaque keyslot actif ; FL_EBADPASS si aucun ne convient (*decrypted = NULL).
 * Le bdev rendu emprunte `dev` et possède sa propre copie de la clé maître (mémoire
 * sécurisée). Ordre de fermeture : fl_bdev_close(decrypted), puis fl_luks_close(l),
 * puis fermeture de `dev`. */
int fl_luks_unlock(fl_luks *l, const void *pass, size_t passlen, fl_bdev **decrypted);
/* Efface tout secret ; NULL accepté. */
void fl_luks_close(fl_luks *l);

#endif
