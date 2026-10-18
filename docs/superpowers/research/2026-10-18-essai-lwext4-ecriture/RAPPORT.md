# Essai jetable : écriture lwext4 face à `e2fsck` (2026-10-18)

But : savoir, avant d'écrire le plan de la phase 3, ce que l'écriture de lwext4 casse sur une image aux
options exactes du vrai disque. Source essayée : `build/native/third_party/lwext4`, c'est-à-dire l'amont
58bcf89 avec le correctif 0001, compilé avec `CONFIG_USE_DEFAULT_CFG=1 CONFIG_EXT4_BLOCKDEVS_COUNT=8
CONFIG_EXT4_MOUNTPOINTS_COUNT=8 CONFIG_DEBUG_ASSERT=0`. Juge : e2fsprogs 1.47.0. Les numéros de ligne
renvoient à cette source patchée.

Fichiers de ce dossier :
- `harness.c` : le banc d'essai. Il comprend un périphérique fichier, l'injection de pannes d'écriture et
  une copie fantôme des octets écrits, comparée ensuite à `debugfs dump`.
- `run.sh` et `crash.sh` : les scénarios et les coupures.
- `jcheck.py` : un vérificateur indépendant des sommes jbd2 v3.
- `csum_fix.diff` : le correctif de 2 lignes vérifié pendant l'essai.

Les chemins de ces scripts pointent vers le dossier temporaire de la session : ils sont à adapter avant
de les relancer.

## Image « vrai disque »

Commande de création :

```
mkfs.ext4 -q -F -T default -b 4096 -I 256 -O ^orphan_file,^metadata_csum_seed -L SPIKE -d tree A.img 512M
```

Elle donne exactement les options du vrai disque, à une exception près : **mkfs laisse les options du
journal vides** tant qu'un noyau n'a pas monté le système de fichiers. Pour obtenir
`journal_incompat_revoke journal_64bit journal_checksum_v3` sans root, il faut lancer
`debugfs -w -R` avec `jo -c`, `jw -r 100` puis `jc`, et enfin `e2fsck -fy`.

## Résultats sans coupure (`e2fsck -fn` après un démontage propre)

Ces scénarios sont **propres**, avec un contenu identique dans 241 vérifications sur 241 :
- 1 fichier ;
- 200 fichiers ;
- 3 000 fichiers dans un dossier (htree) ;
- 300 Mio écrits par blocs de 1 Mio ;
- des réécritures aléatoires ;
- des troncatures vers le bas ;
- des renommages dans un même dossier ou entre dossiers ;
- des suppressions, rmdir compris ;
- 40 niveaux de dossiers ;
- des noms Unicode ;
- une session sans journal.

Les secteurs de 4096 octets et les blocs de 1 Kio marchent aussi.

Défauts relevés :

| Défaut | Emplacement | Effet |
|---|---|---|
| Somme de la feuille d'un dossier vide fausse | `ext4_dir_idx.c:ext4_dir_dx_init` : la somme est posée l. 442 avant la mise à 0 du champ inode l. 447, sur un bloc non effacé | `e2fsck` : « directory passes checks but fails checksum », une fois par dossier resté vide ; le reste du bloc contient d'anciennes données |
| Nom de 255 octets → ENOENT | `ext4.c:ext4_path_check` l. 761 : `<` au lieu de `<=` | bloque aussi la lecture de noms créés par le noyau |
| `ftruncate` vers une taille plus grande → EOK sans effet | `ext4.c:ext4_ftruncate_no_lock` l. 1618 | |
| Positionnement au-delà de la fin → EINVAL ; SEEK_END inversé | `ext4.c:ext4_fseek` l. 2048/2056 | impossible de créer un trou |
| Renommer sur un nom existant → EEXIST | `ext4_frename` | pas de remplacement |
| `ext4_fwrite` renvoie EOK avec wcnt = 0 sur une erreur d'E/S | `ext4.c:2033` : `ext4_fs_put_inode_ref` écrase l'erreur | |
| Aucun horodatage tenu (atime, ctime, mtime, crtime à 0 ; generation à 0) ; fichiers 0666 et dossiers 0777 en uid/gid 0 ; setgid non hérité | `ext4_fs.c:ext4_fs_alloc_inode` l. 911-920 | 1970 sous Ubuntu, fichiers appartenant à root |

## Coupures (écritures en EIO après N écritures, sans démontage)

**Le journal de lwext4 est inutilisable en l'état.** Ses sommes `journal_checksum_v3` sont fausses sur
une machine little-endian :
- `ext4_journal.c:jbd_block_csum` (l. 336-337) hache la séquence dans l'ordre de l'hôte ;
- `jbd_commit_csum_set` (l. 303) oublie `to_be32`.

Conséquence : le noyau, `e2fsck` et lwext4 lui-même rejettent les transactions (« Journal transaction N
was corrupt, replay was aborted »). On obtient alors des blocs utilisés mais marqués libres, donc des
doubles allocations plus tard, et des compteurs de liens faux. Avec `csum_fix.diff`, **toutes** les
coupures se récupèrent proprement, aussi bien par `e2fsck -fy` que par la relecture de lwext4.

**Second défaut, que la correction ci-dessus révèle** : `jbd_journal_stop` (l. 1446) remet la séquence
du journal à 0 à chaque arrêt propre. Les identifiants de transaction d'une session retombent alors aux
mêmes emplacements que ceux de la session précédente. Après une coupure, d'anciennes transactions sont
rejouées : des fichiers partent dans lost+found et un dossier est vidé (reproduit). jbd2, lui, garde
`s_sequence` égal au prochain identifiant.

Autres constats :
- **NULL déréférencé** sur une erreur d'E/S avec le cache en écriture différée :
  `ext4_blockdev.c:ext4_block_get_noread` l. 227 pose `lb_id` avant `ext4_block_cache_shake()`, puis
  `ext4_extent.c:read_extent_tree_block` l. 793 plante (confirmé par ASan). C'est le cas d'un
  débranchement en cours d'écriture.
- **`s_state` passe à « erreurs » (2) pendant tout montage en écriture** (`ext4_fs.c:107-120`). Après
  une coupure, Ubuntu voit « not clean with errors ».
- Le journal au format du **noyau** (écrit par debugfs, avec un revoke) est **correctement rejoué** par
  lwext4.
- Sans journal, une coupure laisse des incohérences (bitmap, somme) que seul `e2fsck` répare.

## Montage en écriture

- **Le premier `ext4_recover` en écriture remet à zéro les tables d'inodes non initialisées** de tous
  les groupes (`ext4.c:584-604` → `ext4_fs.c:600-627`), hors journal. Sur un disque de 1 To, cela fait
  environ 15 Go d'écritures. Tout appel à `ext4_fs_get_block_group_ref` sur un tel groupe le fait aussi.
- Une option ro_compat inconnue force la lecture seule **en silence** : `ext4_mount` rend EOK, puis
  chaque écriture rend EROFS.
- Image aux options par défaut d'e2fsprogs 1.47 en amont (`metadata_csum_seed` + `orphan_file`) :
  ENOTSUP **même en lecture seule**, car l'incompat 0x2000 n'est pas géré. `orphan_file` seul se monte
  en écriture et reste ignoré ; `e2fsck` est propre.
- Les erreurs de somme des descripteurs de groupe sont seulement journalisées, pas refusées.
- Les champs `s_mtime`, `s_wtime`, `s_kbytes_written` et `s_last_mounted` ne sont jamais mis à jour.

## Vitesse (300 Mio, image en cache mémoire)

Environ **84 Mo/s**, avec ou sans journal et avec ou sans écriture différée. Le moteur est limité par le
processeur : 96 % du temps passe dans le crc32c octet par octet (`ext4_crc32.c:164`). Il est recalculé à
chaque allocation de bloc, une par une (76 800 fois), sur le bitmap de 4 Kio et le descripteur de
groupe. Avec des blocs de 1 Kio : 47 Mo/s. Créer 3 000 fichiers prend 1,7 s et 64 870 écritures de bloc.
