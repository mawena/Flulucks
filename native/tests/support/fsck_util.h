#ifndef FL_TT_FSCK_UTIL_H
#define FL_TT_FSCK_UTIL_H
/* Juges externes du banc de non-corruption (e2fsck, debugfs), réservés au PC.
 * Les outils sont lancés par posix_spawnp, l'environnement réduit à LC_ALL=C (et PATH), sans shell. */

#include <stddef.h>
#include <stdint.h>

/* 1 si e2fsck, debugfs et dumpe2fs sont disponibles (PC), 0 sinon (Android, ou outil absent). */
int fu_tools_available(void);
/* Lance argv (argv[0] cherché dans PATH) ; stdout et stderr sont copiés (tronqués, toujours terminés
 * par NUL) dans out. Renvoie le code de sortie, 128 + signal si tué, -1 si le lancement échoue. */
int fu_run(char *const argv[], char *out, size_t cap);
/* Dossier temporaire du banc, sous build/ (créé au besoin) ; 0 si ok. */
int fu_tmpdir(char *out, size_t cap);
/* e2fsck -fn <img> ; renvoie le code de sortie, et copie la sortie (tronquée) dans out. */
int fu_e2fsck_n(const char *img, char *out, size_t cap);
/* e2fsck -fy puis -fn ; *replayed = 1 si e2fsck a rejoué le journal ; renvoie le code du -fn. */
int fu_e2fsck_recover(const char *img, int *replayed, char *out, size_t cap);
/* Contenu d'un fichier lu par debugfs (dump), indépendant de lwext4. Taille, ou <0 (absent, plus grand
 * que cap, erreur). */
int64_t fu_debugfs_cat(const char *img, const char *path, void *buf, size_t cap);
/* Champ d'inode lu par debugfs ("uid", "gid", "mode" = bits de permission, "size", "mtime", "ctime",
 * "atime", "crtime" = secondes). 0 si ok. */
int fu_debugfs_inode_field(const char *img, const char *path, const char *field, uint64_t *out);
/* Copie creuse de src vers un fichier temporaire sous build/ ; chemin dans out. L'appelant le supprime. */
int fu_copy_image(const char *src, char *out, size_t cap);
/* Copie d'image LUKS + ext4 : déchiffre l'ext4 (par le moteur, sans root) vers un fichier temporaire
 * creux pour e2fsck/debugfs ; chemin dans out. L'appelant le supprime. */
int fu_extract_luks_ext4(const char *luks_img, const char *pass_file, char *out, size_t cap);

#endif
