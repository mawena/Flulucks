# FluLuks — Phase 4 : FAT12 / FAT16 / FAT32 / exFAT — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** une clé ou un disque en FAT12, FAT16, FAT32 ou exFAT, en clair ou dans LUKS1/LUKS2, s'ouvre
dans FluLuks et apparaît dans « Fichiers ». On peut le lire, et aussi y créer, modifier, renommer,
déplacer, copier et supprimer des fichiers. Après chaque session éjectée proprement, le disque sort
**propre selon `fsck.fat -n` / `fsck.exfat -n`**. Une clé arrachée sans éjection est **signalée** à la
réouverture et ouverte en lecture seule, jamais réécrite à l'aveugle.

**Architecture :**
- **FatFs.** Le moteur s'appuie sur FatFs R0.16 (ChaN), copié tel quel dans
  `native/third_party/fatfs/`, avec notre `ffconf.h`.
- **Colle `fat_glue.c`.** Elle implémente `fl_fs_ops`, comme `ext4_glue.c`. Tout appel à FatFs passe
  sous un verrou global, car FatFs a des variables globales.
- **Accès disque `fat_diskio.c`.** Il relie FatFs à `fl_bdev`. Il convertit la taille de secteur du
  système de fichiers et regroupe les écritures contiguës (`fl_wbuf`, comme le correctif 0.2.1).
- **Ce que FatFs ne fait pas, la colle le fait :**
  - le bit « volume sale », lu au montage et posé pendant les écritures (§ 4.2) ;
  - la somme de contrôle du secteur de démarrage exFAT ;
  - les trous lus comme des zéros ;
  - la limite de 4 Gio du FAT ;
  - le partage d'un même fichier ouvert plusieurs fois ;
  - le refus des noms que FatFs modifierait en silence.
- **Couches au-dessus.** JNI, Kotlin et Flutter changent peu : ils acceptent les nouveaux types et une
  nouvelle erreur (`FL_EFBIG`), et le renommage qui ne change que la casse.

**Tech Stack :**
- C11, FatFs R0.16, CMake/CTest avec ASan/UBSan ;
- les juges sur PC : dosfstools 4.2 (`mkfs.vfat`, `fsck.fat`), exfatprogs 1.2.2 (`mkfs.exfat`,
  `fsck.exfat`), mtools 4.0.43 (`mcopy`), et un lecteur indépendant en Python 3.12 ;
- Kotlin, et Flutter 3.41.4 avec Pigeon 27.3.0.

**Spec :** [docs/superpowers/specs/2026-10-17-fluluks-design.md](../specs/2026-10-17-fluluks-design.md)
(§ 2, § 4.1.3, § 4.1.5, § 4.1.6, **§ 4.2**, § 4.3, § 6.6, § 7).

## Global Constraints

- **Branche et commits.**
  - Branche `feature/fat-exfat`, créée depuis `develop`.
  - Les commits portent l'identité git de l'utilisateur, avec un message en français (Conventional
    Commits) et **sans aucune attribution à Claude**.
  - Un commit par tâche, plus les commits de correction.
  - On coche la tâche dans la feuille de route (`docs/superpowers/plans/2026-10-17-fluluks-feuille-de-route.md`,
    section Phase 4) **dans le même commit**.
- **Ne jamais corrompre le disque** (anti-objectif n° 1). En cas de doute, le volume passe en lecture
  seule, avec une raison affichée. Aucune tâche ne relâche une règle du § 4.2.
- **FatFs R0.16 tel que publié.**
  - Archive `https://elm-chan.org/fsw/ff/arc/ff16.zip`, SHA-256
    `99f7dc1f7e095356e4a9e3dbe29959090d8b948afe2bbc5441e52fdf4b85449e`.
  - On copie `source/ff.c`, `ff.h`, `diskio.h`, `ffunicode.c`, `ffsystem.c` et `LICENSE.txt` **sans
    modification** dans `native/third_party/fatfs/`. On ne copie ni `ffconf.h` ni `diskio.c` d'amont.
  - Notre configuration est `native/cmake/fatfs/ffconf.h`. Si FatFs doit être corrigé, le correctif va
    dans `native/third_party/patches/fatfs/000N-<sujet>.patch`, appliqué comme pour lwext4
    (`native/cmake/third_party.cmake`). On ne modifie jamais la copie.
- **Configuration FatFs** (`ffconf.h`, `FFCONF_DEF 80386`), *décision du planificateur* :
  - Fonctions et noms :
    - `FF_FS_READONLY 0`, `FF_FS_MINIMIZE 0`, `FF_USE_FASTSEEK 1`, `FF_USE_LABEL 1` ;
    - `FF_USE_MKFS 0`, `FF_USE_FIND 0`, `FF_USE_EXPAND 0`, `FF_USE_CHMOD 0`, `FF_USE_STRFUNC 0` ;
    - `FF_CODE_PAGE 437` (celle de Linux par défaut), `FF_USE_LFN 3` (sur le tas, sûr entre fils),
      `FF_MAX_LFN 255`, `FF_LFN_UNICODE 2` (UTF-8), `FF_LFN_BUF 255`, `FF_SFN_BUF 12` ;
    - `FF_FS_RPATH 0`.
  - Volumes et secteurs :
    - `FF_VOLUMES 8`, `FF_STR_VOLUME_ID 0`, `FF_MULTI_PARTITION 0` ;
    - `FF_MIN_SS 512`, `FF_MAX_SS 4096`, `FF_LBA64 1`, `FF_USE_TRIM 0` ;
    - `FF_FS_TINY 0`, `FF_FS_EXFAT 1`.
  - Heure et état :
    - `FF_FS_NORTC 0`, `FF_FS_CRTIME 0` ;
    - `FF_FS_NOFSINFO 3` : on ne croit pas le FSInfo d'un disque hostile. Un compte de clusters libres
      faux à 0 bloquerait toute allocation.
    - `FF_FS_LOCK 0` : la colle gère elle-même le partage des fichiers ouverts. `FF_FS_REENTRANT 0`.
- **Juges.** Ce sont des outils extérieurs au moteur, PC seulement, qui jugent après chaque série :
  - `fsck.fat -n` et `fsck.exfat -n` doivent sortir avec le code **0, sans aucune ligne d'erreur** ;
  - `tools/fixtures/fatjuge.py` lit l'image sans FatFs et doit donner le même manifeste que le modèle.

  Les tests qui les appellent portent le label CTest `ecriture`. Ils se sautent (code 77) si un outil
  manque ou sur Android.
- **Images de test.**
  - Elles sont générées sans root, dans `build/native/fixtures/`, et jamais commitées.
  - Un test d'écriture n'écrit que sur une copie de l'image (`fu_copy_image`), supprimée même en échec.
  - Aucune vérification par le noyau n'est faite par un agent. Le script sudo est fourni, et c'est
    l'utilisateur qui le lance.
- **Heure.** Les horodatages FAT sont en **heure locale du téléphone** (`localtime_r` / `mktime`),
  comme Linux et Windows par défaut. Ils sont bornés entre 1980-01-01 et 2107-12-31, avec une
  résolution de 2 s. Les tests posent `TZ=UTC` (`setenv` + `tzset`).
- **Ce qui ne change pas sur FAT.** FAT n'a ni propriétaire ni droits. `fl_stat` rend `uid 0`, `gid 0`,
  `mode 0100644` pour un fichier (`0100444` s'il a l'attribut lecture seule) et `040755` pour un dossier.
  Les liens symboliques n'existent pas.
- **Restent valables :**
  - l'entrée disque et l'entrée des autres applications sont **hostiles** ;
  - les noms traversent JNI en octets UTF-8 ;
  - interface en français, avec les chaînes exactes de ce plan ;
  - pas de `printf` dans la bibliothèque ;
  - `-Wall -Wextra -Werror` sur nos sources (FatFs est compilé avec `-w`) ;
  - ASan/UBSan sans aucun rapport.
- **Appareil.**
  - Pixel 4 XL, `192.168.0.104:5555`.
  - Les agents n'écrivent que sous `/data/local/tmp/fluluks/`.
  - Les essais sur clé (T10) sont faits **par l'utilisateur**, sur une clé dont le contenu est sauvegardé.
- **Nouveaux textes, au plus 127 octets, texte exact** *(décision du planificateur)* :
  - `FL_RO_DIRTY` = « disque mal éjecté : vérifiez-le sur un ordinateur (chkdsk ou fsck) avant d'y écrire »
  - `FL_RO_MEDIA` = « erreurs de support signalées par le volume : vérifiez-le sur un ordinateur »
  - `FL_RO_SECTOR` = « taille de secteur non prise en charge en écriture »
  - `FL_EFBIG = -18`, avec le message `fl_strerror` « fichier trop gros pour ce système de fichiers »
  - Message du DocumentsProvider pour `FL_EFBIG` : « Fichier trop gros pour ce disque (4 Gio au plus en FAT) »
  - Message du DocumentsProvider pour `FL_EINVAL` sur créer, renommer, déplacer ou copier : « Nom non
    autorisé sur ce disque »

## Review Focus

1. **Clé arrachée sans « Éjecter » en pleine copie.** C'est l'habitude de beaucoup d'utilisateurs
   Android. Attendu :
   - à la réouverture, lecture seule avec `FL_RO_DIRTY` ;
   - sur PC, `fsck.fat -a` / `fsck.exfat -y` répare, puis `-n` est propre ;
   - les fichiers fermés avant la coupure sont intacts.

   *Tests : T7 `test_fat_coupure_*`.*
2. **Vidéo de plus de 4 Gio copiée vers une clé FAT32.** Attendu :
   - `FL_EFBIG` à l'écriture qui franchit la limite, sans écrire 4 Gio de zéros ;
   - un système de fichiers propre, et le fichier partiel lisible et supprimable ;
   - côté client, l'errno `EFBIG` et le message de la contrainte.

   *Tests : T4 `test_fat_fichier_4gio`, T9 `errnoForEfbig`.*
3. **Noms venus d'autres applications sur un système de fichiers insensible à la casse.** Les cas visés :
   - « Photo.jpg » quand « photo.jpg » existe : attendu un nom unique « Photo (1).jpg » ;
   - `: * ? " < > | \` : attendu une erreur « Nom non autorisé sur ce disque » ;
   - « nom. » ou « nom  » : attendu le refus, et non un renommage silencieux ;
   - le renommage « a.txt » → « A.txt » : attendu qu'il fonctionne.

   *Tests : T5 `test_fat_noms_*`, T9 `WriteOpsTest.renameCaseOnly`.*
4. **Disque FAT hostile ou abîmé.** Les cas visés : chaîne de clusters en boucle, cluster hors bornes,
   BPB incohérent, somme d'un ensemble d'entrées exFAT fausse. Attendu : ni plantage, ni boucle infinie,
   ni rapport ASan ; une erreur propre ou la lecture seule. *Tests : T8 `test_fat_hostile`.*
5. **Le même fichier ouvert deux fois** (VLC lit pendant que « Fichiers » écrit, ou deux lecteurs).
   Attendu : une seule vue cohérente. Le lecteur voit les octets écrits, et la taille et le contenu sont
   justes après la fermeture des deux. *Tests : T4 `test_fat_partage_*`.*

---

## Arborescence

```
native/third_party/fatfs/{ff.c,ff.h,diskio.h,ffunicode.c,ffsystem.c,LICENSE.txt}   (T2, amont R0.16)
native/cmake/fatfs/ffconf.h   native/cmake/third_party.cmake                       (T2)
native/src/fs/fat_diskio.c fat_diskio.h                                            (T2, T6)
native/src/fs/fat_glue.c                                                           (T2–T5)
native/src/fs/fs.c fs_internal.h ro_reasons.h   native/src/probe/probe.c           (T2)
native/include/fluluks/error.h fs.h probe.h   native/src/error.c                   (T2, T4)
native/tests/support/{fsck_util.c,fsck_util.h,tree_model.c,tree_model.h,fault_bdev.c,fault_bdev.h} (T1, T6)
native/tests/{test_fat_banc.c,test_fat.c,test_fat_ecriture.c,test_fat_coupure.c,
              test_fat_batterie.c,test_fat_hostile.c}                              (T1–T8)
tools/fixtures/{make_fixtures.sh,make_tree.py,fatjuge.py,verify_fat_with_kernel.sh} (T1, T8)
tools/android/run_native_tests.sh                                                  (T3)
native/jni/handles.c   native/tools/fluluks_cli.c                                  (T9)
android/app/src/main/kotlin/com/mawena/fluluks/{engine,provider,volumes}/…         (T9)
README.md   native/third_party/PATCHES.md                                          (T11)
```

---

### Task 1 : Images FAT/exFAT et juges indépendants

Ici, aucun code du moteur ne change. Cette tâche prépare ce qui jugera les tâches suivantes, sans FatFs.

**Files :**
- Create : `tools/fixtures/fatjuge.py`, `native/tests/test_fat_banc.c`
- Modify : `tools/fixtures/make_tree.py`, `tools/fixtures/make_fixtures.sh` (ajouter `fatjuge.py` au
  calcul de `STAMP`), `native/tests/support/fsck_util.{c,h}`, `native/tests/support/tree_model.{c,h}`,
  `native/CMakeLists.txt`

**Interfaces :**
- Produces :
  - **`python3 -I tools/fixtures/fatjuge.py manifest <img>`** : lit FAT12, FAT16, FAT32 ou exFAT
    (détecté par le secteur de démarrage) **sans aucune bibliothèque FAT**, et écrit sur stdout le
    manifeste au format exact de `tm_write_manifest` :
    - `uid` et `gid` valent 0, `mode` vaut `100644`, `100444` (attribut lecture seule) ou `40755` ;
    - la ligne de la racine est incluse, l'étiquette de volume et les entrées `.` / `..` ne le sont pas ;
    - une erreur de structure (chaîne en boucle, cluster hors bornes, somme exFAT fausse) donne le code
      de sortie 3 et le message sur stderr.

    Pour FAT, le nom est le nom long (LFN) s'il existe et si sa somme correspond au nom court. Sinon,
    c'est le nom court 8.3, avec les bits de casse NT (octet 12 : 0x08 pour le corps en minuscules, 0x10
    pour l'extension).
  - **`python3 -I tools/fixtures/fatjuge.py free <img>`** : nombre d'octets libres (clusters libres ×
    taille de cluster), compté sur la FAT ou sur le bitmap exFAT.
  - **`python3 -I tools/fixtures/fatjuge.py locate <img> <chemin>`** : offset en octets, dans l'image, du
    premier cluster de données du fichier.
  - **`python3 -I tools/fixtures/fatjuge.py set-ro <img> <chemin>`** : pose l'attribut lecture seule
    (0x01) sur l'entrée du fichier. Pour exFAT, il recalcule la somme de l'ensemble d'entrées.
  - **`python3 -I tools/fixtures/fatjuge.py build-exfat <img> <dossier>`** : remplit une image fraîche de
    `mkfs.exfat` avec le contenu du dossier, sans FatFs.
    - Il alloue dans le bitmap. Les fichiers de plus d'un cluster sont contigus avec `NoFatChain`, sauf
      un fichier sur trois, chaîné dans la FAT : les deux chemins de lecture sont couverts.
    - Les dossiers sont toujours chaînés.
    - Il calcule le hachage des noms (table de majuscules lue sur l'image) et la somme de chaque
      ensemble d'entrées.
    - Les horodatages sont ceux du dossier source, en UTC.
  - `fsck_util.h` :
    - `int fu_fat_tools_available(void)` : 1 si `mkfs.vfat`, `fsck.fat`, `mkfs.exfat`, `fsck.exfat`,
      `mcopy` et `python3` sont présents ;
    - `int fu_fsck_fat_n(const char *img, char *out, size_t cap)` : choisit `fsck.fat -n` ou
      `fsck.exfat -n` d'après l'image. Il rend le code de sortie, ou 1 si la sortie contient une ligne
      d'erreur alors que le code vaut 0 ;
    - `int fu_fsck_fat_repair(const char *img, char *out, size_t cap)` : `fsck.fat -a` ou
      `fsck.exfat -y`, puis le `-n`. Il rend le code du `-n` ;
    - `int fu_fat_manifest(const char *img, const char *out_path)` : lance `fatjuge.py manifest` vers un
      fichier. Il rend 0, 3 pour une structure invalide, ou -1 ;
    - `int64_t fu_fat_free(const char *img)`.
  - `tree_model.h` :
    - `void tm_set_fat_attrs(tm *m)` : pose `uid 0`, `gid 0`, `0100644` et `040755` partout ;
    - `int tm_check_fat_judge(const tm *m, const char *img)` : manifeste du modèle comparé octet pour
      octet à celui de `fatjuge.py`. Il rend le nombre de lignes différentes, ou -1. Les dix premières
      vont sur stderr.
  - **Images** (dans `make_fixtures.sh`, `TZ=UTC`, `LC_ALL=C.UTF-8` pour mtools) :
    - **Arbre FAT** : `make_tree.py --fat <dossier>` produit `tree-fat/`, une arborescence valide sur FAT.
      Elle contient :
      - `été ☀.txt`, `😀.bin` ;
      - un nom de 255 octets UTF-8 : « é » × 125 + « n.txt », soit 250 + 5 octets et 130 unités UTF-16 ;
      - 20 niveaux de dossiers `p1/…/p20/fond.txt` ;
      - `mille/` avec 1000 fichiers `f0000.txt`…`f0999.txt` ;
      - `gros.bin` (3 MiB pseudo-aléatoire, graine fixe) et `vide.txt` (0 octet) ;
      - `MiXeD.TxT`, `abc.txt` (nom court en minuscules), `UPPER.TXT` ;
      - `nom long 1.txt` et `nom long 2.txt` (noms courts `~1` / `~2`).
    - **Images remplies de l'arbre FAT** :
      - `fat12-arbre.img` (8 MiB, `mkfs.vfat -F 12`), puis `mcopy -s -m -i <img> tree-fat/* ::/`. Elle
        n'a pas `mille/` ni `gros.bin` : FAT12 manque de place ;
      - `fat16-arbre.img` (32 MiB, `-F 16`) ;
      - `fat32-arbre.img` (64 MiB, `-F 32`, étiquette `FLUFAT32`) ;
      - `fat32-4k.img` (256 MiB, `-F 32 -S 4096` : secteurs FAT de 4096 octets) ;
      - `exfat-arbre.img` (64 MiB, `mkfs.exfat -L FLUEXFAT`, puis `build-exfat`) ;
      - `luks2-fat32.img` : le contenu de `fat32-arbre.img` chiffré par `luks_encrypt.py` (LUKS2,
        PBKDF2, phrase `pass.txt`).
    - **Images vides** :
      - `fat32-vide.img` (64 MiB, clusters de 4 Kio : `-s 8`) ;
      - `exfat-vide.img` (64 MiB, clusters de 4 Kio : `-c 4096`) ;
      - `exfat-grand.img` (creux, 6 GiB, vide) ;
      - `fat32-512-sur-4k.img` (`-S 512`), ouverte dans les tests avec `fl_file_bdev_open(…, 4096, …)`.
    - **Images sales** (copies d'images d'arbre, un seul octet modifié par `printf | dd conv=notrunc`) :
      - `fat16-sale.img` : octet 0x25 bit 0 ;
      - `fat32-sale.img` : octet 0x41 bit 0 ;
      - `fat32-fat1-sale.img` : FAT[1] bit 27 à 0 ;
      - `fat32-media.img` : FAT[1] bit 26 à 0 ;
      - `exfat-sale.img` : octet 106 bit 1 ;
      - `exfat-media.img` : octet 106 bit 2 ;
      - `exfat-somme.img` : octet 100, numéro de série, inversé. Il est couvert par la somme du secteur 11.

- [ ] **Step 1 : écrire `test_fat_banc.c` (label `ecriture`, saut 77 sans `fu_fat_tools_available`).**
  Le banc doit d'abord se juger lui-même :
  - `test_banc_fixtures_propres` : `fu_fsck_fat_n` rend 0 sur `fat12/16/32-arbre`, `fat32-4k`,
    `exfat-arbre`, `fat32-vide`, `exfat-vide`.
  - `test_banc_manifeste_arbre` : un modèle `tm_from_dir("tree-fat")` puis `tm_set_fat_attrs` donne
    `tm_check_fat_judge == 0` sur `fat16-arbre`, `fat32-arbre`, `fat32-4k` et `exfat-arbre`. Pour
    `fat12-arbre`, on enlève d'abord `mille/` et `gros.bin` du modèle.
  - `test_banc_voit_contenu` : un octet de données de `gros.bin` inversé sur une copie donne
    `tm_check_fat_judge ≥ 1`. Il se trouve au cluster obtenu par
    `fatjuge.py locate`.
  - `test_banc_voit_fat_cassee` : sur une copie, la FAT de `gros.bin` est mise en boucle (son premier
    cluster pointe sur lui-même). Attendu : `fu_fsck_fat_n ≠ 0` et `fu_fat_manifest == 3`.
  - `test_banc_voit_somme_exfat` : sur `exfat-somme.img`, `fu_fsck_fat_n ≠ 0`.
  - `test_banc_libre` : `fu_fat_free(fat32-vide)` est égal au nombre de clusters libres donné par
    `fsck.fat -n -v`, multiplié par la taille de cluster.
- [ ] **Step 2 : lancer `ctest --test-dir build/native -R test_fat_banc` et vérifier qu'il ÉCHOUE**
  (fixtures ou outils absents).
- [ ] **Step 3 : écrire `fatjuge.py`, `make_tree.py --fat`, les images, et les fonctions de
  `fsck_util` / `tree_model`.**
- [ ] **Step 4 : relancer `test_fat_banc`. Attendu : PASS. Puis `ctest -L ecriture -LE lent` : aucune
  régression.**
- [ ] **Step 5 : commit** `test(fat): images FAT/exFAT et juges indépendants (fsck, fatjuge.py)`.

---

### Task 2 : FatFs dans le moteur — montage, statfs, politique de lecture seule

**Files :**
- Create : `native/third_party/fatfs/*` (amont, voir Global Constraints), `native/cmake/fatfs/ffconf.h`,
  `native/src/fs/fat_diskio.c`, `native/src/fs/fat_diskio.h`, `native/src/fs/fat_glue.c`,
  `native/tests/test_fat.c`
- Modify : `native/cmake/third_party.cmake` (cible `fl_fatfs`, `-w`, include `native/cmake/fatfs` puis
  `third_party/fatfs`), `native/CMakeLists.txt` (`fat_diskio.c` et `fat_glue.c` dans `FL_SOURCES`,
  `fl_add_test(test_fat)`), `native/src/fs/fs.c` (table `k_backends`),
  `native/src/fs/fs_internal.h`, `native/src/fs/ro_reasons.h`, `native/src/probe/probe.c`,
  `native/include/fluluks/probe.h`, `native/include/fluluks/fs.h` (commentaire d'en-tête)

**Interfaces :**
- Produces :
  - `int fl_fat_mount(fl_bdev *dev, bool want_rw, fl_fs **out);` (dans `fs_internal.h`). Il est
    enregistré dans `k_backends` pour `FL_CONTENT_FAT12`, `FAT16`, `FAT32` et `EXFAT`.
  - `fl_content_supported` rend vrai aussi pour FAT12, FAT16, FAT32 et exFAT.
  - **`fat_diskio.h`**, interne :

    ```c
    typedef struct fat_disk fat_disk;
    /* Rattache dev à l'emplacement FatFs pdrv (0..FF_VOLUMES-1) ; ss = taille de secteur du système de
     * fichiers (512..4096, puissance de 2). ss multiple de dev->sector_size : lecture et écriture ;
     * ss plus petit : lecture seule (lecture du secteur englobant). */
    int fat_disk_attach(uint8_t pdrv, fl_bdev *dev, uint32_t ss, bool writable, fat_disk **out);
    void fat_disk_detach(fat_disk *d);
    void fat_disk_set_writable(fat_disk *d, bool writable); /* faux : disk_status rend STA_PROTECT */
    int fat_disk_error(const fat_disk *d);                  /* première erreur fl_err d'écriture, collante */
    ```

    `disk_ioctl` répond à `CTRL_SYNC` (vidage du tampon de T6, puis `fl_bdev_flush`), à
    `GET_SECTOR_SIZE` (ss) et à `GET_SECTOR_COUNT` (taille de dev / ss).
  - **Montage `fl_fat_mount`**, sous le verrou global `g_fat_lock` :
    1. Lire le secteur 0 par `fl_bdev_read_bytes`. ss vaut `BPB_BytsPerSec` pour FAT, ou
       `1 << BytesPerSectorShift` pour exFAT. Une valeur hors de 512..4096, ou qui n'est pas une
       puissance de 2, donne `FL_ECORRUPT`.
    2. Prendre un emplacement FatFs libre (8 au plus, sinon `FL_EBUSY`), appeler `fat_disk_attach`, puis
       `f_mount(fs, "N:", 1)`. `FR_NO_FILESYSTEM` donne `FL_ECORRUPT`, `FR_DISK_ERR` donne `FL_EIO`.
    3. Appliquer la politique d'écriture. Elle n'est évaluée que si `want_rw`. Le premier cas vrai fixe la
       raison, et le volume reste lisible :
       - exFAT, somme des secteurs 0–10 ≠ secteur 11 → `FL_RO_CSUM` ;
       - ss < `dev->sector_size` → `FL_RO_SECTOR` ;
       - « sale » : FAT16 octet 0x25 bit 0, FAT32 octet 0x41 bit 0, FAT[1] bit 15 (FAT16) ou bit 27
         (FAT32) à 0, exFAT `VolumeFlags` bit 1 → `FL_RO_DIRTY` ;
       - « support » : FAT[1] bit 14 (FAT16) ou bit 26 (FAT32) à 0, exFAT `VolumeFlags` bit 2 →
         `FL_RO_MEDIA`.
    4. FAT12 n'a pas de bits d'état : il s'écrit sans ce contrôle.
  - **`statfs`** :
    - le total et l'espace libre viennent de `f_getfree` (le premier appel parcourt la FAT, puisque
      `FF_FS_NOFSINFO 3`) ;
    - l'étiquette vient de `f_getlabel`, débarrassée des espaces de fin ;
    - `needs_recovery` vaut toujours faux.
  - **`unmount`** : `FL_EINVAL` si un fichier est ouvert. Sinon `f_sync` de rien, puis l'état propre
    (T4), puis `f_unmount`, `fat_disk_detach`, et la libération de l'emplacement.

- [ ] **Step 1 : écrire les tests de montage dans `test_fat.c`** (pas de label : ils tournent aussi sur
  Android, avec les fixtures poussées). Chaque test monte l'image par `fl_file_bdev_open` puis
  `fl_fs_mount(dev, <kind sondé>, true, &fs)`.
  - `test_fat_montage_types` : pour `fat12/16/32-arbre`, `fat32-4k` et `exfat-arbre` : `FL_OK`, non
    `read_only`, et `ro_reason == ""`. Le `label` vaut `FLUFAT32` et `FLUEXFAT` sur les images
    étiquetées. `total_bytes` est égal au nombre de clusters de données × la taille de cluster, deux
    valeurs que le test calcule lui-même depuis le BPB.
  - `test_fat_libre` : `free_bytes(fat32-vide) == fu_fat_free(...)` quand les outils sont présents (sauté
    sinon).
  - `test_fat_sale` : `fat16-sale`, `fat32-sale`, `fat32-fat1-sale` et `exfat-sale` donnent `read_only`
    avec `ro_reason == FL_RO_DIRTY`. `fat32-media` et `exfat-media` donnent `FL_RO_MEDIA`.
    `exfat-somme` donne `FL_RO_CSUM`. Toutes restent lisibles (`fl_fs_stat("/gros.bin")` réussit).
  - `test_fat_secteur_512_sur_4k` : `fat32-512-sur-4k` monté sur un périphérique de 4096 octets donne
    `read_only` avec `FL_RO_SECTOR`, et `/` reste listable.
  - `test_fat_lecture_seule_demandee` : `want_rw = false` donne `FL_RO_USER`, et
    `fl_fs_mkdir == FL_EROFS`.
  - `test_fat_emplacements` : 8 montages simultanés réussissent, le 9e rend `FL_EBUSY`. Après 8
    démontages, 8 nouveaux montages réussissent (pas de fuite d'emplacement, y compris après un échec de
    montage sur `sig-exfat.img`, qui doit rendre `FL_ECORRUPT`).
  - `test_fat_raisons_tiennent` : chaque nouvelle raison fait au plus 127 octets.
- [ ] **Step 2 : lancer `test_fat`. Attendu : il ÉCHOUE (`FL_ENOTSUP`).**
- [ ] **Step 3 : implémenter** :
  - copie de FatFs ;
  - `ffconf.h` ;
  - `fat_diskio.c` (pas encore de tampon) ;
  - dans `fat_glue.c` : montage, `statfs`, démontage, et `get_fattime()` (`fl_fs_now()` puis
    `localtime_r`, borné) ;
  - les raisons et le sondage.
- [ ] **Step 4 : lancer `test_fat` et `test_probe`. Attendu : PASS. Puis `ctest -LE lent` complet.
  Compiler aussi pour arm64 :** `cmake --build build/android-arm64`.
- [ ] **Step 5 : commit** `feat(fat): montage FAT12/16/32 et exFAT par FatFs R0.16, lecture seule si sale`.

---

### Task 3 : Lecture — stat, readdir, open, pread

**Files :**
- Modify : `native/src/fs/fat_glue.c`, `native/tests/test_fat.c`, `tools/android/run_native_tests.sh`
  (ajouter `test_fat` à `TESTS`, et les images `fat*-arbre.img`, `fat32-4k.img`, `exfat-arbre.img`,
  `luks2-fat32.img` et `tree-fat/gros.bin` à `FIXTURES`)

**Interfaces :**
- Produces : `stat`, `readdir`, `open` (lecture), `pread` et `close` de `fl_fs_ops`, sur FAT.
  - **Chemins.** On ajoute le préfixe `"N:"` au chemin validé. La racine `/` a un `stat` synthétique
    (`FL_FT_DIR`, `040755`, `mtime 0`), car `f_stat` n'accepte pas la racine.
  - **`readdir`.** Le parcours se fait par `f_opendir` / `f_readdir`. On ignore `.`, `..` et les entrées
    vides, et on ne rappelle pas l'utilisateur sous `g_fat_lock`. Les entrées sont lues par lots de 64
    sous le verrou, et les rappels se font entre les lots ; on rouvre le dossier avec l'index de reprise.
  - **Noeuds partagés.** Un fichier ouvert plusieurs fois a **un seul `FIL`**, partagé :
    - La clé d'un noeud vaut `(dir_sect, dir_ptr - fs->win)` pour FAT, et `(obj.c_scl, obj.c_ofs)` pour
      exFAT. Elle se calcule par un `f_open(FA_READ)` temporaire, aussitôt fermé si le noeud existe déjà.
    - Le noeud est ouvert en `FA_READ|FA_WRITE` si le volume est en écriture et que le fichier n'a pas
      l'attribut lecture seule, sinon en `FA_READ`.
    - Le noeud compte ses poignées et ses écrivains. `fl_file` pointe sur son noeud.
  - **`pread`.** Il fait `f_lseek` puis `f_read` sous le verrou. Il lit 0 au-delà de la taille.
  - **Recherche rapide.** Un noeud sans écrivain construit à la première lecture une table de clusters
    (`cltbl`, `CREATE_LINKMAP`), d'au plus 4096 mots. S'il manque de place (`FR_NOT_ENOUGH_CORE`), il
    s'en passe. La table est libérée (`cltbl = NULL`) dès qu'un écrivain s'attache.
  - **Erreurs.**

    | FatFs | fl_err |
    |---|---|
    | `FR_NO_FILE`, `FR_NO_PATH` | `FL_ENOENT` |
    | `FR_INVALID_NAME` | `FL_EINVAL` |
    | `FR_DENIED` (cas par cas, voir T4 et T5) | — |
    | `FR_EXIST` | `FL_EEXIST` |
    | `FR_WRITE_PROTECTED` | `FL_EROFS` |
    | `FR_DISK_ERR` | `FL_EIO` |
    | `FR_INT_ERR` | `FL_ECORRUPT` |
    | `FR_NOT_ENOUGH_CORE` | `FL_ENOMEM` |
    | autres | `FL_EIO` |

    Un dossier ouvert comme fichier donne `FL_EISDIR`. Un composant intermédiaire qui est un fichier
    donne `FL_ENOTDIR`.
  - **`fl_stat`.** `mtime` est la date FAT, en heure locale, convertie par `mktime`.

- [ ] **Step 1 : écrire les tests de lecture dans `test_fat.c`.**
  - `test_fat_arbre_<type>` pour `fat12/16/32-arbre`, `fat32-4k`, `exfat-arbre` et `luks2-fat32`
    (déverrouillé avec `pass.txt`) : `tm_check_fl(modèle de tree-fat, fs) == 0`. Le modèle est construit
    par `tm_from_dir` puis `tm_set_fat_attrs`, sans `mille/` ni `gros.bin` pour FAT12. Ce test compare le
    contenu entier.
  - `test_fat_noms` :
    - `readdir("/")` donne exactement les noms UTF-8 de l'arbre ;
    - `stat` du nom de 255 octets réussit ;
    - `abc.txt` est rendu en minuscules et `UPPER.TXT` en majuscules ;
    - `stat("/ABC.TXT")` et `stat("/mixed.txt")` réussissent (insensible à la casse).
  - `test_fat_mtime` : `stat("/vide.txt").mtime` est égal au `mtime` du fichier source arrondi aux 2 s
    inférieures.
  - `test_fat_types` : `open` d'un dossier donne `FL_EISDIR`, `stat("/gros.bin/x")` donne `FL_ENOTDIR`,
    `readdir` d'un fichier donne `FL_ENOTDIR`, et `stat("/absent")` donne `FL_ENOENT`.
  - `test_fat_readdir_rappel` : le rappel rappelle `fl_fs_stat` sur chaque entrée de `/mille`, sans
    interblocage, avec 1000 entrées vues. Un rappel qui rend 7 arrête le parcours, et `readdir` rend 7.
  - `test_fat_deux_lecteurs` : deux `fl_file` sur `/gros.bin` lisent en entrelacé des zones différentes,
    avec un contenu juste.
  - `test_fat_lecture_aleatoire` (`fat32-arbre`) : un compteur de lectures `fl_fault_bdev` (voir T6
    `fl_fault_bdev_reads`, ajouté ici) entoure le périphérique. 500 `pread` de 4 Kio à des positions
    pseudo-aléatoires de `/gros.bin` font **au plus 2 lectures de périphérique par `pread`** en moyenne,
    après la première.
- [ ] **Step 2 : lancer `test_fat`. Attendu : il ÉCHOUE.**
- [ ] **Step 3 : implémenter les opérations de lecture, les noeuds partagés, la recherche rapide et
  `fl_fault_bdev_reads`.**
- [ ] **Step 4 : lancer `test_fat` sur PC. Attendu : PASS. Puis
  `tools/android/run_native_tests.sh 192.168.0.104:5555`. Attendu : tout passe sur le Pixel.**
- [ ] **Step 5 : commit** `feat(fat): lecture (stat, readdir, pread), noeuds partagés, recherche rapide`.

---

### Task 4 : Écriture des fichiers — pwrite, truncate, close, sync, état propre

**Files :**
- Modify : `native/src/fs/fat_glue.c`, `native/include/fluluks/error.h` (`FL_EFBIG = -18`),
  `native/src/error.c`, `native/tests/test_error.c`, `native/include/fluluks/fs.h` (documenter
  `FL_EFBIG` sur `pwrite` et `truncate`)
- Create : `native/tests/test_fat_ecriture.c` (label `ecriture`)

**Interfaces :**
- Produces : `open` avec `FL_O_RDWR` / `FL_O_CREAT` / `FL_O_TRUNC`, `pwrite`, `truncate`, `close` et
  `sync` sur FAT.
  - **Ouverture.**
    - `FL_O_CREAT` sur un fichier absent : `f_open(FA_CREATE_NEW|FA_READ|FA_WRITE)`. Un parent absent
      donne `FL_ENOENT`, un parent qui n'est pas un dossier donne `FL_ENOTDIR`.
    - `FL_O_TRUNC` : `f_truncate` sur le noeud partagé, à 0.
    - `FL_O_RDWR` sur un fichier qui a l'attribut lecture seule : `FL_EROFS`.
  - **`pwrite`.**
    - FAT12, FAT16 ou FAT32 avec `off + len > 0xFFFFFFFF` : **`FL_EFBIG` d'emblée**, sans rien écrire.
    - Si `off` dépasse la taille, la colle écrit d'abord des zéros de la taille jusqu'à `off`, par
      tranches de 64 Kio. FatFs laisse cette zone indéfinie.
    - Puis `f_lseek` et `f_write`. Un compte court (`bw < btw`) donne `FL_ENOSPC`. Le fichier garde ce
      qui a été écrit (contrat de `fs.h`).
  - **`truncate`.**
    - Vers le haut : des zéros, comme ci-dessus, avec la même limite `FL_EFBIG`.
    - Vers le bas : `f_lseek` puis `f_truncate`.
  - **`close`.** Pour un fichier écrit ou tronqué, `f_sync` du noeud, puis `fl_fs_sync`, dont le code est
    rendu. Le noeud est fermé à la dernière poignée.
  - **`sync`.** `f_sync` de chaque noeud modifié, puis `CTRL_SYNC`, puis l'**état propre**.
  - **État « sale » posé par FluLuks** (§ 4.2 ; FatFs ne le gère pas) :
    - **Avant la première modification d'une session propre** (création, `pwrite`, `truncate`, `mkdir`,
      `unlink`, `rmdir`, `rename`), la colle écrit le drapeau sale dans le secteur 0, puis vide le cache
      du périphérique. Le drapeau est l'octet 0x25 ou 0x41 bit 0 pour FAT16 ou FAT32, et `VolumeFlags`
      bit 1 pour exFAT. Rien pour FAT12.
    - **À la fin de `sync` et au démontage**, quand tout est écrit, la colle efface le drapeau, puis vide
      le cache à nouveau.
    - **Le secteur 0 est écrit par `fl_bdev` directement**, fenêtre de FatFs propre. Après l'écriture, il
      faut invalider la fenêtre : `fs->winsect = (LBA_t)-1`. FatFs relit lui-même le secteur 0 avant d'y
      écrire `PercInUse` (`sync_fs`, ff.c l. 1133).
    - **On ne touche jamais FAT[1]**, que FatFs garde dans sa fenêtre.
  - **Erreurs et lecture seule collante.**
    - Une erreur d'écriture du périphérique (`fat_disk_error ≠ 0`, ou `FR_DISK_ERR` sur une opération
      d'écriture) donne `fl_fs_go_read_only(FL_RO_IOERR)` et `fat_disk_set_writable(false)`.
    - `FR_INT_ERR` pendant une écriture donne `FL_RO_CORRUPT`.
    - Le drapeau sale reste alors posé.

- [ ] **Step 1 : écrire `test_fat_ecriture.c`.** Chaque test travaille sur une copie de `fat32-vide`, de
  `exfat-vide` et de `fat16-arbre`. Il finit par `fl_fs_unmount`, `fu_fsck_fat_n == 0` et
  `tm_check_fat_judge == 0`, le modèle tenu à jour par `tm_apply_*`.
  - `test_fat_creer_ecrire` : créer `/a.txt`, écrire 10 000 octets en 7 `pwrite` irréguliers, fermer,
    rouvrir, relire : identique.
  - `test_fat_trou` :
    - `pwrite` d'un octet à 1 MiB + 3 dans un fichier vide : la taille vaut 1 MiB + 4, et tout est à zéro
      sauf le dernier octet ;
    - même chose après `truncate` vers le haut ;
    - le juge doit voir les mêmes zéros.
  - `test_fat_tronquer` : 3 MiB, puis `truncate` à 5000, puis relecture ; le juge voit 5000 octets.
    L'espace libre (`statfs`) revient à sa valeur d'avant à un cluster près.
  - `test_fat_fichier_4gio` (FAT32) :
    - `pwrite(off = 0xFFFFFFFF, len = 1)` donne `FL_EFBIG` en moins d'une seconde, et la taille reste
      inchangée ;
    - `truncate(0x100000000)` donne `FL_EFBIG`.
  - `test_fat_exfat_plus_de_4gio` (label `lent`, sur `exfat-grand`) : `pwrite` de 1 MiB à l'offset
    4 GiB + 1 MiB. La taille et le contenu sont justes, la zone d'avant est nulle, et `fsck` est propre.
    Le juge ne compare que la taille (fichier trop gros).
  - `test_fat_partage_lecteur_ecrivain` : `w` (RDWR) et `r` (RDONLY) sur `/p.bin`. `w` écrit 64 Kio,
    puis `r` relit immédiatement les mêmes 64 Kio, avant toute fermeture. On ferme `r` puis `w`, et le
    juge voit 64 Kio.
  - `test_fat_partage_deux_ecrivains` : deux `RDWR` écrivent des moitiés disjointes, puis on ferme dans
    un ordre puis dans l'autre. Le contenu final est la réunion des deux.
  - `test_fat_drapeau_sale` (FAT32 et exFAT) :
    - après le premier `pwrite`, une lecture brute de l'image (par `pread` sur le fichier image) montre
      le drapeau posé ;
    - après `fl_fs_sync`, il est effacé ;
    - après un nouvel `fl_fs_mkdir`, il est reposé ;
    - après `fl_fs_unmount`, il est effacé, et un remontage en écriture n'est pas en lecture seule.
  - `test_fat_attribut_lecture_seule` : sur une copie où `fatjuge.py set-ro` a marqué `/vide.txt`.
    Attendu : `stat.mode == 0100444`, et `open(RDWR) == FL_EROFS`.
  - `test_fat_disque_plein` : écrire jusqu'à `FL_ENOSPC` dans `/plein.bin` par tranches de 1 MiB.
    - Le fichier garde une taille multiple de ce qui a réussi ;
    - `fsck` est propre ;
    - le fichier se relit et se supprime (`fl_fs_unlink`) ;
    - `free_bytes` revient à sa valeur initiale.
  - `test_fat_lecture_seule_tient` : sur `fat32-sale`, `open(RDWR)`, `mkdir`, `unlink` et `rename`
    donnent `FL_EROFS`, et l'image est **identique octet pour octet** après le démontage (comparaison
    SHA-256 avant et après).
  - Dans `test_error` : `fl_strerror(FL_EFBIG)` donne le texte exact.
- [ ] **Step 2 : lancer `test_fat_ecriture`. Attendu : il ÉCHOUE.**
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : lancer `test_fat_ecriture`, `test_fat` et `test_error`. Attendu : PASS. Puis
  `ctest -LE lent`.**
- [ ] **Step 5 : commit** `feat(fat): écriture des fichiers, zéros des trous, limite 4 Gio, drapeau sale`.

---

### Task 5 : Écriture des dossiers et des noms — mkdir, unlink, rmdir, rename

**Files :**
- Modify : `native/src/fs/fat_glue.c`, `native/tests/test_fat_ecriture.c`

**Interfaces :**
- Produces : `mkdir`, `unlink`, `rmdir` et `rename` sur FAT, avec les codes de `fs.h`.
  - **Noms refusés par la colle**, avant FatFs, avec `FL_EINVAL` : un dernier composant qui finit par
    « . » ou « » (FatFs les retire en silence, ff.c l. 2938), et tout caractère `: * ? " < > | \` ou de
    code < 0x20. FatFs rend aussi `FR_INVALID_NAME` pour ces derniers, mais la colle vérifie elle-même :
    le code ne dépend alors pas de la version de FatFs.
  - **`unlink`.**
    - Sur un dossier : `FL_EISDIR`. `f_unlink` supprimerait un dossier vide, donc la colle vérifie le
      type d'abord.
    - Sur un fichier ouvert : `FL_EBUSY`. Le noeud est trouvé par la clé de T3.
  - **`rmdir`.** Un dossier non vide donne `FL_ENOTEMPTY` (la colle lit une entrée), `/` donne
    `FL_EINVAL`, et un fichier donne `FL_ENOTDIR`.
  - **`rename`.**
    - `from` est un fichier ouvert : `FL_EBUSY`. Déplacer son entrée rendrait faux le `dir_sect` du
      `FIL`. Un dossier qui contient des fichiers ouverts se renomme : leurs entrées ne bougent pas.
    - `to` désigne **le même objet** que `from`, à la casse près : le renommage est fait. FatFs l'accepte
      (ff.c l. 5306).
    - `to` existe et c'est un autre objet : `FL_EEXIST`.
    - `to` est sous `from` : `FL_EINVAL`. La comparaison se fait **par cluster de départ et non par
      chemin**, car la casse diffère. On parcourt chaque ancêtre du parent de `to`, de la racine au
      parent ; si l'un a le même `obj.sclust` (lu par `f_opendir`) que `from`, on refuse. FatFs ne le
      vérifie pas.
    - Un dossier déplacé : FatFs met à jour son « .. ».

- [ ] **Step 1 : ajouter les tests à `test_fat_ecriture.c`** (même fin : `fsck` et juge).
  - `test_fat_dossiers` :
    - `mkdir` de `/d`, puis `/d/e`, puis `/d/e/f.txt` ;
    - `rmdir("/d")` donne `FL_ENOTEMPTY`, `unlink("/d")` donne `FL_EISDIR` ;
    - `rmdir("/d/e/f.txt")` donne `FL_ENOTDIR`, `mkdir("/d")` donne `FL_EEXIST`, `mkdir("/x/y")` donne
      `FL_ENOENT` ;
    - suppression enfants d'abord, puis le juge.
  - `test_fat_noms_interdits` : `a:b`, `a*b`, `a?b`, `a"b`, `a<b`, `a>b`, `a|b`, `a\b`, `fin.` et
    `fin ` donnent `FL_EINVAL` en création de fichier, en `mkdir` et en `rename`. Aucune entrée n'est
    créée (juge).
  - `test_fat_noms_unicode` : créer `été ☀.txt`, `😀.bin` et un nom de 255 octets. Le juge les lit à
    l'identique.
  - `test_fat_noms_casse` :
    - créer `/Photo.jpg`, puis `open("/photo.jpg", CREAT)` ouvre le même fichier (taille partagée) ;
    - `rename("/Photo.jpg", "/PHOTO.jpg")` réussit, et `readdir` montre `PHOTO.jpg` ;
    - `rename` vers un autre fichier existant à la casse près donne `FL_EEXIST`.
  - `test_fat_renommer_deplacer` :
    - un fichier déplacé de `/a/x` vers `/b/x`, puis un dossier `/a` déplacé vers `/b/a2` : le contenu
      suit, et le « .. » de `/b/a2` est juste (`fsck` propre) ;
    - `rename("/b", "/B/a2/c")`, sous lui-même avec une autre casse, donne `FL_EINVAL`.
  - `test_fat_ouvert_occupe` : un fichier ouvert donne `FL_EBUSY` pour `unlink` et `rename`. Un dossier
    contenant un fichier ouvert se renomme, et ce fichier continue d'être lu puis écrit juste, `fsck`
    propre ensuite.
  - `test_fat_nombreux` : 2000 fichiers créés puis 1000 supprimés dans `/n`, puis 500 recréés. Les noms
    courts `~N` restent uniques (`fsck` propre), et le juge est d'accord.
- [ ] **Step 2 : lancer. Attendu : il ÉCHOUE.**
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : lancer `test_fat_ecriture` et `test_fat`. Attendu : PASS.**
- [ ] **Step 5 : commit** `feat(fat): dossiers, renommage (casse, sous lui-même), noms refusés`.

---

### Task 6 : Débit — regroupement des écritures contiguës

FatFs envoie au plus un cluster par `disk_write` (ff.c, `f_write`). Sur USB, chaque commande coûte une
aller-retour, comme le montrait le correctif 0.2.1 pour ext4.

**Files :**
- Modify : `native/src/fs/fat_diskio.c`, `native/tests/support/fault_bdev.{c,h}`,
  `native/tests/test_fat_ecriture.c`

**Interfaces :**
- Consumes : `fl_wbuf` (`native/src/fs/wbuf.h` : `fl_wbuf_write`, `fl_wbuf_flush`, `fl_wbuf_end`,
  `fl_wbuf_free`, `FL_WBUF_CAP`).
- Produces :
  - Dans `fat_diskio.c`, un `fl_wbuf` par disque. Un `disk_write` contigu au tampon s'y ajoute.
  - Le tampon est **vidé avant** tout `disk_read`, tout `disk_write` non contigu, `CTRL_SYNC` et
    `fat_disk_detach`. L'ordre des écritures sur le disque reste exactement celui de FatFs.
  - Une erreur de vidage est collante (`fat_disk_error`) et rendue par l'appel en cours.
  - `fault_bdev.h` :
    - `uint64_t fl_fault_bdev_reads(const fl_bdev *d)` (nombre d'appels de lecture, ajouté en T3) ;
    - `void fl_fault_bdev_count_from(fl_bdev *d, uint64_t lba)` ;
    - `uint64_t fl_fault_bdev_counted_writes(const fl_bdev *d)` : appels d'écriture à `lba ≥` le seuil.

- [ ] **Step 1 : écrire `test_fat_debit` (FAT32 et exFAT vides, clusters de 4 Kio).** Le seuil de
  comptage est le début de la zone de données (lu dans le BPB). On copie 64 MiB en `pwrite` de 128 Kio
  (le `CHUNK` du provider). Attendu : **≤ 128 appels d'écriture dans la zone de données**, soit une
  moyenne ≥ 512 Kio, contre environ 16 384 sans tampon. Le juge et `fsck` sont propres. On fait aussi
  ce test avec une panne (`fl_fault_bdev_new(…, writes_ok = 3, FL_EIO)`) : `close` rend `FL_EIO`, le
  volume passe en lecture seule `FL_RO_IOERR`, et `fl_fs_sync` rend `FL_EIO`.
- [ ] **Step 2 : lancer. Attendu : il ÉCHOUE (environ 16 384 écritures).**
- [ ] **Step 3 : implémenter le tampon dans `fat_diskio.c` et les compteurs.**
- [ ] **Step 4 : lancer `test_fat_ecriture` (toutes les tâches précédentes comprises). Attendu : PASS.**
- [ ] **Step 5 : commit** `perf(fat): regroupement des écritures contiguës (fl_wbuf) dans l'accès disque`.

---

### Task 7 : Coupures et pannes

**Files :**
- Create : `native/tests/test_fat_coupure.c` (label `ecriture`)
- Modify : `native/CMakeLists.txt`

**Interfaces :**
- Consumes : `fl_fault_bdev_new`, `fu_fsck_fat_repair`, `fu_fsck_fat_n`, `tm_check_fat_judge`.
- Produces : aucun code de moteur, sauf correction d'un défaut que ces tests montreraient.

- [ ] **Step 1 : écrire les tests.** Chaque test est fait sur FAT32 et sur exFAT.
  - `test_fat_coupure_points` : un scénario fixe.
    - Créer 5 fichiers de 300 Kio et les fermer : ils sont « sûrs ».
    - Puis, dans le même montage, écrire `/encours.bin` (2 MiB), faire `mkdir("/d")`, `rename` d'un
      fichier sûr, et tronquer un autre fichier sûr.
    - On le rejoue pour chaque `writes_ok` de 0 à N : N est le nombre d'écritures du scénario complet,
      mesuré par une première passe, par pas de `max(1, N/40)`.

    Après la panne :
    - on démonte (un code d'erreur est accepté) ;
    - un remontage en écriture est en lecture seule avec `FL_RO_DIRTY`. Exception : une coupure avant la
      première écriture laisse l'image inchangée, octet pour octet ;
    - `fu_fsck_fat_repair == 0` ;
    - chaque fichier sûr non touché par le scénario a son contenu d'origine. On le vérifie par
      `fatjuge.py manifest` : les lignes de ces chemins sont identiques à celles du modèle.
  - `test_fat_panne_lecture` : `fl_fault_bdev_fail_reads(FL_EIO)` en cours de lecture donne `pread ==
    FL_EIO`, sans plantage. Après rétablissement, une nouvelle ouverture du fichier relit juste.
  - `test_fat_debranche` : `FL_ENODEV` pendant une écriture. Les opérations rendent une erreur, et le
    démontage libère tout : ASan ne voit aucune fuite, et l'emplacement FatFs est réutilisable.
- [ ] **Step 2 : lancer.** Si un test échoue, c'est un défaut du moteur : on le corrige dans la colle, et
  au besoin par un correctif FatFs (voir Global Constraints).
- [ ] **Step 3 : relancer jusqu'à PASS.**
- [ ] **Step 4 : commit** `test(fat): coupures à chaque écriture, pannes de lecture et débranchement`.

---

### Task 8 : Batterie aléatoire, disque hostile, vérification par le noyau

**Files :**
- Create : `native/tests/test_fat_batterie.c` (label `ecriture`, avec une variante `lent`),
  `native/tests/test_fat_hostile.c`, `tools/fixtures/verify_fat_with_kernel.sh`
- Modify : `native/CMakeLists.txt`

**Interfaces :**
- Consumes : tout ce qui précède.
- Produces : `verify_fat_with_kernel.sh <image> <manifeste>`. Ce script est lancé **par l'utilisateur**,
  avec sudo. Il :
  - monte l'image en loop (`vfat` ou `exfat`, `ro`, `tz=UTC`) ;
  - calcule le manifeste du noyau (même format que `fatjuge.py`, condensat par tranches de 1 MiB) ;
  - le compare au manifeste donné, puis démonte.

- [ ] **Step 1 : écrire `test_fat_batterie`.** Il fait 3 × 400 opérations pseudo-aléatoires (graine
  affichée, fixée par `FL_SEED`) sur FAT16, FAT32 et exFAT. Les opérations sont pondérées :
  - créer un fichier ou un dossier ;
  - `pwrite` à un offset aléatoire, trou compris ;
  - `truncate` vers le haut ou vers le bas ;
  - `rename` : dans le même dossier, vers un autre dossier, ou changement de casse seul ;
  - `unlink`, `rmdir` ;
  - ouvrir deux fois le même fichier.

  Toutes les 50 opérations : `fl_fs_sync`, puis `tm_check_fl == 0`. À la fin : démontage,
  `fu_fsck_fat_n == 0`, `tm_check_fat_judge == 0`, et écriture de
  `build/native/tmp/fat-batterie-<type>.manifest` avec l'image gardée pour le script noyau (variable
  `FL_KEEP=1`). La variante `test_fat_batterie_lent` fait 20 000 opérations.
- [ ] **Step 2 : écrire `test_fat_hostile`** (sans label, Android compris). Il part de 300 images
  dérivées de `fat16-arbre`, `fat32-arbre` et `exfat-arbre`. Chacune reçoit 1 à 8 octets aléatoires
  (graine fixe) dans le secteur de démarrage, la FAT ou le bitmap, ou le premier cluster de la racine.
  Il ajoute des cas fabriqués :
  - la FAT d'un dossier en boucle ;
  - un cluster > `n_fatent` ;
  - `BPB_SecPerClus = 0`, puis 3 ;
  - `BPB_NumFATs = 0` ;
  - une somme d'ensemble d'entrées exFAT fausse ;
  - un `DataLength` exFAT plus grand que le volume.

  Pour chaque image : montage en **lecture seule** ; s'il réussit, parcours complet borné (profondeur
  64, 100 000 entrées) avec lecture entière de chaque fichier par tranches de 1 MiB. Attendu : aucun
  plantage ni rapport ASan/UBSan, et chaque appel rend `FL_OK` ou un `fl_err`. La durée totale reste
  sous 120 s ; une boucle infinie est un échec (délai CTest de 300 s). Les images sont fabriquées en
  mémoire par le test, sur une copie dans le dossier temporaire, et non dans les fixtures.
- [ ] **Step 3 : lancer. Corriger tout défaut** (colle d'abord, correctif FatFs sinon). Attendu : PASS
  des deux, puis `ctest -L lent -R fat` : PASS.
- [ ] **Step 4 : préparer pour l'utilisateur la commande du script noyau** (voir T10). L'agent ne la
  lance pas.
- [ ] **Step 5 : commit** `test(fat): batterie aléatoire, images hostiles, vérification par le noyau`.

---

### Task 9 : JNI, Kotlin, Flutter

**Files :**
- Modify :
  - `native/jni/handles.c` : `vol_open` accepte FAT12, FAT16, FAT32 et exFAT, en clair et dans LUKS ;
  - `native/tools/fluluks_cli.c` : montage selon le type sondé ;
  - `android/.../engine/EngineError.kt` : `FL_EFBIG = -18` ;
  - `android/.../provider/FluLuksDocumentsProvider.kt` :
    - `guard` : `FL_EFBIG` → message de la contrainte ; `FL_EINVAL` pour `createDocument`,
      `renameDocument`, `moveDocument` et `copyDocument` → « Nom non autorisé sur ce disque » ;
    - `FL_ENOTSUP` → « Opération non prise en charge par ce disque » : l'ancien texte ext3 est gardé
      seulement si le volume est ext4 ;
    - `errnoFor` : `FL_EFBIG` → `OsConstants.EFBIG` ;
  - `android/.../provider/WriteOps.kt` (`rename`) ;
  - `android/.../provider/DocNames.kt` (commentaire : 255 octets UTF-8, la limite d'ext4 ; sur FAT,
    255 unités UTF-16 sont toujours atteintes après) ;
  - `android/.../volumes/VolumeManager.kt` (commentaire `MAX_VOLUMES` : 8 montages ext4 et 8 FAT, la
    limite commune reste 8) ;
  - `native/tests/test_handles.c`, tests JVM `WriteOpsTest.kt` et `VolumeManagerTest.kt`.

**Interfaces :**
- Produces :
  - `fj_scan` rend `supported: true` pour les partitions FAT et exFAT, en clair ou chiffrées.
  - `fj_mount` les monte, avec `want_rw` respecté.
  - **`WriteOps.rename`** ne vérifie plus lui-même l'existence de la cible. Il appelle `engine.rename`,
    et `FL_EEXIST` devient « Un élément porte déjà ce nom ». Un renommage qui ne change que la casse
    passe donc sur FAT, et reste refusé sur ext4 si la cible existe.

- [ ] **Step 1 : écrire les tests.**
  - `test_handles` : `test_handles_fat32` et `test_handles_luks2_fat32`. On ouvre le disque image, puis :
    - le JSON de `fj_scan` contient `"content":"FAT32"` et `"supported":true` ;
    - `fj_mount` en écriture, puis `mkdir`, puis démontage ;
    - `fu_fsck_fat_n == 0`.
  - JVM `WriteOpsTest` :
    - `renameCaseOnly` : le faux moteur accepte « a.txt » → « A.txt », et `rename` rend le nouveau
      chemin ;
    - `renameExistingMapsEexist` : le faux moteur rend `FL_EEXIST`, d'où le message « Un élément porte
      déjà ce nom ».
  - JVM `errnoForEfbig` : `errnoFor(FL_EFBIG) == OsConstants.EFBIG`.
  - JVM `invalidNameMessage` : `FL_EINVAL` sur `createDocument` donne « Nom non autorisé sur ce disque ».
- [ ] **Step 2 : lancer `ctest -R test_handles` et `./gradlew :app:testDebugUnitTest`. Attendu : ils
  ÉCHOUENT.**
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : relancer les deux, puis `flutter test` et `flutter build apk --release`. Attendu : PASS
  et compilation réussie.**
- [ ] **Step 5 : commit** `feat(android): ouverture des disques FAT/exFAT, erreur EFBIG, renommage de casse`.

---

### Task 10 : Essais sur le Pixel *(utilisateur)*

L'agent prépare les commandes. **L'utilisateur** branche et écrit, sur une clé FAT32 puis une clé exFAT
(ou une seule clé reformatée), dont le contenu est sauvegardé. Le vrai disque ext4 n'est pas concerné.

- [ ] Installer : `flutter build apk --release && adb -s 192.168.0.104:5555 install -r build/app/outputs/flutter-apk/app-release.apk`.
- [ ] Sur la clé FAT32 :
  - ouvrir ;
  - lire une vidéo dans VLC, avance rapide comprise ;
  - copier un dossier de photos depuis le téléphone ;
  - renommer, supprimer ;
  - copier un fichier de plus de 4 Gio : le message « Fichier trop gros… » doit apparaître ;
  - éjecter ;
  - sur PC : `sudo fsck.fat -n /dev/sdX1` est propre.
- [ ] Même parcours sur exFAT, avec un fichier de plus de 4 Gio qui doit réussir. Sur PC : `sudo
  fsck.exfat -n` est propre.
- [ ] Arracher la clé pendant une copie, puis la rebrancher. Attendu : ouverture en lecture seule avec
  « disque mal éjecté… ». Puis sur PC, `fsck` réparateur, et la clé redevient inscriptible.
- [ ] Facultatif : `sudo tools/fixtures/verify_fat_with_kernel.sh` sur les images gardées en T8.

---

### Task 11 : Documentation et version `v0.3.0`

**Files :**
- Modify : `README.md`, `native/third_party/PATCHES.md`, la feuille de route, `pubspec.yaml`
  (`version: 0.3.0+4`)

- [ ] **README.**
  - FAT12, FAT16, FAT32 et exFAT sont lus et écrits.
  - Limites :
    - 4 Gio par fichier en FAT ;
    - noms insensibles à la casse ;
    - pas de propriétaire ;
    - une clé mal éjectée s'ouvre en lecture seule jusqu'à sa vérification sur un ordinateur ;
    - FluLuks ne répare pas un FAT abîmé.
  - Licences : FatFs est sous licence de type BSD à 1 clause, compatible avec le reste.
- [ ] **`PATCHES.md`.** Une section FatFs : la version, l'URL, le SHA-256 de l'archive, la liste des
  fichiers copiés, l'absence de correctif (ou la liste des correctifs), et `ffconf.h` avec ses écarts à
  l'amont.
- [ ] **Feuille de route.** Cocher la phase 4, avec les résultats de mesure de T6 et les essais de T10.
- [ ] **Version, selon git flow.**
  - Fusionner `feature/fat-exfat` → `develop` (`--no-ff`).
  - Créer `release/0.3.0`, avec le changement de version.
  - Fusionner vers `main`, poser l'étiquette `v0.3.0`, puis fusionner vers `develop`.
  - Pas de push sans accord de l'utilisateur.
- [ ] **Commits :** `docs: FAT/exFAT dans le README et PATCHES.md`, puis `chore(version): 0.3.0 — FAT et exFAT`.
