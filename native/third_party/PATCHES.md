# Correctifs des dépendances tierces

## lwext4

- Amont : https://github.com/gkostka/lwext4, commit `58bcf89` (« Documentation improvements (#65) »).
  Le sous-module `native/third_party/lwext4` reste sur ce commit, avec l'URL amont dans `.gitmodules` ;
  il n'est jamais modifié.
- Correctifs, dans le dépôt, appliqués dans l'ordre alphabétique :
  - `native/third_party/patches/lwext4/0001-fix-lecture-des-trous-parcours-de-dossier-et-inodes-.patch`
    (généré par `git format-patch`, détaillé ci-dessous en 1 à 3) ;
  - `native/third_party/patches/lwext4/0002-lecture-noms-255-trous-de-dossier.patch` (phase 3, T1,
    détaillé en 4 à 6) ;
  - `native/third_party/patches/lwext4/0003-ecriture-api-horodatages-trous.patch` (phase 3, T4,
    détaillé en 7 à 10).
- Application (`native/cmake/third_party.cmake`, à la configuration CMake) : `src/` et `include/` du
  sous-module sont copiés dans `${CMAKE_BINARY_DIR}/third_party/lwext4`, puis chaque correctif y est
  appliqué par `git apply -p1` (avec `GIT_CEILING_DIRECTORIES`, sinon `git apply` lancé à l'intérieur
  du dépôt ignore les chemins en silence et réussit sans rien faire). La cible `fl_lwext4` compile cette
  copie. Une empreinte SHA-256 des sources amont et des correctifs (`.fl_stamp`) déclenche une nouvelle
  copie dès que l'un d'eux change ; les correctifs sont des dépendances de configuration, donc `cmake
  --build` reconfigure tout seul. Un correctif qui ne s'applique pas fait échouer la configuration.
- Ajouter un correctif : le préparer dans le sous-module (branche locale), l'exporter par
  `git -C native/third_party/lwext4 format-patch -1 <commit> -o native/third_party/patches/lwext4/` en le
  numérotant à la suite, puis remettre le sous-module sur `58bcf89`. La configuration copie le
  sous-module tel qu'il est sur le disque : un travail en cours y casse l'application des correctifs
  (le faire plutôt dans un `git worktree` hors du sous-module).

Aucune fonctionnalité du disque réel (`metadata_csum`, `64bit`, `flex_bg`, `extent`, `huge_file`,
`dir_nlink`, `extra_isize`, journal présent mais non rejoué…) n'est refusée par lwext4 : le montage en
lecture seule fonctionne sans correctif. Les correctifs 0001 et 0002 portent sur la justesse de la
lecture et sur la robustesse face à un disque hostile ; 0003 sur l'écriture.

### 1. `ext4_fread` : trous et extents non écrits lus comme des zéros

Dans la boucle des blocs entiers et dans le bloc final partiel, un bloc logique sans bloc physique (trou
d'un fichier creux, extent « unwritten ») a pour adresse 0. lwext4 lisait alors le **bloc 0 du disque**
(zone d'amorçage et superbloc) au lieu de rendre des zéros ; seul le début de lecture non aligné traitait
ce cas. Corrigé : les suites de trous sont remplies de zéros, les suites de blocs contigus sont lues d'un
coup, et une suite est limitée à 4096 blocs pour que le nombre de secteurs passé à `bread` tienne sur
32 bits.

Test : `test_ext4_lecture_au_dela_de_4gio` (lecture de 3 blocs et demi dans le trou de `clairsemé.bin`
à 4 Gio et à 4 Gio + 1000 : tout à zéro). Sans le correctif, les octets du superbloc apparaissent.

### 2. `ext4_dir_entry_get` (nouvelle fonction) et `ext4_dir_entry_next`

`ext4_dir_entry_next` déréférençait `it.curr` sans vérifier qu'il existe (dossier de taille nulle ou bloc
de dossier corrompu → pointeur NULL), copiait le nom avec une longueur pouvant atteindre 4088 octets
(révision 0, `name_length_high`) dans un tableau de 255 octets, et ne distinguait pas la fin du dossier
d'une erreur d'E/S. `ext4_dir_entry_get(dir, &de)` rend un code d'erreur (`EIO` pour un dossier corrompu)
et `de = NULL` en fin de dossier ; `ext4_dir_entry_next` en devient une simple enveloppe.

### 3. `ext4_raw_inode_fill_ino` (nouvelle fonction) et copie bornée des inodes

- `ext4_raw_inode_fill_ino(point_de_montage, numéro, &inode)` lit un inode par son numéro (pour
  `readdir`, sans relire tout le chemin), après avoir vérifié `1 ≤ numéro ≤ inodes_count`.
- `ext4_raw_inode_fill` copiait `sizeof(struct ext4_inode)` (160 octets) depuis le tampon du bloc : avec
  des inodes de 128 octets, le dernier inode d'un bloc déborde du tampon. La copie est désormais bornée à
  la taille d'inode du superbloc, le reste étant mis à zéro.

### 4. Noms de 255 octets

`ext4_path_check` bornait un composant de chemin à 254 octets (`<` au lieu de `<=`) : un fichier au nom de
255 octets, créé par le noyau, apparaissait dans `readdir`, mais `stat`/`open` rendaient `FL_ENOENT`.
Corrigé. Un composant de 256 octets reste refusé par `fs.c` (`FL_EINVAL`).

Test : `test_ext4_nom_255` (`ext4-nom255.img`).

### 5. Trous de dossier : `ext4_dir_get_dblk` et le code `EFSCORRUPTED`

Un bloc logique de dossier sans bloc physique (trou, extent non écrit) était rendu par
`ext4_fs_get_inode_dblk_idx` comme l'adresse 0 : lwext4 lisait alors le **bloc 0 du disque** comme un bloc
de dossier (et l'aurait écrit en ajoutant une entrée ; l'ajout bouclait sans fin). La nouvelle fonction
`ext4_dir_get_dblk` rend `EFSCORRUPTED` dans ce cas ; elle remplace l'appel direct dans le parcours, la
recherche et l'ajout, linéaires comme htree (`ext4_dir.c`, `ext4_dir_idx.c`). Le noyau traite ce cas comme
une corruption (« directory contains a hole »).

`EFSCORRUPTED` (= `EUCLEAN`, 117, comme le noyau) est défini dans `ext4_errno.h` et sert à toute structure
corrompue : trou de dossier, entrée mal formée, inode ou groupe hors bornes. `EIO` reste réservé aux erreurs
du périphérique. La colle traduit `EFSCORRUPTED` en `FL_ECORRUPT` et `EIO` en `FL_EIO`.

Au passage, dans les dossiers htree : une erreur d'E/S ou un trou rencontré dans l'index n'est plus changé en
`EXT4_ERR_BAD_DX_DIR` (qui effaçait le drapeau d'index du dossier et repassait en recherche linéaire), une
erreur de `ext4_dir_dx_next_block` n'est plus changée en `ENOENT`, et `ext4_dir_dx_add_entry` ne libère
plus de bloc non chargé sur ses chemins d'erreur.

`ext4_dir_dx_init` (appelé par `ext4_trunc_dir`, donc par `ext4_dir_rm`) obtient les blocs logiques 0 et 1
par `ext4_fs_init_inode_dblk_idx`, qui n'alloue un bloc absent que pour un inode à extents : pour un dossier
adressé par blocs (sans extents), il rendait 0, et `dx_init` écrivait une racine d'index dans le **bloc 0 du
disque** (superbloc primaire), y compris pour un simple `rmdir` d'un dossier vide d'un seul bloc. Les deux
blocs sont maintenant vérifiés avant toute modification : `EFSCORRUPTED` pour un trou dans le dossier,
`ENOTSUP` pour un bloc au-delà de sa fin (il faudrait l'allouer, ce que lwext4 ne fait pas sans extents).
`ext4_dir_get_dblk` passe `support_unwritten = true` (elle refuse le 0 elle-même), si bien que
`ext4_assert` ne peut pas se déclencher avec `CONFIG_DEBUG_ASSERT`.

Tests :
- `test_ext4_trou_de_dossier` : parcours et recherche, aucune lecture des octets [0, 4096) après le montage
  (`ext4-trou-dossier.img`).
- `test_ext4_trou_de_dossier_ajout` : `ext4_dir_add_entry` sur une copie en mémoire montée en écriture,
  aucun accès aux octets [0, 4096).
- `test_ext4_dossier_indirect_bloc0` : `ext4_dir_dx_init` et `ext4_dir_rm` sur `ext4-indirect.img` (sans
  extents ; trou dans `/r`, `/vide` d'un bloc), aucun accès aux octets [0, 4096).
- `test_ext4_erreur_es_dossier` : seule la lecture d'un bloc de dossier échoue (feuille htree de `/h`, bloc
  d'index d'extents de `/h`, bloc 0 du dossier linéaire `/d`, numéros notés dans
  `ext4-trou-dossier.blocs`) : parcours et recherche rendent `FL_EIO`.

### 6. Entrées de dossier hostiles

- `ext4_dir_find_in_block` (recherche par nom) vérifie que chaque en-tête d'entrée et chaque nom tiennent dans
  le bloc (avant : lecture possible jusqu'à 8 octets après le bloc, et une longueur d'entrée nulle était
  ignorée en silence) et **ignore** une entrée dont l'inode est hors de [1, `s_inodes_count`]. Avant, un tel
  nom se résolvait vers un inode lu au hasard (descripteur de groupe hors table, « table d'inodes » au
  bloc 0) et `stat` rendait `FL_OK` avec un contenu quelconque.
- `ext4_dir_iterator_set` rend `EFSCORRUPTED` pour une entrée mal formée.
- `ext4_dir_entry_get` rend une entrée au nom de plus de 255 octets (révision 0) comme une entrée libre, et le
  parcours continue au lieu de s'arrêter sur `EIO`.
- `__ext4_fs_get_inode_ref` et `ext4_fs_get_block_group_ref` refusent un numéro d'inode ou de groupe hors
  bornes (`EFSCORRUPTED`).
- Côté colle, `next_entry` ignorait déjà les entrées d'inode nul ou hors bornes, de nom vide ou contenant
  `/` ou NUL.

Test : `test_ext4_entree_hostile_ignoree` (`ext4-entrees-hostiles.img`, fabriquée par `craft.py`).

### 7. Règles de création et horodatages (`ext4_set_policy`, `ext4_inode_set_times`)

L'amont ne tenait aucun horodatage (tout à 0, donc 1970 sous Ubuntu), créait les fichiers en 0666 et
les dossiers en 0777, en uid/gid 0, sans hériter du bit setgid. `ext4_set_policy(&règle)` pose une
horloge et les droits des inodes créés ; avec elle, lwext4 tient les horodatages comme le noyau, dans
la transaction de chaque opération :
- création : atime, mtime, ctime et crtime (bits d'époque des champs « extra » après 2038 ; crtime si
  `extra_isize` ≥ 24) ; uid et gid du dossier parent (16 bits hauts compris), droits de la règle, bit
  setgid hérité par un dossier ;
- écriture et troncature : mtime et ctime du fichier ;
- ajout, retrait et renommage d'entrée : mtime et ctime du ou des dossiers parents, ctime de l'inode
  déplacé ou délié ; dtime d'un inode libéré.

La colle pose `{fl_fs_now, 0644, 0755}` à chaque montage en écriture (§ 4.2 de la spec). Sans règle :
comportement amont.

Tests : `test_ecriture_proprietaire_herite`, `test_ecriture_horodatages` (dont 2106).

### 8. Écriture de fichiers : trous, blocs partiels, erreurs

- `ext4_fwrite` est réécrit pour les fichiers à extents. L'amont, pour un bloc au-delà de la fin,
  appelait `ext4_fs_append_inode_dblk`, qui alloue le bloc suivant la fin et non celui visé (une
  écriture au-delà de la fin atterrissait au mauvais endroit) ; un bloc partiel nouvellement alloué
  gardait l'ancien contenu du disque autour des données ; l'erreur d'E/S était écrasée par
  `ext4_fs_put_inode_ref` (EOK avec 0 octet écrit) ; une sortie anticipée laissait le cache en
  écriture différée. Désormais : trou au-delà de la fin, zéros autour des données d'un bloc neuf,
  blocs entiers alloués par suites contiguës, taille tenue au fil de l'écriture (une erreur laisse un
  fichier cohérent, plus court), première erreur rendue. Un fichier adressé par blocs (sans extents)
  rend `ENOTSUP` ; au-delà de 2³² blocs, `EFBIG`.
- `ext4_ftruncate` : agrandir crée un trou (l'amont rendait EOK sans effet) ; raccourcir à une taille
  non alignée met à zéro la fin du nouveau dernier bloc, comme le noyau (`block_truncate_page`).
- `ext4_fseek` : position au-delà de la fin permise, `SEEK_END` corrigé (le décalage était
  retranché) ; `ext4_fread` ne lit rien au-delà de la fin.
- `ext4_extent_remove_space` : quand le début de la plage tombe dans un trou, l'amont ne retirait
  rien ; supprimer un fichier creux (`clairsemé.bin`) laissait ses blocs alloués (e2fsck « Block bitmap
  differences »).

Tests : `test_ecriture_trou`, `test_ecriture_troncature`, `test_ecriture_gros_fichier`,
`test_ecriture_suppression`.

### 9. Dossiers en écriture

- `ext4_dir_dx_init` : racine et feuille (non lues) remplies de zéros ; la somme de la feuille est
  posée après la mise à 0 de l'inode (avant : e2fsck « passes checks but fails checksum » pour chaque
  dossier resté vide, défaut de l'essai).
- `ext4_dir_find_entry`, `ext4_dir_add_entry` : un index htree jugé invalide rend `EFSCORRUPTED`, sans
  effacer le drapeau d'index (l'amont l'effaçait, même pour une recherche, puis continuait en linéaire).
- `ext4_dir_try_insert_entry`, `ext4_dir_dx_split_data` : entrées validées comme dans
  `ext4_dir_find_in_block` (`EFSCORRUPTED` ; une longueur nulle bouclait sans fin), erreur propagée par
  les appelants au lieu d'être prise pour un bloc plein.
- `ext4_dir_dx_get_leaf` : le nœud fils d'un index à deux niveaux est lu dans un bloc à lui (avant :
  dans la racine de l'appelant, rendue jamais, le fils deux fois, sur une erreur à ce niveau).
- `ext4_dir_dx_split_index` : la limite d'un nouveau nœud tient compte de la queue de somme ; l'amont
  écrivait une limite que lwext4 lui-même jugeait ensuite invalide (index à deux niveaux avec
  `metadata_csum`).
- `ext4_dir_dx_add_entry` : un ajout qui échoue après le partage d'une feuille rend l'erreur (avant :
  EOK, entrée manquante).
- `ext4_link` : renommer un dossier linéaire vers un autre parent recalcule la somme du bloc de « .. »
  (avant : « Directory block checksum does not match »).

Tests : `test_ecriture_dossier_vide`, `test_ecriture_3000`, `test_ecriture_renommage`,
`test_ecriture_htree_invalide_sans_modif`, `test_interne_htree_invalide_recherche`,
`test_interne_ajout_longueur_nulle`, `test_interne_htree_2niveaux_erreur`.

### 10. `ext4_dir_rm`

Un dossier à libérer est vidé par `ext4_trunc_inode` avant le retrait de son entrée, comme un fichier
dans `ext4_fremove`. `ext4_trunc_dir`, qui reconstruisait d'abord un index dans le dossier (et
échouait en `ENOTSUP` sur un dossier sans extents d'un seul bloc, après avoir retiré l'entrée : dossier
délié mais alloué), disparaît.

Tests : `test_interne_rmdir_sans_extents`, `test_ext4_dossier_indirect_bloc0`.

## Protections côté FluLuks (sans correctif lwext4)

En écriture, `ext4_glue.c` vérifie chaque opération avant d'appeler lwext4, dont les fonctions de haut
niveau ont des effets que l'API n'admet pas : `ext4_fopen2` avec `O_CREAT` crée les dossiers
intermédiaires (le parent doit exister), `ext4_frename` et `ext4_dir_mk` ne refusent pas tous les cas
(cible existante, cible sous la source), `ext4_dir_rm` supprime récursivement (le dossier doit être
vide), `ext4_fremove` sur un fichier ouvert libère son inode sous le descripteur (`FL_EBUSY`).
L'écriture n'est permise qu'avec journal et extents, et sans option inconnue ; sinon lecture seule avec
la raison.

`native/src/fs/ext4_glue.c` valide le superbloc avant de laisser lwext4 s'en servir (taille de bloc,
tailles d'inode et de descripteur, compteurs par groupe, cohérence avec la taille du disque, fonctions
incompatibles), et sert à lwext4 la copie validée du superbloc à chaque relecture (un périphérique
malveillant ne peut pas en changer entre la validation et le montage). Sans cette validation, un
superbloc avec `s_log_block_size = 30` provoque un comportement indéfini dans lwext4 dès le montage.

## Limites connues de lwext4 (non corrigées)

- Blocs de 64 Kio sans `metadata_csum` : une entrée qui couvre tout le bloc a une longueur codée 0 ou 65535
  (65536 pour le noyau) ; `ext4_dir_find_in_block` (comme déjà `ext4_dir_iterator_set` en amont) la traite
  comme une corruption. Le vrai disque a des blocs de 4 Kio et `metadata_csum`.
- Un dossier créé par lwext4 est toujours indexé (racine htree et une feuille, 2 blocs), même vide ;
  le noyau crée un dossier linéaire d'un bloc et ne l'indexe qu'au-delà. Valide pour `e2fsck`.
- Une opération en plusieurs transactions n'est pas atomique : la suppression d'un fichier ou d'un
  dossier tronque d'abord (transactions de 16 Mio au plus), puis retire l'entrée ; une coupure entre les
  deux laisse une entrée vers un fichier vide, que `e2fsck` accepte.
- Journal : les sommes jbd2 v3 écrites par lwext4 sont fausses et la séquence repart de 0 à chaque
  session (essai du 2026-10-18) ; correctif 0004 (T5). D'ici là, seul un démontage propre laisse le
  disque propre : une coupure en pleine écriture n'est pas récupérable proprement par Ubuntu.
- Les liens symboliques ne sont pas suivis ; `fl_fs_open` sur un lien rend `FL_ENOTSUP`.
- Les sommes de contrôle `metadata_csum` des inodes, des descripteurs et des dossiers ne sont vérifiées
  qu'à titre d'avertissement (seule celle du superbloc est bloquante).
- Le journal n'est jamais rejoué (`needs_recovery` le signale) ; `metadata_csum_seed`, `inline_data`,
  `encrypt`, `casefold`, `largedir`, `ea_inode` rendent `FL_ENOTSUP`.
