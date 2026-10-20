#ifndef FL_TT_TREE_MODEL_H
#define FL_TT_TREE_MODEL_H
/* Modèle en mémoire de l'arborescence attendue d'un volume : chemin -> type, taille, sha256, uid, gid,
 * mode (st_mode complet). Chemins absolus UTF-8 ("/" = racine), triés à la comparaison.
 *
 * Les tests appliquent au modèle (tm_apply_*) la même opération que celle faite sur le volume, puis
 * comparent : tm_check_fl par l'API du moteur, tm_check_debugfs par debugfs (indépendant de lwext4).
 *
 * Contenu : « sha256 » du modèle = sha256 de la suite des sha256 de chaque tranche de TM_CHUNK octets du
 * fichier entier (la dernière peut être plus courte ; une tranche entièrement nulle utilise un condensat
 * précalculé). Toute taille, y compris les fichiers creux géants. Dossier racine : mode 040755, uid 0,
 * gid 0 (comme le crée mkfs -d). "lost+found" à la racine n'est jamais compté.
 *
 * ATTENTION : tm_check_fl compare toujours le contenu entier. tm_check_debugfs ne le compare que pour les
 * fichiers d'au plus tm_set_debugfs_full_limit() octets (1 Gio par défaut, car debugfs « dump » écrit
 * le fichier entier sur disque). Au-delà, SEUL(S) la taille, uid, gid et mode sont comparés par debugfs :
 * le juge indépendant est FAIBLE pour ces fichiers (il ne voit pas une corruption de contenu). */

#include <stddef.h>
#include <stdint.h>

#include "fluluks/fs.h"

#define TM_CHUNK ((size_t)1 << 20)
#define TM_DEBUGFS_FULL_DEFAULT ((uint64_t)1 << 30)

typedef struct tm tm;

/* 1 : ne rien écrire sur stderr (vérifications négatives du banc) ; 0 par défaut. */
void tm_set_quiet(int quiet);

tm *tm_new(void); /* contient la racine */
tm *tm_clone(const tm *m);
/* Taille maximale d'un fichier dont tm_check_debugfs compare le contenu (défaut 1 Gio). */
void tm_set_debugfs_full_limit(uint64_t limit);
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

/* Écrit le modèle dans `fichier`, une ligne par entrée, triée par chemin, champs séparés par des
 * tabulations : type (d ou f), taille, condensat (hexadécimal, voir plus haut ; « - » pour un
 * dossier), uid, gid, mode (octal, st_mode complet), chemin. Lu par
 * tools/fixtures/verify_rw_with_kernel.sh. 0 ou -1. */
int tm_write_manifest(const tm *m, const char *fichier);

/* Compare le volume monté au modèle. Renvoie le nombre d'écarts (0 = identique) ; chacun est décrit
 * sur stderr (dix premiers). Un écart de contenu, un fichier en trop ou manquant comptent. */
int tm_check_fl(const tm *m, fl_fs *fs);
/* Idem par debugfs sur l'image `img` (hors moteur). -1 si debugfs est inutilisable. */
int tm_check_debugfs(const tm *m, const char *img);
/* FAT (phase 4) : uid 0, gid 0, mode 0100644 (fichiers) et 040755 (dossiers) partout. */
void tm_set_fat_attrs(tm *m);
/* Manifeste du modèle comparé, octet pour octet, à celui de fatjuge.py (hors moteur) sur `img`.
 * Nombre de lignes différentes (dix premières sur stderr), ou -1 si le juge est inutilisable. */
int tm_check_fat_judge(const tm *m, const char *img);
/* NTFS (phase 5) : mêmes droits que FAT (fichiers 0100644, dossiers 040755, uid et gid 0). */
void tm_set_ntfs_attrs(tm *m);
/* ntfsjuge.py check (cohérence, hors moteur) puis manifeste de ntfsjuge.py comparé au modèle : nombre de
 * lignes différentes, plus 1 si check a trouvé un défaut (défauts sur stderr), ou -1 si le juge est
 * inutilisable. */
int tm_check_ntfs_judge(const tm *m, const char *img);

#endif
