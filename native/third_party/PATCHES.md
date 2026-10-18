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
    détaillé en 7 à 10) ;
  - `native/third_party/patches/lwext4/0004-journal-sommes-v3-sequence-barrieres.patch` (phase 3, T5,
    détaillé en 11) ;
  - `native/third_party/patches/lwext4/0005-montage-etat-tables-inodes-sommes.patch` (phase 3, T6,
    détaillé en 12) ;
  - `native/third_party/patches/lwext4/0006-crc32c-rapide.patch` (phase 3, T7, détaillé en 13) ;
  - `native/third_party/patches/lwext4/0007-journal-commit-echappement-zero.patch` (phase 3, correctif
    du journal après relecture, détaillé en 14) ;
  - `native/third_party/patches/lwext4/0008-extents-racine-verifiee.patch` et
    `native/third_party/patches/lwext4/0009-journal-annulation-sans-relecture.patch` (phase 3, correctif
    écriture et politique après relecture, détaillés en 15).
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
lecture et sur la robustesse face à un disque hostile ; 0003 sur l'écriture ; 0004 sur le journal ; 0005 sur le montage ; 0006 sur la vitesse ; 0007 corrige encore le journal ; 0008 et 0009 les erreurs pendant une écriture.

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

### 11. Journal récupérable après une coupure

- Sommes jbd2 v3 : `jbd_block_csum` hache la séquence en big-endian et `jbd_commit_csum_set` stocke la
  somme du bloc de commit en big-endian (`csum_fix.diff` de l'essai). Avant : transactions rejetées par
  le noyau, `e2fsck` et lwext4 lui-même.
- Séquence : `jbd_journal_stop` ne remet plus `s_sequence` à 0 (prochaine transaction, comme jbd2) ;
  avant, une coupure à la session suivante rejouait d'anciennes transactions. Le journal est marqué vide
  (`s_start = 0`) avant l'effacement de `needs_recovery`.
- Barrières : `ext4_blockdev_iface.bflush` (NULL accepté), appelée avant et après le bloc de commit,
  avant l'écriture du superbloc du journal (point de contrôle) et à la fin de `ext4_cache_flush`. La
  colle la branche sur `fl_bdev_flush` (sur USB : SYNCHRONIZE CACHE).
- `ext4_umount` libère tout même si le superbloc ne peut pas être écrit (disque mort), et rend l'erreur.

Côté colle : après une erreur d'écriture ou de barrière, plus rien n'est écrit (le disque reste figé dans
l'état de la panne, que le journal sait récupérer ; lwext4 ignore les erreurs d'écriture de son cache de
blocs, et son état en mémoire diverge alors de celui du disque) ; les écritures rendent `FL_EIO`.

Tests (`test_ext4_coupure.c`) : `test_ext4_journal_sommes` (vérificateur C des sommes, repris de
`jcheck.py`), `test_ext4_barrieres`, `test_ext4_coupure_sessions`,
`test_ext4_coupure_fichiers_fermes_intacts`, `test_ext4_coupure_gros_fichier`,
`test_ext4_coupure_3000` : après coupure, `e2fsck -fy` ne fait que rejouer le journal (et corriger les
compteurs globaux), puis `e2fsck -fn` est propre.

### 12. Montage sobre et sûr

- État du superbloc : avec journal, un montage en écriture laisse `s_state` « propre »
  (`needs_recovery` dit que le volume est en service), comme le noyau ; avant : « erreurs » (ERROR_FS),
  et Ubuntu voyait « not clean with errors » après une coupure. `s_mtime`, `s_wtime` et
  `s_kbytes_written` sont tenus.
- Tables d'inodes : jamais remises à zéro par `ext4_fs_get_block_group_ref` (avant : environ 15 Go
  d'écritures pour 1 To au premier montage) ; `bg_itable_unused` dit quels inodes n'ont jamais servi.
- `metadata_csum_seed` pris en charge (`ext4_sb_csum_seed`) : les images aux options par défaut
  d'e2fsprogs 1.47 se montent, en lecture comme en écriture.
- Sommes fausses (inode, bloc d'extents, bloc de dossier ou d'index, bitmap, descripteur de groupe) :
  `EBADMSG`, sans rien modifier (avant : simple avertissement, puis réécriture avec une somme juste).
- `ext4_block_get_noread` : `lb_id` posé seulement une fois le bloc obtenu (NULL déréférencé sur une
  erreur d'E/S avec le cache en écriture différée).
- `ext4_ialloc_alloc_inode` ne boucle plus sur un groupe au bitmap plein.

Côté colle : `ext4_policy.c` applique les règles du § 4.2 avant que lwext4 n'écrive quoi que ce soit
(options du système de fichiers et du journal, journal présent et sans rejeu en attente, état propre,
sommes du superbloc, de chaque descripteur de groupe et du superbloc du journal, taille de bloc).
`EBADMSG` devient `FL_ECORRUPT` et fait passer le volume en lecture seule (`RO_CSUM`) ; une erreur
d'écriture, en lecture seule `RO_IOERR`. Lecture seule collante : les écritures suivantes rendent
`FL_EROFS`, la lecture continue.

Tests : `test_ext4_politique.c` (12 tests).

### 13. crc32c rapide

`ext4_crc32c` (96 % du temps d'écriture dans l'essai, octet par octet) garde sa signature et son
résultat au bit près. Chemin choisi une fois : SSE4.2 sur x86_64, instructions crc32c d'ARMv8 sur arm64
(`HWCAP_CRC32`), sinon tranches de 8. `ext4_crc32c_sw` (chemin portable) et `ext4_crc32c_impl` (nom du
chemin) servent aux tests.

Mesure (PC, build Debug avec ASan, 300 Mio écrits par tranches de 1 Mio sur `ext4-rw-gros.img`, comme
`test_ecriture_gros_fichier`) : 5,3 s avant, 2,2 s après (× 2,4). Sur le PC, `test_crc32c debit`
(blocs de 4 Kio, même build) : 243 Mo/s octet par octet, 390 Mo/s par tranches de 8, 1 966 Mo/s avec
SSE4.2. Le chemin arm64 est vérifié à la compilation (clang, cible aarch64 : `crc32cb`/`crc32cx`
émises) ; reste à le passer sur le Pixel (`tools/android/run_native_tests.sh`).

Tests : `test_crc32c.c` (`test_crc32c_vecteurs`, `test_crc32c_equivalence`, `test_crc32c_debit` en
label `lent`).

### 14. Journal : blocs échappés, blocs à zéro, sommes v1

- Blocs échappés : un bloc journalisé qui commence par le magique jbd2 est écrit dans le journal avec le
  magique effacé (drapeau `ESCAPE`). La somme d'étiquette v3 porte désormais sur ce bloc échappé, comme
  jbd2 la vérifie au rejeu (avant de remettre le magique) ; avant, elle portait sur le bloc d'origine et
  le noyau comme `e2fsck` sautaient le bloc (« JBD2: Invalid checksum recovering data block »), la mise
  à jour était perdue. Constaté sans le correctif par `test_ext4_coupure_echappement`.
- Blocs de descripteur, de révocation et de commit mis à zéro avant d'être remplis : plus d'octets
  restés du cache (contenu d'autres blocs) après la dernière étiquette, dans `h_chksum[1..7]` ou
  `h_commit_sec`.
- `JBD_FEATURE_COMPAT_CHECKSUM` testé comme option compat (l'amont testait le bit incompat de même
  valeur, REVOKE ; sans effet sur un journal v3) ; somme du commit calculée avec seul `h_chksum[0]` à
  zéro, comme jbd2.

Côté colle : `ext4_policy.c` refuse l'écriture sur un journal portant une option compat (sommes v1 :
lwext4 ne les calcule pas comme jbd2, le noyau arrêterait le rejeu au premier commit) ou ro_compat.
`ext4-rw-uninit.img` (sans `metadata_csum`) a donc désormais un journal sans somme.

Tests (`test_ext4_coupure.c`) : vérificateur aligné sur jbd2 (somme d'étiquette sur le bloc du journal,
blocs de révocation, octets inutilisés nuls) ; `test_ext4_journal_echappement` (inode 1 retouché pour que
le bloc de l'inode racine commence par le magique), `test_ext4_coupure_echappement` (coupure juste après
le commit, rejeu par `e2fsck -E journal_only`), `test_ext4_barrieres` vérifie l'ordre (chaque commit
écrit seul entre deux vidages, d'après une trace de `fault_bdev`) et plus seulement le nombre de vidages ;
`test_politique_journal_options`.

### 15. Erreurs pendant une écriture

- 0008, extents : la racine de l'arbre (en-tête dans l'inode) est vérifiée comme les blocs de l'arbre
  (magique, nombre d'entrées, profondeur au plus 5) ; avant, une racine effacée se lisait comme un arbre
  vide et une suppression laissait tous les blocs alloués. Les erreurs de libération des blocs (bitmap
  illisible, ou à la somme fausse : `EBADMSG`) remontent par `ext4_ext_free_blocks`,
  `ext4_ext_remove_blocks`, `ext4_ext_remove_idx`, `ext4_ext_remove_leaf` et
  `ext4_extent_remove_space`, qui les jetaient : une troncature validait un inode sans ses extents (ou à
  la taille nulle) avec des blocs encore alloués. Dans une feuille, l'extent n'est modifié qu'après la
  libération ; un nœud d'index l'est avant (le volume est alors gelé). Un nœud d'index sans entrée est
  refusé, comme par le noyau.
- 0009, journal : à l'annulation d'une transaction, la restauration d'un bloc depuis sa copie validée
  ne copie plus depuis NULL quand cette copie est illisible (`ext4_assert` absent des builds) ; le bloc
  reste rattaché à la transaction validée (sinon, utilisation après libération au point de contrôle).

Côté colle (`ext4_glue.c`) : après un échec, l'état de lwext4 n'est plus sûr (opération en partie
faite, restauration impossible depuis le journal, `ext4_fwrite` qui valide sa transaction même en échec).
Dès qu'un volume passe en lecture seule en cours de session (erreur d'écriture, somme fausse, ou échec
d'une opération d'écriture : `RO_IOERR`, `RO_CSUM`, `RO_CORRUPT`, `RO_ABORT`), il est gelé : plus rien
n'atteint le disque, ni fermeture, ni démontage (qui rend `FL_EIO`, journal laissé à rejouer), et
`fl_fs_sync` rend `FL_EIO` (un client ne croit jamais son `fsync` réussi). Le disque garde la dernière
transaction validée. Ne gèlent pas : `ENOTSUP` et `EFBIG` (refus de `ext4_fwrite` avant toute
modification), et le disque plein (`ENOSPC` : à l'annulation, lwext4 jette du cache les blocs modifiés
et restaure depuis le journal ceux d'une transaction validée), sauf si une lecture a échoué pendant la
session. Un fichier à blocs indirects (volume converti depuis ext3) est refusé à l'ouverture en
écriture (`FL_ENOTSUP`), avant toute troncature ; les écritures dans le dernier bloc logique sont
refusées (`FL_EINVAL`) avant lwext4. `rmdir` : un dossier dont le parcours s'arrête sur la borne d'entrées
ignorées n'est plus déclaré vide (`ext4_dir_rm`, récursif, aurait supprimé la suite).

Tests (`test_ext4_politique.c`) : `test_politique_somme_en_cours` (gel jusqu'au démontage, rejeu),
`test_politique_lecture_eio_en_ecriture` (lecture impossible au milieu d'une suppression : seul le
rejeu du journal est nécessaire), `test_politique_corruption_en_ecriture` (racine d'extents effacée).

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

- `ext4_ext_split_node` : si une découpe d'arbre d'extents sur plusieurs niveaux échoue en `ENOSPC` au
  deuxième niveau, les extents déjà sortis de la feuille n'y sont pas remis, et `ext4_fwrite` valide la
  transaction (avant que la colle ne gèle). Il faut un arbre de profondeur 2 au moins avec un nœud
  d'index plein (environ 115 000 extents dans un seul fichier, blocs de 4 Kio) et un disque plein au bloc
  près. Correction propre : allouer tous les blocs avant de modifier l'arbre, comme le noyau.

- Blocs de 64 Kio sans `metadata_csum` : une entrée qui couvre tout le bloc a une longueur codée 0 ou 65535
  (65536 pour le noyau) ; `ext4_dir_find_in_block` (comme déjà `ext4_dir_iterator_set` en amont) la traite
  comme une corruption. Le vrai disque a des blocs de 4 Kio et `metadata_csum`.
- Un dossier créé par lwext4 est toujours indexé (racine htree et une feuille, 2 blocs), même vide ;
  le noyau crée un dossier linéaire d'un bloc et ne l'indexe qu'au-delà. Valide pour `e2fsck`.
- Une opération en plusieurs transactions n'est pas atomique : la suppression d'un fichier ou d'un
  dossier tronque d'abord (transactions de 16 Mio au plus), puis retire l'entrée ; une coupure entre les
  deux laisse une entrée vers un fichier vide, que `e2fsck` accepte.
- Le journal n'est jamais rejoué sur le téléphone (Ubuntu le rejoue) ; le montage en écriture d'un
  volume à rejouer est refusé (lecture seule).
- Les liens symboliques ne sont pas suivis ; `fl_fs_open` sur un lien rend `FL_ENOTSUP`.
- Le parcours d'un dossier (`readdir`) ne vérifie pas la somme de ses blocs ; la recherche par nom, si.
- Le journal n'est jamais rejoué (`needs_recovery` le signale) ; `inline_data`,
  `encrypt`, `casefold`, `largedir`, `ea_inode` rendent `FL_ENOTSUP`.
