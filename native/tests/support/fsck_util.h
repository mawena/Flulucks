#ifndef FL_TT_FSCK_UTIL_H
#define FL_TT_FSCK_UTIL_H
/* Juges externes du banc de non-corruption (e2fsck, debugfs), réservés au PC.
 * Les outils sont lancés par posix_spawnp, l'environnement réduit à LC_ALL=C (et PATH), sans shell. */

#include <stddef.h>
#include <stdint.h>

/* 1 si e2fsck, debugfs et dumpe2fs sont disponibles (PC), 0 sinon (Android, ou outil absent). */
int fu_tools_available(void);
/* Codes propres au banc (jamais renvoyés par e2fsck lui-même). */
#define FU_RC_TIMEOUT (-2)   /* outil tué après le délai */
#define FU_RC_JOURNAL 64     /* journal non rejoué : « e2fsck -fn » dit 0 mais l'image n'est pas à jour */
#define FU_RC_REPAIRED 65    /* « e2fsck -fy » a fait des réparations autres que bénignes */

/* Lance argv (argv[0] cherché dans PATH) ; stdout et stderr sont copiés (tronqués, toujours terminés
 * par NUL) dans out. Renvoie le code de sortie, 128 + signal si tué, -1 si le lancement échoue,
 * FU_RC_TIMEOUT si l'outil dépasse 900 s (il est alors tué par SIGKILL). */
int fu_run(char *const argv[], char *out, size_t cap);
/* Idem avec un délai en secondes. */
int fu_run_timeout(char *const argv[], char *out, size_t cap, unsigned seconds);
/* Dossier temporaire du banc, sous build/ (créé au besoin) ; 0 si ok. */
int fu_tmpdir(char *out, size_t cap);
/* e2fsck -fn <img> ; renvoie le code de sortie, et copie la sortie (tronquée) dans out.
 * Un journal à rejouer n'est PAS propre : « e2fsck -fn » l'ignore (« skipping journal recovery ») et
 * sort avec 0 ; la fonction renvoie alors FU_RC_JOURNAL si la sortie le dit ou si dumpe2fs -h montre
 * needs_recovery. */
int fu_e2fsck_n(const char *img, char *out, size_t cap);
/* e2fsck -fy puis -fn. *replayed = 1 si e2fsck a rejoué le journal ; *fy_rc = code de sortie du -fy ;
 * fy_out (tronquée, peut être NULL) = sortie du -fy ; out = sortie du -fn.
 * Renvoie 0 seulement si (a) le -fn final renvoie 0 (au sens de fu_e2fsck_n) ET (b) le -fy n'a fait que
 * des réparations bénignes (fu_fy_repairs_benign) avec un code < 4. Sinon : le code du -fn s'il est non
 * nul, sinon FU_RC_REPAIRED. */
int fu_e2fsck_recover(const char *img, int *replayed, int *fy_rc, char *fy_out, size_t fy_cap, char *out,
                      size_t cap);
/* 1 si toutes les réparations de la sortie d'un « e2fsck -fy » sont bénignes : rejeu du journal et
 * compteurs globaux du superbloc (« Free blocks count wrong (» / « Free inodes count wrong (»,
 * pas les variantes « for group #N »). 0 dès qu'une autre réparation apparaît. */
int fu_fy_repairs_benign(const char *fy_out);
/* Contenu d'un fichier lu par debugfs (dump), indépendant de lwext4. Taille, ou <0 (absent, plus grand
 * que cap, erreur). */
int64_t fu_debugfs_cat(const char *img, const char *path, void *buf, size_t cap);
/* Champ d'inode lu par debugfs ("uid", "gid", "mode" = bits de permission, "size", "mtime", "ctime",
 * "atime", "crtime" = secondes depuis l'époque, 64 bits : bits d'époque du champ « extra » inclus ;
 * négatif refusé). 0 si ok. */
int fu_debugfs_inode_field(const char *img, const char *path, const char *field, uint64_t *out);
/* Copie creuse de src vers un fichier temporaire sous build/ ; chemin dans out. L'appelant le supprime. */
int fu_copy_image(const char *src, char *out, size_t cap);
/* Copie d'image LUKS + ext4 : déchiffre l'ext4 (par le moteur, sans root) vers un fichier temporaire
 * creux pour e2fsck/debugfs ; chemin dans out. L'appelant le supprime. */
int fu_extract_luks_ext4(const char *luks_img, const char *pass_file, char *out, size_t cap);

/* --- FAT / exFAT (phase 4) ------------------------------------------------------------------- */
/* 1 si mkfs.vfat, fsck.fat, mkfs.exfat, fsck.exfat, mcopy et python3 sont disponibles (PC), 0 sinon. */
int fu_fat_tools_available(void);
/* fsck.fat -n ou fsck.exfat -n selon l'image (secteur de démarrage) ; code de sortie, ou 1 si le code vaut
 * 0 mais que la sortie contient une ligne d'erreur. Sortie (tronquée) dans out. */
int fu_fsck_fat_n(const char *img, char *out, size_t cap);
/* fsck.fat -a ou fsck.exfat -y, puis fu_fsck_fat_n : rend le code de ce dernier. */
int fu_fsck_fat_repair(const char *img, char *out, size_t cap);
/* python3 -I tools/fixtures/fatjuge.py <cmd> <img> [arg] ; stdout (tronqué) dans out. Code de sortie. */
int fu_fatjuge(char *out, size_t cap, const char *cmd, const char *img, const char *arg);
/* fatjuge.py manifest <img> vers out_path : 0, 3 (structure invalide), ou -1. */
int fu_fat_manifest(const char *img, const char *out_path);
/* fatjuge.py free <img> : octets libres, ou -1. */
int64_t fu_fat_free(const char *img);

/* --- NTFS (phase 5) --------------------------------------------------------------------------- */
/* 1 si mkntfs, ntfsfix et python3 sont disponibles (PC), 0 sinon. */
int fu_ntfs_tools_available(void);
/* python3 -I tools/fixtures/ntfsjuge.py <cmd> <img> [arg] ; stdout (tronqué) dans out. Code de sortie. */
int fu_ntfsjuge(char *out, size_t cap, const char *cmd, const char *img, const char *arg);
/* ntfsjuge.py manifest <img> vers out_path : 0, 3 (structure invalide), ou -1. */
int fu_ntfs_manifest(const char *img, const char *out_path);
/* ntfsjuge.py free <img> : octets libres, ou -1. */
int64_t fu_ntfs_free(const char *img);
/* ntfsjuge.py flags <img> : drapeaux de $Volume, ou -1. */
int fu_ntfs_flags(const char *img);
/* ntfsfix -n <img> : code de sortie ; sortie (tronquée) dans out. */
int fu_ntfsfix_n(const char *img, char *out, size_t cap);

#endif
