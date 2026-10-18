#ifndef FL_TT_TREE_MODEL_H
#define FL_TT_TREE_MODEL_H
/* Modèle en mémoire de l'arborescence attendue d'un volume : chemin -> type, taille, sha256, uid, gid,
 * mode (st_mode complet). Chemins absolus UTF-8 ("/" = racine), triés à la comparaison.
 *
 * Les tests appliquent au modèle (tm_apply_*) la même opération que celle faite sur le volume, puis
 * comparent : tm_check_fl par l'API du moteur, tm_check_debugfs par debugfs (indépendant de lwext4).
 *
 * Contenu : sha256 du fichier entier jusqu'à TM_BIG octets ; au-delà, sha256 des TM_EDGE premiers
 * octets suivis des TM_EDGE derniers (fichiers creux géants), et tm_check_debugfs ne compare que
 * taille, uid, gid et mode. Dossier racine : mode 040755, uid 0, gid 0 (comme le crée mkfs -d). "lost+found"
 * à la racine n'est jamais compté. */

#include <stddef.h>
#include <stdint.h>

#include "fluluks/fs.h"

#define TM_BIG ((uint64_t)64 << 20)
#define TM_EDGE ((size_t)1 << 20)

typedef struct tm tm;

/* 1 : ne rien écrire sur stderr (vérifications négatives du banc) ; 0 par défaut. */
void tm_set_quiet(int quiet);

tm *tm_new(void); /* contient la racine */
void tm_free(tm *m);
size_t tm_count(const tm *m);
/* Construit le modèle depuis un dossier source (celui donné à mkfs -d). 0 ou -1. */
int tm_from_dir(tm *m, const char *dir);

/* 0 ou -1 (chemin déjà pris, parent absent, mauvais type...). */
int tm_apply_mkdir(tm *m, const char *path, uint32_t uid, uint32_t gid, uint32_t perm);
/* Crée ou remplace le contenu d'un fichier (création, écriture, troncature : l'appelant donne le
 * contenu final). perm : bits de permission (le type est ajouté). */
int tm_apply_file(tm *m, const char *path, const void *data, size_t len, uint32_t uid, uint32_t gid,
                  uint32_t perm);
int tm_apply_unlink(tm *m, const char *path);
int tm_apply_rmdir(tm *m, const char *path); /* dossier vide seulement */
int tm_apply_rename(tm *m, const char *from, const char *to); /* déplace aussi les descendants */
int tm_apply_attrs(tm *m, const char *path, uint32_t uid, uint32_t gid, uint32_t perm);

/* Compare le volume monté au modèle. Renvoie le nombre d'écarts (0 = identique) ; chacun est décrit
 * sur stderr (dix premiers). Un écart de contenu, un fichier en trop ou manquant comptent. */
int tm_check_fl(const tm *m, fl_fs *fs);
/* Idem par debugfs sur l'image `img` (hors moteur). -1 si debugfs est inutilisable. */
int tm_check_debugfs(const tm *m, const char *img);

#endif
