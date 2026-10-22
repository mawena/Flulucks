#ifndef FLULUKS_FORMAT_H
#define FLULUKS_FORMAT_H

#include <stdbool.h>
#include <stdint.h>
#include "fluluks/blockdev.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Formatage d'un disque USB (phase 8). Tout écrit sur `d` : jamais d'appel sur un disque monté. */

typedef enum { FL_FMT_FAT32 = 1, FL_FMT_EXFAT, FL_FMT_EXT4, FL_FMT_NTFS } fl_fmt_fs;

/* Table à une seule partition alignée sur 1 Mio, jusqu'à la fin du disque (au Mio près) : MBR jusqu'à
 * 2 Tio, GPT au-delà. Type selon `fs` (LUKS : partition Linux). Le Mio de tête et le dernier Mio sont
 * effacés (anciens GPT). Rend la place de la partition, en secteurs de `d`. */
int fl_part_write_single(fl_bdev *d, fl_fmt_fs fs, bool encrypted, uint64_t *first_lba, uint64_t *count);

/* FAT32 ou exFAT (FatFs) sur tout `part`, sans table. Étiquette : 11 caractères au plus, ASCII seulement
 * en FAT32 (mise en majuscules) ; sinon FL_EINVAL avant toute écriture. FL_ENOSPC si `part` est trop petit
 * (FAT32 : 65 525 clusters au moins). */
int fl_mkfs_fat(fl_bdev *part, bool exfat, const char *label);

/* ext4 avec journal (lwext4), blocs de 4 Kio, sur tout `part`. Étiquette : 16 octets UTF-8 au plus, sinon
 * FL_EINVAL avant toute écriture. Le dossier racine appartient à root_uid:root_gid. FL_ENOSPC sous 8 Mio. */
int fl_mkfs_ext4(fl_bdev *part, const char *label, uint32_t root_uid, uint32_t root_gid);

/* NTFS 3.1 (mkntfs de ntfs-3g, formatage rapide) sur tout `part`, sans table. Étiquette : 32 caractères
 * (unités UTF-16) au plus, UTF-8 valide sans caractère de contrôle, sinon FL_EINVAL avant toute écriture.
 * `hidden_sectors` : début de la partition sur le disque, en secteurs (champ du secteur de démarrage,
 * 32 bits ; 0 dans un volume chiffré). FL_ENOSPC sous 8 Mio. */
int fl_mkfs_ntfs(fl_bdev *part, const char *label, uint64_t hidden_sectors);

/* LUKS2 comme `cryptsetup luksFormat` : aes-xts-plain64, clé maîtresse de 512 bits, emplacement 0 en
 * Argon2id (t=4, m=kdf_mem_kib, p=4), deux en-têtes de 16 Kio, données à 16 Mio, secteurs chiffrés de
 * 512 octets (ou ceux de `part` s'ils sont plus grands). Rend le volume déchiffré (à fermer avant `part`).
 * FL_EINVAL (phrase vide, kdf_mem_kib < 8 Mio) ou FL_ENOSPC (moins d'un Mio de données) avant toute
 * écriture. La clé maîtresse ne quitte jamais la mémoire sécurisée. */
int fl_luks2_format(fl_bdev *part, const void *pass, size_t passlen, uint32_t kdf_mem_kib, fl_bdev **decrypted);

typedef struct {
  fl_fmt_fs fs;
  bool encrypted;
  const char *label;
  uint32_t kdf_mem_kib;        /* mémoire Argon2 si chiffré */
  uint32_t root_uid, root_gid; /* ext4 : propriétaire de la racine */
} fl_fmt_opts;

/* Formate tout `disk` : vérifications (taille minimale, étiquette, phrase) avant toute écriture, table à
 * une partition, effacement des anciennes signatures, LUKS2 si demandé, système de fichiers, vidage du
 * cache du disque. progress (facultatif) reçoit un pourcentage croissant, 100 à la fin. */
int fl_format(fl_bdev *disk, const fl_fmt_opts *o, const void *pass, size_t passlen,
              void (*progress)(int pct, void *arg), void *arg);

/* `len` octets d'aléa de /dev/urandom ; FL_EIO si la lecture échoue (jamais d'aléa faible). */
int fl_random(void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif
