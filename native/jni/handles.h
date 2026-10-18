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

/* Image disque en lecture seule (secteurs de 512 octets), avec le cache 64 Kio × 256 par-dessus. */
int64_t fj_open_image(const char *path);
/* Disque USB (usbdevfs, LUN 0, lecture seule) + cache 64 Kio × 256. Le fd reste à l'appelant.
 * FL_ENOTSUP hors Linux/Android. */
int64_t fj_open_usb(int fd, int iface, int ep_in, int ep_out);
/* Démonte de force les volumes du disque, puis ferme toute la pile (transport USB en dernier). */
int fj_close_disk(int64_t disk);

/* JSON du disque dans *out (voir task-6-brief) ; renvoie FL_OK, ou un fl_err (out non défini). */
int fj_scan(int64_t disk, fl_json *out);
/* Ouvre la partition d'indice part_index : tranche, fl_probe, déverrouillage LUKS si besoin
 * (pass peut être NULL pour un volume en clair), puis ext4 en lecture seule. Poignée volume ou fl_err. */
int64_t fj_mount(int64_t disk, uint32_t part_index, const void *pass, size_t passlen);
/* Sans force : FL_EBUSY si un fichier est ouvert, ou si le disque parent est en cours de fermeture
 * (c'est lui qui démontera le volume). Un enregistrement d'enfant (fj_mount, fj_open_file) sous un
 * parent en cours de fermeture rend aussi FL_EBUSY. Avec force : ferme les fichiers (en attendant les
 * lectures en cours) puis démonte. */
int fj_unmount(int64_t vol, bool force);

int fj_statfs(int64_t vol, fl_json *out);
int fj_stat(int64_t vol, const char *path, fl_json *out);
/* Tableau trié par nom (ordre des octets). */
int fj_list(int64_t vol, const char *path, fl_json *out);

int64_t fj_open_file(int64_t vol, const char *path);
/* Octets lus (<= min(len, FJ_PREAD_MAX)), 0 = fin, < 0 = fl_err. */
int64_t fj_pread(int64_t file, void *buf, size_t len, uint64_t off);
int fj_close_file(int64_t file);

/* Références et état « en fermeture » d'une entrée (tests) ; FL_EINVAL si la poignée est invalide. */
int fj_debug_slot(int64_t h, unsigned *refs, bool *closing);
/* Tests : fn(arg) est appelée par fj_mount, référence du disque tenue, juste avant d'enregistrer le
 * volume. NULL retire le crochet. À poser avant de lancer des fils, à retirer après leur fin. */
void fj_debug_set_mount_hook(void (*fn)(void *), void *arg);
/* Nombre d'entrées occupées (tests). */
unsigned fj_debug_used(void);

#ifdef __cplusplus
}
#endif

#endif
