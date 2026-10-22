# Portail — Phase 8 : formatage des disques USB — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** formater un disque USB branché sur le téléphone, en **FAT32, exFAT, ext4 ou NTFS**, en clair
ou **chiffré LUKS2**, avec une étiquette. Le disque obtenu est reconnu sans réserve par Ubuntu, Windows et
les outils de référence. Fin de phase : version **v1.1.0**.

**Architecture :**
- **Moteur natif**, nouveau module `native/src/format/` et en-tête `include/fluluks/format.h` :
  - `part_write.c` : table de partitions à **une partition** couvrant le disque (alignée sur 1 Mio) :
    MBR jusqu'à 2 Tio, GPT au-delà ;
  - `luks2_format.c` : en-tête LUKS2 (deux copies), emplacement de clé 0 (Argon2id, AF, aes-xts-plain64),
    condensé PBKDF2-SHA256, puis ouverture par `fl_luks_unlock` ;
  - systèmes de fichiers : `f_mkfs` de FatFs (`FF_USE_MKFS 1`) pour FAT32 et exFAT, `ext4_mkfs` de
    lwext4, `mkntfs` de ntfs-3g (importé de l'archive amont, sans `main`) ;
  - `fl_format(dev, opts, progress)` enchaîne : effacement des signatures, table, (LUKS2), système de
    fichiers, synchronisation.
- **JNI :** `fj_format(disk, json_opts, pass, passlen)`, sur la poignée d'un disque ouvert sans volume.
- **Kotlin :** `Engine.format`, `VolumeManager.format(diskId, FormatOptions, pass)` sur l'exécuteur du
  moteur ; refus si un volume du disque est ouvert ou si le disque est protégé en écriture ; disque relu
  (scan) ensuite.
- **Flutter :** action « Formater… » sur la carte d'un disque, écran `FormatScreen` (système de fichiers,
  chiffrement, phrase de passe, étiquette), confirmation en tapant le nom du disque, progression.

**Tech Stack :** C11, FatFs R0.16, lwext4, libntfs-3g et mkntfs 2022.10.3, mbedTLS, Argon2 ; Kotlin ;
Flutter 3.41.4, Pigeon 27.3.0. Vérifications : `sfdisk`, `sgdisk`, `fsck.fat`, `fsck.exfat`, `e2fsck`,
`dumpe2fs`, `ntfsfix`, `cryptsetup` 2.7 (sans root), puis montage par le noyau (script `sudo`, lancé par
l'utilisateur).

**Spec :** [docs/superpowers/specs/2026-10-17-fluluks-design.md](../specs/2026-10-17-fluluks-design.md)
(§ 4.2, § 7, § 13, et **§ 14 « formatage »**).

## Global Constraints

- **Branche et commits.**
  - Branche `feature/formatage`, créée depuis `feature/robustesse` *(décision du planificateur : la phase 7
    attend les essais de l'utilisateur ; elle sera fusionnée dans `develop` avant cette branche)*.
  - Commits sous l'identité `mawena <gamligocharles@gmail.com>`, en français (Conventional Commits),
    **sans aucune attribution à Claude**, en nommant les fichiers (`git commit -- <fichiers>`).
  - Un commit par tâche ; la tâche est cochée dans la feuille de route (section Phase 8) dans le même commit.
- **Sûreté.**
  - Formater seulement un disque dont Portail a pris l'interface USB, **sans aucun volume ouvert**, et non
    protégé en écriture. Jamais le stockage du téléphone, une carte SD ou une image de test en lecture seule.
  - Les tests natifs ne formatent que des **fichiers image** dans le dossier de construction ; sur le
    téléphone, seulement sous `/data/local/tmp/fluluks/` et dans le cache de l'application.
  - Essai réel : **clé USB sacrifiée seulement**, jamais le vrai disque de l'utilisateur.
  - La phrase de passe suit les règles existantes : octets effacés après usage, jamais journalisée.
  - Aléa : clé maîtresse, sels et UUID lus dans `/dev/urandom` (échec de lecture → `FL_EIO`, jamais d'aléa
    faible).
- **Compatibilité exigée** (spec § 14) : chaque format produit doit passer l'outil de référence **sans
  aucune erreur ni avertissement** : `fsck.fat -n`, `fsck.exfat -n`, `e2fsck -fn`, `ntfsfix -n`,
  `cryptsetup luksDump` et `cryptsetup open --test-passphrase`, `sfdisk -d` / `sgdisk -v`.
- **Valeurs fixées** *(décisions du planificateur)* :
  - table : MBR (type `0x0C` FAT32, `0x07` exFAT et NTFS, `0x83` ext4 et LUKS) jusqu'à 2 Tio, GPT au-delà
    (types « Microsoft basic data » et « Linux filesystem ») ; partition de 1 Mio jusqu'à la fin du disque
    arrondie au Mio ;
  - LUKS2 comme `cryptsetup luksFormat` d'Ubuntu : aes-xts-plain64, clé de 512 bits, Argon2id t=4,
    m=1 Gio (256 Mio si la mémoire disponible ne le permet pas, même règle que le déverrouillage), p=4,
    en-têtes de 16 Kio, zone des emplacements jusqu'à 16 Mio, données à 16 Mio, secteurs de 512 octets ;
  - ext4 : blocs de 4 Kio, journal, propriétaire racine `1000:1000` *(premier utilisateur d'Ubuntu : la
    racine lui appartient)* ;
  - étiquettes : FAT32 11 caractères ASCII en majuscules, exFAT 11, ext4 16 octets, NTFS 32 caractères ;
    au-delà, refus avec un message qui donne la limite ;
  - choix par défaut dans l'écran : exFAT, non chiffré.
- **Langue :** interface et messages en français.

## Review Focus

1. **Disque arraché pendant le formatage.** Attendu : aucun plantage ; message « Disque débranché pendant
   le formatage : recommencez le formatage » ; au rebranchement, le disque est vu (au pire « non reconnu »)
   et peut être reformaté. *Tests : T6 `formatInterruptedReportsAndRescans`.*
2. **Disque plus petit que le minimum** (FAT32 < 33 Mio, ext4 avec journal < 8 Mio, LUKS2 < 17 Mio + le
   système de fichiers). Attendu : refus avant toute écriture, avec la taille minimale. *Tests : T5
   `tooSmallRefusedBeforeWriting`.*
3. **Secteurs de 4 096 octets** (certains disques USB). Attendu : table, LUKS2 et systèmes de fichiers
   alignés et lisibles par les outils. *Tests : T1, T2, T3, T5 sur une image à secteurs de 4 096 octets.*
4. **Anciennes signatures** (ancien LUKS, ancien GPT de secours en fin de disque, ancien superbloc ZFS ou
   btrfs). Attendu : effacées ; `blkid` et notre détection ne voient que le nouveau format. *Tests : T5
   `oldSignaturesWiped`.*
5. **Phrase de passe vide, ou confirmation différente.** Attendu : refus dans l'écran, avant tout appel.
   *Tests : T8 `emptyOrMismatchedPassphraseRefused`.*

---

### Task 1 : table de partitions (MBR ou GPT) à une partition

**Files :** Create `native/src/format/part_write.c`, `native/include/fluluks/format.h`,
`native/tests/test_format_part.c` ; Modify `native/CMakeLists.txt`.

**Interfaces :**
- Produces :
  - `typedef enum { FL_FMT_FAT32 = 1, FL_FMT_EXFAT, FL_FMT_EXT4, FL_FMT_NTFS } fl_fmt_fs;`
  - `int fl_part_write_single(fl_bdev *d, fl_fmt_fs fs, bool encrypted, uint64_t *first_lba, uint64_t *count);`
    : écrit MBR ou GPT (protecteur + principal + secours, CRC32), rend la place de la partition.

- [ ] **Step 1 : tests qui échouent** (images de 64 Mio et de 3 Tio creuses, secteurs de 512 et de 4 096
  octets) : `mbrForSmallDisk` (`sfdisk -d` : une partition de type attendu, début 1 Mio) ; `gptForBigDisk`
  (`sgdisk -v` : « No problems found ») ; `scanReadsBack` (`fl_part_scan` retrouve la partition) ;
  `alignedTo1MiB`.
- [ ] **Step 2 : lancer** `ctest -R test_format_part`. Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** : ctest rapide.
- [ ] **Step 5 : commit** `feat(formatage): table de partitions à une partition`.

### Task 2 : FAT32 et exFAT

**Files :** Create `native/src/format/mkfs_fat.c`, `native/tests/test_format_fat.c` ; Modify
`native/cmake/fatfs/ffconf.h` (`FF_USE_MKFS 1`), `native/src/fs/fat_diskio.c` (lecteur logique sur un
`fl_bdev`).

**Interfaces :**
- Produces : `int fl_mkfs_fat(fl_bdev *part, bool exfat, const char *label);`

- [ ] **Step 1 : tests qui échouent** : `fat32Clean` / `exfatClean` (`fsck.fat -n` / `fsck.exfat -n` sans
  erreur, étiquette lue par `fatlabel` / `exfatlabel`) ; `mountedAndWritten` (notre moteur monte en
  écriture, écrit, éjecte, puis `fsck` toujours propre) ; `labelTooLongRefused`.
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** : ctest rapide (les tests FAT existants restent verts avec `FF_USE_MKFS`).
- [ ] **Step 5 : commit** `feat(formatage): FAT32 et exFAT`.

### Task 3 : ext4

**Files :** Create `native/src/format/mkfs_ext4.c`, `native/tests/test_format_ext4.c` ; correctifs
éventuels dans `native/third_party/patches/lwext4/` (documentés dans `PATCHES.md`).

**Interfaces :**
- Produces : `int fl_mkfs_ext4(fl_bdev *part, const char *label, uint32_t root_uid, uint32_t root_gid);`

- [ ] **Step 1 : tests qui échouent** : `ext4Clean` (`e2fsck -fn` sans aucune ligne d'erreur ;
  `dumpe2fs -h` : `has_journal`, `extent`, étiquette, état propre) ; `mountedWritableByEngine` (notre
  moteur l'ouvre **en écriture** : aucune option refusée par `ext4_policy`) ; `rootOwnedByUser` ;
  `writtenThenClean` (écritures, éjection, `e2fsck -fn` propre).
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.** Si `ext4_mkfs` produit un système que `e2fsck` signale : corriger lwext4
  par un correctif documenté (comme en phase 3), jamais masquer l'avertissement.
- [ ] **Step 4 : vérifier** : ctest rapide.
- [ ] **Step 5 : commit** `feat(formatage): ext4 avec journal`.

### Task 4 : NTFS

**Files :** Create `native/third_party/ntfs-3g/ntfsprogs/` (`mkntfs.c`, `attrdef.c`, `boot.c`, `sd.c`,
`utils.c` et leurs en-têtes, de l'archive amont 2022.10.3, SHA-256 vérifié, documentés dans `PATCHES.md`),
`native/src/format/mkfs_ntfs.c`, `native/tests/test_format_ntfs.c`.

**Interfaces :**
- Produces : `int fl_mkfs_ntfs(fl_bdev *part, const char *label);` — `mkntfs` appelé comme bibliothèque
  (son `main` renommé par un correctif), sur le périphérique `fl_bdev` déjà utilisé par `ntfs_dev.c`,
  formatage rapide (`-Q`).

- [ ] **Step 1 : tests qui échouent** : `ntfsClean` (`ntfsfix -n` propre, `ntfsjuge.py check` propre,
  étiquette) ; `mountedAndWritten` (notre moteur l'ouvre en écriture, écritures, éjection, juges propres).
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** : ctest rapide.
- [ ] **Step 5 : commit** `feat(formatage): NTFS`.

### Task 5 : LUKS2 et orchestration `fl_format`

**Files :** Create `native/src/format/luks2_format.c`, `native/src/format/format.c`,
`native/tests/test_format_luks.c`, `native/tests/test_format.c`.

**Interfaces :**
- Produces :
  - `int fl_luks2_format(fl_bdev *part, const void *pass, size_t passlen, uint32_t kdf_mem_kib, fl_bdev **decrypted);`
  - `typedef struct { fl_fmt_fs fs; bool encrypted; const char *label; uint32_t kdf_mem_kib; uint32_t root_uid, root_gid; } fl_fmt_opts;`
  - `int fl_format(fl_bdev *disk, const fl_fmt_opts *o, const void *pass, size_t passlen, void (*progress)(int pct, void *arg), void *arg);`
    : vérifie la taille minimale, efface les 1 premiers et 1 derniers Mio, écrit la table, chiffre si
    demandé, formate, synchronise ; la phrase de passe et la clé maîtresse sont effacées sur tous les
    chemins.

- [ ] **Step 1 : tests qui échouent** :
  - `luksDumpAccepts` (`cryptsetup luksDump` : version 2, aes-xts-plain64, argon2id, 512 bits) ;
  - `cryptsetupOpensWithPassphrase` (`cryptsetup open --test-passphrase --key-file` : code 0) ;
  - `ourUnlockReadsBack` ; `wrongPassphraseRefused` ;
  - `formatEachFsEncryptedAndPlain` (les 8 combinaisons, chacune jugée par son outil, le contenu chiffré
    jugé après extraction par `fu_extract_luks_ext4`) ;
  - `oldSignaturesWiped` (image avec un ancien LUKS1 et un ancien GPT de secours) ;
  - `tooSmallRefusedBeforeWriting` (image de 16 Mio, ext4 chiffré : refus, image inchangée) ;
  - `passIsZeroed` (comme `test_luks_phrase`) ;
  - script `tools/fixtures/verify_format_with_kernel.sh` (sudo, lancé par l'utilisateur) : chaque image
    montée par le noyau, LUKS ouvert par dm-crypt.
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** : ctest rapide, puis la suite longue.
- [ ] **Step 5 : commit** `feat(formatage): LUKS2 et formatage complet d'un disque`.

### Task 6 : JNI et Kotlin

**Files :** Modify `native/jni/` (`fj_format`), `engine/Engine.kt`, `engine/NativeEngine.kt`,
`volumes/VolumeManager.kt`, `bridge/HostApiImpl.kt`, `pigeons/fluluks_api.dart` ; tests
`native/tests/test_handles.c`, `VolumeManagerTest.kt`, `androidTest/.../FormatInstrumentedTest.kt`.

**Interfaces :**
- Produces :
  - `Engine.format(disk: Long, opts: ByteArray /* JSON */, pass: ByteArray?): Int` ;
  - `data class FormatOptions(val fs: String, val encrypted: Boolean, val label: String)` ;
  - `suspend fun VolumeManager.format(diskId: String, o: FormatOptions, pass: ByteArray?)` : refus
    `MSG_FORMAT_OPEN` = « Éjectez d'abord les volumes de ce disque », `MSG_FORMAT_RO` = « Disque protégé en
    écriture », disque relu ensuite ; `formatting: StateFlow<Int?>` (pourcentage) ;
  - Pigeon : `@async void formatDisk(String diskId, String fs, bool encrypted, String label, Uint8List? passphrase)`,
    `FluLuksEventsApi.onFormatProgress(int? pct)`.

- [ ] **Step 1 : tests qui échouent** : `test_handles` `formatThroughJni` ; `VolumeManagerTest`
  `formatRefusedWithOpenVolume`, `formatRescans`, `formatInterruptedReportsAndRescans`, `passZeroedAfterFormat` ;
  `FormatInstrumentedTest` (Pixel, image dans le cache) : formatage exFAT puis ext4 chiffré, montage et
  écriture.
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** : JVM, ctest rapide, instrumenté (`run_phase7_tests.sh` avec la classe).
- [ ] **Step 5 : commit** `feat(formatage): formatage depuis Kotlin`.

### Task 7 : écran de formatage

**Files :** Create `lib/src/screens/format_screen.dart`, `test/format_screen_test.dart` ; Modify
`lib/src/widgets/disk_card.dart`, `lib/src/state/app_state.dart`, `lib/src/state/host_api.dart`,
`lib/src/strings_fr.dart`, `test/fake_host_api.dart`.

**Interfaces :**
- Produces : menu « Formater… » sur la carte d'un disque (désactivé avec la raison si un volume est
  ouvert) ; `FormatScreen` : système de fichiers (exFAT par défaut, FAT32, ext4, NTFS, avec une ligne
  d'aide chacun : « Windows, macOS, Linux, TV », « ancien matériel, 4 Gio par fichier au plus »,
  « Ubuntu », « Windows »), interrupteur « Chiffrer (LUKS2) » avec phrase de passe et confirmation,
  étiquette ; bouton « Formater » → dialogue « Tout le contenu de « <nom> » (<taille>) sera effacé. Tapez
  le nom du disque pour confirmer. » ; progression ; à la fin, retour à l'accueil, disque relu.

- [ ] **Step 1 : tests qui échouent** : `defaultsToExfat` ; `emptyOrMismatchedPassphraseRefused` ;
  `confirmRequiresExactDiskName` ; `formatCallsApiWithOptions` ; `menuDisabledWithOpenVolume` ;
  `labelLimitShown` ; pas de débordement à 412 dp.
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** : `flutter analyze`, `flutter test`.
- [ ] **Step 5 : commit** `feat(formatage): écran de formatage`.

### Task 8 *(utilisateur)* : essais

- [ ] Sur le PC : `sudo tools/fixtures/verify_format_with_kernel.sh build/native/format` (images montées
  par le noyau).
- [ ] Sur le Pixel, **clé USB sacrifiée** : formater en exFAT, puis FAT32, puis ext4 chiffré, puis NTFS ;
  après chacun, y copier un fichier, éjecter, et brancher la clé sur Ubuntu (et Windows si possible).

### Task 9 : documentation et v1.1.0

- [ ] README (formatage, limites), feuille de route cochée ; commit `docs: formatage des disques`.
- [ ] Relecture finale de toute la branche, puis fusion `feature/formatage` → `develop` (après celle de
  la phase 7), `release/1.1.0` (pubspec `1.1.0+8`), `main`, étiquette signée `v1.1.0`. Pas de push sans
  l'accord de l'utilisateur.
