#ifndef FLULUKS_JNI_HANDLES_H
#define FLULUKS_JNI_HANDLES_H

/* Moteur à poignées du pont JNI (sans jni.h : compilé aussi dans les tests PC).
 *
 * Poignées : entiers 64 bits > 0, (génération << 16) | indice, dans une table de FJ_MAX entrées typées
 * (disque, volume, fichier). Une poignée périmée, inconnue, négative ou d'un autre type → FL_EINVAL,
 * sans jamais déréférencer l'objet. Toute valeur < 0 renvoyée est un code fl_err.
 *
 * Fils d'exécution : tout est sûr entre fils. La table est protégée par un mutex, jamais tenu pendant
 * une opération longue (déverrouillage Argon2, lecture USB). Chaque opération prend une référence sur
 * l'objet visé ; une fermeture marque l'entrée « en fermeture » (plus aucune nouvelle opération ne
 * l'atteint, FL_EINVAL), attend la fin des opérations en cours, puis libère l'objet. Fermer un disque
 * démonte d'abord de force ses volumes ; démonter de force ferme d'abord les fichiers du volume. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "json_out.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FJ_MAX 1024u
/* Plus grande lecture servie par un seul fj_pread (au-delà : lecture partielle). */
#define FJ_PREAD_MAX (4u * 1024u * 1024u)
/* Nombre maximal d'entrées rendues par fj_list (au-delà : FL_ENOMEM). */
#define FJ_LIST_MAX 262144u

/* Image disque (secteurs de 512 octets), avec le cache 64 Kio × 256 par-dessus ; read_only = false
 * pour les tests d'écriture. */
int64_t fj_open_image(const char *path, bool read_only);
/* Disque USB (usbdevfs, LUN 0) ouvert en lecture-écriture (MODE SENSE décide de la protection) + cache
 * 64 Kio × 256. Le fd reste à l'appelant. FL_ENOTSUP hors Linux/Android. */
int64_t fj_open_usb(int fd, int iface, int ep_in, int ep_out);
/* Démonte de force les volumes du disque, puis ferme toute la pile (transport USB en dernier). */
int fj_close_disk(int64_t disk);

/* JSON du disque dans *out (voir task-6-brief) ; renvoie FL_OK, ou un fl_err (out non défini). */
int fj_scan(int64_t disk, fl_json *out);
/* Ouvre la partition d'indice part_index : tranche, fl_probe, déverrouillage LUKS si besoin
 * (pass peut être NULL pour un volume en clair), puis ext4, en écriture si want_rw et si le système de
 * fichiers le permet (sinon lecture seule, raison dans fj_statfs ; want_rw = false → RO_USER, disque
 * protégé → RO_WP). Poignée volume ou fl_err. */
int64_t fj_mount(int64_t disk, uint32_t part_index, const void *pass, size_t passlen, bool want_rw);
/* Sans force : FL_EBUSY si un fichier est ouvert, ou si le disque parent est en cours de fermeture
 * (c'est lui qui démontera le volume). Un enregistrement d'enfant (fj_mount, fj_open_file) sous un
 * parent en cours de fermeture rend aussi FL_EBUSY. Avec force : ferme les fichiers (en attendant les
 * lectures en cours) puis démonte. Un volume en écriture est synchronisé puis démonté (journal arrêté,
 * SYNCHRONIZE CACHE) ; disque mort : tout est libéré quand même et l'erreur est rendue.
 * Contrat d'erreur (sans force) : seul FL_EBUSY laisse le volume intact et utilisable (on peut réessayer).
 * Toute autre erreur (EIO…) est rendue APRÈS destruction du volume : système de fichiers libéré, clé
 * effacée, poignée invalide (un second appel rend FL_EINVAL). L'appelant doit alors oublier le volume ;
 * des données ont pu ne pas être écrites. */
int fj_unmount(int64_t vol, bool force);

int fj_statfs(int64_t vol, fl_json *out);
int fj_stat(int64_t vol, const char *path, fl_json *out);
/* Tableau trié par nom (ordre des octets). */
int fj_list(int64_t vol, const char *path, fl_json *out);

/* flags : FL_O_* (fs.h). */
int64_t fj_open_file(int64_t vol, const char *path, int flags);
/* Octets lus (<= min(len, FJ_PREAD_MAX)), 0 = fin, < 0 = fl_err. */
int64_t fj_pread(int64_t file, void *buf, size_t len, uint64_t off);
/* Octets écrits (= len, len <= FJ_PREAD_MAX sinon FL_EINVAL) ou fl_err. */
int64_t fj_pwrite(int64_t file, const void *buf, size_t len, uint64_t off);
int fj_truncate(int64_t file, uint64_t size);
/* Un fichier écrit est synchronisé à la fermeture (code rendu). */
int fj_close_file(int64_t file);
int fj_mkdir(int64_t vol, const char *path);
int fj_unlink(int64_t vol, const char *path);
int fj_rmdir(int64_t vol, const char *path);
int fj_rename(int64_t vol, const char *from, const char *to);
int fj_sync(int64_t vol);

/* Formate tout le disque (phase 8). json : {"fs": "fat32"|"exfat"|"ext4"|"ntfs", "encrypted": bool,
 * "label": texte, "kdf_mem_kib": Kio d'Argon2 (1 Gio par défaut), "uid"/"gid": propriétaire de la racine
 * ext4 (1000 par défaut)}. FL_EBUSY si un volume du disque est ouvert (et, pendant le formatage, fj_mount
 * sur ce disque rend FL_EBUSY), FL_EROFS si le disque est en lecture seule, FL_EINVAL pour des options
 * invalides ; sinon le code de fl_format. progress (facultatif) : pourcentage croissant, depuis ce fil. */
int fj_format(int64_t disk, const char *json, const void *pass, size_t passlen, void (*progress)(int pct, void *arg),
              void *arg);

/* Références et état « en fermeture » d'une entrée (tests) ; FL_EINVAL si la poignée est invalide. */
int fj_debug_slot(int64_t h, unsigned *refs, bool *closing);
/* Tests : fn(arg) est appelée par fj_mount, référence du disque tenue, juste avant d'enregistrer le
 * volume. NULL retire le crochet. À poser avant de lancer des fils, à retirer après leur fin. */
void fj_debug_set_mount_hook(void (*fn)(void *), void *arg);
/* Nombre d'entrées occupées (tests). */
unsigned fj_debug_used(void);
/* Vidages (fl_bdev_flush) reçus par le disque sous son cache, ou fl_err (tests). */
int64_t fj_debug_flushes(int64_t disk);

#ifdef __cplusplus
}
#endif

#endif
