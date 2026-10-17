# Correctifs des dépendances tierces

## lwext4

- Amont : https://github.com/gkostka/lwext4, commit `58bcf89` (« Documentation improvements (#65) »).
- Correctifs : branche `fluluks` du sous-module `native/third_party/lwext4`, commit `9386719` au-dessus de `58bcf89`.
- **À faire :** publier cette branche sur un fork (aucun n'existe encore), puis faire pointer l'URL de
  `.gitmodules` vers ce fork. Tant que ce n'est pas fait, un clone neuf ne peut pas récupérer le commit
  référencé par le sous-module.

Aucune fonctionnalité du disque réel (`metadata_csum`, `64bit`, `flex_bg`, `extent`, `huge_file`,
`dir_nlink`, `extra_isize`, journal présent mais non rejoué…) n'est refusée par lwext4 : le montage en
lecture seule fonctionne sans correctif. Les correctifs portent sur la justesse de la lecture et sur la
robustesse face à un disque hostile.

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

## Protections côté FluLuks (sans correctif lwext4)

`native/src/fs/ext4_glue.c` valide le superbloc avant de laisser lwext4 s'en servir (taille de bloc,
tailles d'inode et de descripteur, compteurs par groupe, cohérence avec la taille du disque, fonctions
incompatibles), et sert à lwext4 la copie validée du superbloc à chaque relecture (un périphérique
malveillant ne peut pas en changer entre la validation et le montage). Sans cette validation, un
superbloc avec `s_log_block_size = 30` provoque un comportement indéfini dans lwext4 dès le montage.

## Limites connues de lwext4 (non corrigées)

- `ext4_path_check` refuse un composant de chemin d'exactement 255 octets (borne `<` au lieu de `<=`) :
  un tel fichier apparaît dans `readdir` mais `stat`/`open` rendent `FL_ENOENT`. Pas de fixture pour le
  tester ; à corriger avec un test dédié.
- Les liens symboliques ne sont pas suivis ; `fl_fs_open` sur un lien rend `FL_ENOTSUP`.
- Les sommes de contrôle `metadata_csum` des inodes, des descripteurs et des dossiers ne sont vérifiées
  qu'à titre d'avertissement (seule celle du superbloc est bloquante).
- Le journal n'est jamais rejoué (`needs_recovery` le signale) ; `metadata_csum_seed`, `inline_data`,
  `encrypt`, `casefold`, `largedir`, `ea_inode` rendent `FL_ENOTSUP`.
