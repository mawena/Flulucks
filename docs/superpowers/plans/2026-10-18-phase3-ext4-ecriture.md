# FluLuks — Phase 3 : ext4 en écriture — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** on peut créer, modifier, renommer, déplacer, copier et supprimer des fichiers et dossiers sur
un volume ext4 (en clair ou dans LUKS1/LUKS2) depuis n'importe quelle application Android, via
l'application « Fichiers ». Le disque doit sortir **propre selon `e2fsck -fn`** après chaque session, et
**récupérable par Ubuntu** après une coupure.

**Architecture :**
- **lwext4 corrigé.** Le moteur natif écrit grâce à lwext4, corrigé par une série de correctifs
  (`native/third_party/patches/lwext4/0002…0006`). Ces correctifs suivent les constats de l'essai du
  2026-10-18 : journal, horodatages, trous de fichier, montage, crc32c.
- **Règles du § 4.2.** La colle ext4 applique les règles de sécurité du § 4.2 : lecture seule au moindre
  doute, propriétaire hérité du parent, journal obligatoire.
- **Écriture USB.** Le périphérique USB apprend `WRITE` et `SYNCHRONIZE CACHE`, et détecte la protection
  en écriture.
- **Couches au-dessus.** Le JNI, le `VolumeManager` et le DocumentsProvider exposent l'écriture. Flutter
  montre l'état de lecture seule et sa raison.
- **Banc de non-corruption.** Tout est vérifié sur PC par un banc de non-corruption : images aux options
  du vrai disque, `e2fsck -fn`, lecture indépendante par `debugfs`, et coupures simulées.

**Tech Stack :** C11 (moteur, lwext4 58bcf89 + correctifs), CMake/CTest avec ASan/UBSan, e2fsprogs 1.47
(`mkfs.ext4`, `e2fsck`, `debugfs`) comme juge sur PC, cryptsetup sur fichiers (sans root), Kotlin
(DocumentsProvider, `ProxyFileDescriptorCallback`), Flutter 3.41.4 avec Pigeon 27.3.0.

**Spec :** [docs/superpowers/specs/2026-10-17-fluluks-design.md](../specs/2026-10-17-fluluks-design.md)
(§ 3, § 4.1.6, **§ 4.2**, § 4.3, § 7, § 10).
**Essai préalable :** [docs/superpowers/research/2026-10-18-essai-lwext4-ecriture/RAPPORT.md](../research/2026-10-18-essai-lwext4-ecriture/RAPPORT.md).
Ce rapport donne les fichiers et les lignes de chaque défaut de lwext4 cité ci-dessous, ainsi que le
banc d'essai `harness.c` et le correctif de 2 lignes déjà vérifié (`csum_fix.diff`).

## Global Constraints

- Branche `feature/ext4-ecriture` depuis `develop`. Les commits portent l'identité git de
  l'utilisateur, avec un message en français et **sans aucune attribution à Claude**. Un commit par
  tâche, plus les commits de correction. On coche la tâche dans la feuille de route
  (`docs/superpowers/plans/2026-10-17-fluluks-feuille-de-route.md`, section Phase 3) **dans le même
  commit**.
- Anti-objectif n° 1 de la spec : **ne jamais corrompre le disque**. En cas de doute, le volume passe
  **en lecture seule**, avec une raison affichée. Aucune tâche ne relâche une règle du § 4.2.
- lwext4 n'est **jamais modifié dans le sous-module**. Chaque correctif est un fichier
  `native/third_party/patches/lwext4/000N-<sujet>.patch`, appliqué au configure dans l'ordre des
  numéros, comme le 0001. L'ordre est fixe : 0002 (T1), 0003 (T4), 0004 (T5), 0005 (T6), 0006 (T7).
  Chaque correctif porte un en-tête qui explique le défaut et renvoie au rapport d'essai.
- **Juge :** `e2fsck -fn` (e2fsprogs ≥ 1.47) doit sortir avec le code **0, sans aucune ligne
  d'erreur**, sur toute image écrite par le moteur. Les tests qui l'appellent portent le label CTest
  `ecriture`, sont réservés au PC et sont sautés (sans échec) si `e2fsck` est absent ou sur Android.
- Images de test **générées sans root** (`mkfs.ext4`, `debugfs -w`, `cryptsetup luksFormat` sur
  fichier), dans `build/native/fixtures/`, jamais commitées. Les images d'écriture sont **copiées**
  avant chaque test, qui n'écrit que sur sa copie. Les copies vont dans un dossier temporaire sous
  `build/` et sont supprimées à la fin du test, même en échec.
- Toute vérification par le noyau (montage loop, dm-crypt) demande sudo : elle est **faite par
  l'utilisateur**, avec un script fourni, jamais par un agent.
- Ce qui reste valable des phases 1 et 2 :
  - l'entrée disque et l'entrée des autres applications sont **hostiles** ;
  - la phrase de passe circule en octets UTF-8 mis à zéro, et la clé maîtresse ne quitte pas le natif ;
  - les noms traversent JNI en octets UTF-8 ;
  - interface en français, avec les chaînes exactes de ce plan ;
  - pas de `printf` dans la bibliothèque ;
  - `-Wall -Wextra -Werror` sur nos sources ;
  - ASan/UBSan sans aucun rapport.
- Appareil : Pixel 4 XL, `192.168.0.104:5555` en Wi‑Fi. Les écritures n'ont lieu **que** sous
  `/data/local/tmp/fluluks/` et dans l'application `com.mawena.fluluks`. **Le vrai disque n'est jamais
  écrit par un agent.** L'utilisateur l'écrit lui-même en T13, et seulement après une sauvegarde
  (§ 7 de la spec).
- Raisons de lecture seule, au plus 127 octets, texte exact *(décision du planificateur)* :
  - `RO_FEATURES` = « options ext4 non prises en charge en écriture »
  - `RO_RECOVERY` = « journal à rejouer : branchez le disque sous Ubuntu »
  - `RO_ERRORS` = « système de fichiers marqué en erreur : vérifiez-le sous Ubuntu »
  - `RO_CSUM` = « sommes de contrôle invalides »
  - `RO_NOJOURNAL` = « pas de journal : écriture désactivée par sécurité »
  - `RO_BLOCKSIZE` = « taille de bloc non prise en charge en écriture »
  - `RO_WP` = « disque protégé en écriture »
  - `RO_USER` = « ouvert en lecture seule à votre demande »
  - `RO_IOERR` = « erreur d'écriture : volume passé en lecture seule »

## Review Focus

1. **Débranchement ou mort du processus en pleine copie** (« Fichiers » copie une vidéo vers le
   disque). Attendu :
   - FluLuks ne plante pas ;
   - sur PC, `e2fsck -fy` rejoue le journal, puis `e2fsck -fn` est propre ;
   - à la réouverture sur le téléphone, le volume s'ouvre en lecture seule avec `RO_RECOVERY`.

   *Tests : T5 `test_ext4_coupure_*`, T9 `test_usb_ecriture_disparition`, T10
   `VolumeManagerTest.detachDuringWriteWarns`.*
2. **Disque plein pendant une écriture.** Attendu : le message « plus d'espace disponible », un
   système de fichiers propre, et un fichier partiel cohérent, qu'on peut lire puis supprimer.
   *Tests : T8 `test_batterie_disque_plein`, T11 `ProviderWriteTest.diskFullFailsCleanly`.*
3. **Lectures et écritures concurrentes sur le même volume** (VLC lit pendant que « Fichiers »
   copie). Attendu : des données justes, pas d'interblocage et pas de corruption. *Tests : T4
   `test_ext4_ecriture_concurrente`, T11 `ProviderWriteTest.readWhileWriting`.*
4. **Noms venus d'autres applications.** Les cas visés : nom déjà pris, 255 octets, émoji, `/`, NUL,
   `.` ou `..`. Attendu : un nom unique « nom (1).ext » quand le nom est déjà pris, une erreur propre
   pour un nom interdit, et aucun fichier hors du volume. *Tests : T1 `test_ext4_nom_255`, T11
   `ProviderWriteTest.createNameConflict`, `ProviderWriteTest.forbiddenNames`.*
5. **Mode d'ouverture « w » sur un fichier existant plus long.** Android connaît des fournisseurs qui
   oublient de tronquer et laissent la fin de l'ancien contenu. Attendu : « w », « wt » et « rwt »
   tronquent, « rw » conserve, et « wa » ajoute à la fin. *Tests : T11 `ProviderWriteTest.modeW*`.*

---

## Arborescence

```
native/third_party/patches/lwext4/
  0002-lecture-noms-255-trous-de-dossier.patch      (T1)
  0003-ecriture-api-horodatages-trous.patch         (T4)
  0004-journal-sommes-v3-sequence-barrieres.patch   (T5)
  0005-montage-etat-tables-inodes-sommes.patch      (T6)
  0006-crc32c-rapide.patch                          (T7)
native/src/fs/ext4_glue.c  ext4_policy.c ext4_policy.h      (T4, T6)
native/src/usb/scsi.c usb_bdev.c bot.c                        (T9)
native/jni/handles.c fluluks_jni.c                            (T10)
native/tests/support/{fsck_util.c,fsck_util.h,fault_bdev.c,fault_bdev.h,tree_model.c,tree_model.h} (T3)
native/tests/{test_ext4_ecriture.c,test_ext4_coupure.c,test_ext4_politique.c,test_crc32c.c,
              test_batterie.c}                                (T4–T8)
tools/fixtures/make_fixtures.sh   tools/fixtures/verify_rw_with_kernel.sh (T3, T8)
android/app/src/main/kotlin/com/mawena/fluluks/{engine,volumes,provider}/…  (T10, T11)
lib/src/…  pigeons/fluluks_api.dart                           (T12)
```

---

### Task 1 : Dettes de lecture dans lwext4 (avant toute écriture)

Ce sont des défauts de lecture qui deviennent dangereux dès qu'on écrit. Ils figurent dans les reports
de la phase 1.

**Files :**
- Create : `native/third_party/patches/lwext4/0002-lecture-noms-255-trous-de-dossier.patch`
- Modify : `native/src/fs/ext4_glue.c`, `native/tests/test_ext4.c`, `tools/fixtures/craft.py` (si une
  image fabriquée est nécessaire), `tools/fixtures/make_fixtures.sh`

**Interfaces :**
- Produces : aucune nouvelle API publique. Comportements :
  - Un composant de **255 octets** s'ouvre et se liste (`ext4.c:ext4_path_check` l. 761 : `<` → `<=`).
    256 octets restent refusés par `fs.c` (`FL_EINVAL`).
  - Un **trou dans un dossier** (bloc logique < taille du dossier sans bloc physique, rendu par
    `ext4_fs_get_inode_dblk_idx` comme le bloc 0) ne fait **jamais** lire ni écrire le bloc 0 :
    lwext4 rend `EIO`, la colle le traduit en `FL_ECORRUPT`. Cela vaut pour `ext4_dir_*` (parcours,
    recherche, ajout) et pour les dossiers htree.
  - Une **entrée de dossier inutilisable** (inode hors bornes, nom vide, `/` ou NUL) est ignorée
    pendant le parcours, au lieu de l'interrompre par `EIO`. La recherche par nom l'ignore aussi.

- [ ] **Step 1 : écrire les tests qui échouent** (`test_ext4.c`) :
  - `test_ext4_nom_255` : sur une image dont un fichier a un nom de 255 octets (créée par
    `mkfs.ext4 -d`), `fl_fs_stat` rend `FL_OK`, l'ouverture puis la lecture rendent le contenu attendu,
    et `fl_fs_readdir` le liste.
  - `test_ext4_trou_de_dossier` : sur une image dont un dossier a un bloc logique sans bloc physique
    (fabriquée avec `debugfs -w` : `punch`/`set_inode_field` sur le dossier ; décrire la recette dans
    `make_fixtures.sh`), `fl_fs_readdir` et `fl_fs_stat("/d/x")` rendent `FL_ECORRUPT`. Un compteur
    du bdev de test vérifie qu'**aucune** lecture n'a visé les octets [0, 4096).
  - `test_ext4_entree_hostile_ignoree` : un dossier contenant une entrée à inode hors bornes, puis une
    entrée valide → le parcours rend l'entrée valide, sans erreur.
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : écrire le correctif 0002 et adapter la colle.**
- [ ] **Step 4 : vérifier.** La suite rapide passe sous ASan/UBSan, `test_ext4` compris.
- [ ] **Step 5 : commit**, en cochant T1 — `fix(ext4): noms de 255 octets, trous de dossier, entrées hostiles`

---

### Task 2 : Tests manquants des phases 1 et 2, poignées occupées, écriture LUKS2

**Files :**
- Modify : `native/tests/{test_partition.c,test_luks1.c,test_luks2.c,test_crypto.c,test_handles.c}`,
  `native/jni/handles.c`, `tools/fixtures/craft.py`, `tools/fixtures/make_fixtures.sh`

**Interfaces :**
- Produces : `fj_unmount(vol, false)` pendant la fermeture d'un parent, et l'enregistrement d'un enfant
  sous un parent en cours de fermeture → **`FL_EBUSY`** (au lieu de `FL_EINVAL` transitoire), dans
  `tab_put`.

- [ ] **Step 1 : écrire les tests :**
  - `test_partition_gpt_hostile` : nombre d'entrées démesuré, taille d'entrée 0 ou non multiple de 8,
    LBA des entrées hors du disque → `FL_ECORRUPT`, sans lecture hors bornes.
  - `test_partition_ebr_boucle` : une chaîne EBR qui reboucle → arrêt borné, partitions lues jusque-là.
  - `test_crypto_sha512` : vecteurs FIPS 180-4 (`"abc"` et chaîne vide).
  - `test_luks1_corruptions` : un keyslot dont les matériaux de clé sortent du disque, un nombre de
    stripes de 0, un digest tronqué → `FL_ECORRUPT`.
  - `test_luks2_label` : un en-tête avec label et sous-système se déverrouille normalement.
  - `test_luks2_ecriture_aller_retour` : sur une copie de `whole-luks2-exact.img`, déverrouillée en
    lecture-écriture, on écrit 3 secteurs aux LBA 0, 1000 et dernier − 2, on ferme, on rouvre, et on
    relit les mêmes octets. Le texte chiffré a changé, et ce test seul utilise les paramètres exacts
    d'Argon2.
  - `test_handles_ebusy_fermeture` : un démontage non forcé pendant `closeDisk` en cours → `FL_EBUSY`.
- [ ] **Step 2 : constater les échecs**, s'il y en a.
- [ ] **Step 3 : corriger** ce que les tests révèlent, et `tab_put`.
- [ ] **Step 4 : vérifier.** La suite rapide passe sous ASan/UBSan, et `test_handles` passe sous TSan
  (`build/native-tsan`, supprimé à la fin).
- [ ] **Step 5 : commit**, en cochant T2 — `test: dettes des phases 1 et 2, FL_EBUSY en fermeture, écriture LUKS2`

---

### Task 3 : Banc de non-corruption (images d'écriture, juges, modèle, pannes)

**Files :**
- Create : `native/tests/support/{fsck_util.c,fsck_util.h,fault_bdev.c,fault_bdev.h,tree_model.c,tree_model.h}`,
  `native/tests/test_banc.c`
- Modify : `tools/fixtures/make_fixtures.sh`, `native/CMakeLists.txt`

**Interfaces :**
- Images produites par `make_fixtures.sh`, toutes créées avec `-d tree/` puis **passées par l'étape
  journal** :
  - L'étape journal, sans root : `debugfs -w -R "jo -c"`, puis `jw -r 100` sur un bloc de données
    quelconque, puis `jc`, puis `e2fsck -fy`.
  - Elle donne au journal `journal_incompat_revoke journal_64bit journal_checksum_v3`, comme sur le
    vrai disque, alors que `mkfs` laisse ces options vides.

  | Image | Taille | Options |
  |---|---|---|
  | `ext4-rw-reel.img` | 256 Mio | `-T default -b 4096 -I 256 -O ^orphan_file,^metadata_csum_seed` (options exactes du vrai disque) |
  | `ext4-rw-1k.img` | 64 Mio | idem avec `-b 1024` |
  | `ext4-rw-uninit.img` | 128 Mio | `-O ^metadata_csum,uninit_bg,^orphan_file,^metadata_csum_seed` (anciens disques) |
  | `ext4-rw-seed.img` | 128 Mio | options par défaut d'e2fsprogs 1.47 en amont : `-O metadata_csum_seed,orphan_file` |
  | `ext4-rw-sans-journal.img` | 64 Mio | `-O ^has_journal` |
  | `ext4-rw-meta-bg.img` | 64 Mio | `-O meta_bg,^resize_inode` |
  | `ext4-rw-gros.img` | 8 Gio, creux | options du vrai disque |
  | `luks2-rw-reel.img` | 256 Mio de données | LUKS2 aes-xts-plain64 512 bits, secteurs de 512, **pbkdf2 1000 itérations** (rapide, pour la batterie), contenant `ext4-rw-reel.img` |

  `make_fixtures.sh` vérifie avec `dumpe2fs -h` que les options du journal sont bien posées. Sinon, il
  échoue.
- `fsck_util.h` :
  ```c
  /* 1 si e2fsck/debugfs sont disponibles (PC), 0 sinon (Android, ou outil absent). */
  int fu_tools_available(void);
  /* e2fsck -fn <img> ; renvoie le code de sortie, et copie la sortie (tronquée) dans out. */
  int fu_e2fsck_n(const char *img, char *out, size_t cap);
  /* e2fsck -fy puis -fn ; *replayed = 1 si e2fsck a rejoué le journal ; renvoie le code du -fn. */
  int fu_e2fsck_recover(const char *img, int *replayed, char *out, size_t cap);
  /* Contenu d'un fichier lu par debugfs (dump), indépendant de lwext4. Taille ou <0. */
  int64_t fu_debugfs_cat(const char *img, const char *path, void *buf, size_t cap);
  /* Champ d'inode lu par debugfs ("uid", "gid", "mode", "mtime", "crtime"…). */
  int fu_debugfs_inode_field(const char *img, const char *path, const char *field, uint64_t *out);
  /* Copie creuse de src vers un fichier temporaire sous build/ ; chemin dans out. */
  int fu_copy_image(const char *src, char *out, size_t cap);
  /* Copie d'image LUKS + ext4 : déchiffre l'ext4 vers un fichier temporaire pour e2fsck/debugfs. */
  int fu_extract_luks_ext4(const char *luks_img, const char *pass_file, char *out, size_t cap);
  ```
  Les outils sont lancés avec `posix_spawnp`, l'environnement réduit à `LC_ALL=C`, et les arguments ne
  passent jamais par un shell.
- `fault_bdev.h` :
  ```c
  /* Enveloppe un parent : après `writes_ok` écritures réussies, toute écriture rend `err`
   * (FL_EIO ou FL_ENODEV) sans rien écrire ; les lectures continuent. writes_ok = UINT64_MAX : jamais. */
  int fl_fault_bdev_new(fl_bdev *parent, uint64_t writes_ok, int err, fl_bdev **out);
  uint64_t fl_fault_bdev_writes(const fl_bdev *d);   /* écritures réussies à ce jour */
  uint64_t fl_fault_bdev_flushes(const fl_bdev *d);  /* appels flush */
  ```
- `tree_model.h` : un modèle en mémoire de l'arborescence attendue (chemin → type, taille, sha256,
  uid, gid, mode), avec les fonctions suivantes :
  - `tm_apply_*` : appliquée en même temps que l'opération réelle ;
  - `tm_check_fl(fs)` : comparaison par l'API du moteur ;
  - `tm_check_debugfs(img)` : comparaison par debugfs (`rdump` dans un dossier temporaire, puis
    comparaison).

- [ ] **Step 1 : écrire les tests du banc lui-même** (`test_banc.c`, label `ecriture`) :
  - `test_banc_image_propre` : `fu_e2fsck_n` sur une copie de chaque image `ext4-rw-*` → 0.
  - `test_banc_detecte_corruption` : une copie dont on met à zéro le bitmap de blocs du groupe 0 →
    `fu_e2fsck_n` ≠ 0 et une sortie non vide. Le juge n'est donc pas aveugle.
  - `test_banc_debugfs_lit` : `fu_debugfs_cat(copie, "/hello.txt")` = `"Bonjour FluLuks\n"`.
  - `test_banc_panne` : avec `fault_bdev`, `writes_ok` = 2 → la troisième écriture rend `FL_EIO`, et le
    parent n'a reçu que 2 écritures.
  - `test_banc_modele` : le modèle construit depuis `tree/` concorde avec la copie, à la fois par le
    moteur (lecture) et par debugfs.
  - `test_banc_journal_v3` : `dumpe2fs -h` de chaque image (sauf sans journal) contient
    `journal_checksum_v3`.
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : implémenter le banc et les images.**
- [ ] **Step 4 : vérifier.** `ctest -L ecriture` passe, et la suite rapide aussi. Sur le téléphone,
  `run_native_tests.sh` saute ces tests sans échec.
- [ ] **Step 5 : commit**, en cochant T3 — `test: banc de non-corruption ext4 (images, e2fsck, debugfs, pannes)`

---

### Task 4 : Écriture ext4 dans la colle (API complète, propriétaire, horodatages)

**Files :**
- Create : `native/third_party/patches/lwext4/0003-ecriture-api-horodatages-trous.patch`,
  `native/src/fs/ro_reasons.h`, `native/tests/test_ext4_ecriture.c`
- Modify : `native/src/fs/ext4_glue.c`, `native/src/fs/fs.c`, `native/include/fluluks/fs.h`

**Interfaces :**
- Consumes : le banc de T3.
- Produces :
  - `ro_reasons.h` définit les neuf raisons des Global Constraints, sous la forme
    `#define FL_RO_FEATURES "options ext4 non prises en charge en écriture"`, et ainsi de suite jusqu'à
    `FL_RO_IOERR`. Un test vérifie que chacune tient en 127 octets.
  - `fs.c` ne force plus la lecture seule. La raison « écriture pas encore disponible » disparaît.
    `want_rw = false` → lecture seule avec `FL_RO_USER`. `want_rw = true` sur un `dev->read_only` →
    lecture seule avec `FL_RO_WP` *(décision du planificateur : un bdev n'est en lecture seule à ce
    stade que parce que le disque est protégé ou qu'une image de test est ouverte ainsi)*.
  - `fl_fs_mount(dev, FL_CONTENT_EXT4, want_rw = true, &fs)` monte en écriture si `dev->read_only` est
    faux. La politique complète du § 4.2 arrive en T6 ; en T4, le montage en écriture exige seulement
    `has_journal` (sinon `FL_RO_NOJOURNAL`). Le montage fait `ext4_mount(…, false)` puis `ext4_journal_start`, **sans**
    `ext4_recover` et **sans** `ext4_cache_write_back` *(décision du planificateur : écriture
    immédiate, plus simple et sans perte mesurée par l'essai)*.
  - `fl_fs_open` avec `FL_O_RDWR`, `FL_O_CREAT` ou `FL_O_TRUNC`. `FL_O_CREAT` sur un chemin dont le
    parent n'existe pas → `FL_ENOENT`. Sur un dossier → `FL_EISDIR`.
  - `fl_file_pwrite(f, buf, len, off)` : nombre d'octets écrits (= len) ou fl_err. Une écriture **au-delà
    de la fin** crée un trou, lu comme des zéros. Une écriture partielle due à une erreur d'E/S rend
    l'erreur, jamais un succès court.
  - `fl_file_truncate(f, size)` : vers le bas comme vers le haut (creux).
  - `fl_fs_mkdir`, `fl_fs_unlink` (fichier ou lien ; `FL_EISDIR` sur un dossier), `fl_fs_rmdir`
    (`FL_ENOTEMPTY` si non vide, `FL_EINVAL` sur "/").
  - `fl_fs_rename(from, to)` → **`FL_EEXIST` si `to` existe** *(décision du planificateur : pas de
    remplacement, le provider gère les conflits)*. `FL_EINVAL` si `to` est sous `from`. Le
    renommage d'un dossier vers un autre parent met à jour son `..` et les compteurs de liens.
  - `fl_fs_sync(fs)` : vide le cache de blocs de lwext4 (`ext4_cache_flush`), puis `fl_bdev_flush(dev)`.
  - `fl_file_close(f)` d'un fichier écrit : met à jour `mtime` et `ctime`, puis fait `fl_fs_sync`
    (§ 4.2 : « sync à chaque fermeture d'un fichier ouvert en écriture »).
  - `fl_fs_unmount` d'un volume en écriture : `ext4_journal_stop`, `ext4_umount`, `fl_bdev_flush`.
  - Ajout à `fs.h` :
    ```c
    /* Heure utilisée pour les horodatages (secondes Unix) ; NULL = time(). Pour les tests. */
    void fl_fs_set_clock(int64_t (*now)(void));
    ```
- **Propriétaire, droits et horodatages** (§ 4.2, appliqués par la colle, en une transaction avec la
  création quand lwext4 le permet) :
  - un fichier ou dossier créé reçoit l'**uid et le gid de son dossier parent** ;
  - les droits sont `0644` pour un fichier et `0755` pour un dossier ; si le parent a le bit setgid,
    un **dossier** créé reçoit aussi le bit setgid (`02755`) ;
  - `atime`, `mtime`, `ctime` et `crtime` valent l'heure du téléphone à la création. Le correctif 0003
    ajoute `ext4_crtime_set`, qui renseigne les champs `i_crtime` et `i_crtime_extra` quand
    `extra_isize` ≥ 24 ;
  - écrire ou tronquer met à jour `mtime` et `ctime` du fichier ;
  - créer, supprimer ou renommer une entrée met à jour `mtime` et `ctime` du ou des dossiers parents ;
    le renommage met aussi à jour le `ctime` de l'inode déplacé ;
  - un fichier modifié garde son propriétaire et ses droits.
- Correctif 0003 (défauts relevés par l'essai) :
  - `ext4_ftruncate_no_lock` : agrandir change la taille (creux) ;
  - `ext4_fseek` : position au-delà de la fin autorisée, `SEEK_END` corrigé ;
  - `ext4_fwrite` : l'erreur n'est plus écrasée par `ext4_fs_put_inode_ref` (l. 2033), et une sortie
    anticipée rétablit l'état du cache ;
  - `ext4_dir_dx_init` : la feuille est remplie de zéros, l'inode est mis à 0, **puis** la somme est
    posée ;
  - `ext4_crtime_set`.
- Le verrou global `g_lock` couvre toujours tout appel à lwext4, écritures comprises.

- [ ] **Step 1 : écrire les tests qui échouent** (`test_ext4_ecriture.c`, label `ecriture`). Chaque test
  part d'une copie de `ext4-rw-reel.img`, démonte proprement, puis vérifie `fu_e2fsck_n` = 0 et
  `tm_check_debugfs`. Une horloge figée à 1 760 000 000 sert dans tous les tests :
  - `test_ecriture_petit_fichier` : création puis écriture de 1 Kio, contenu relu par le moteur et par
    debugfs.
  - `test_ecriture_proprietaire_herite` : le dossier `/u` a uid/gid 1000/1000 (fixé par
    `debugfs -w sif` dans la copie). Un fichier créé dedans a uid/gid 1000/1000 et le mode `0100644` ;
    un dossier créé dedans a le mode `040755`. Dans `/s`, dossier setgid `02775`, un dossier créé a le
    mode `042755`.
  - `test_ecriture_horodatages` : `mtime`, `ctime` et `crtime` du fichier créé valent 1 760 000 000.
    On avance l'horloge à 1 760 000 100 ; après une écriture et la fermeture, `mtime` = `ctime` =
    1 760 000 100 et `crtime` est inchangé. Le parent a `mtime` = 1 760 000 000.
  - `test_ecriture_gros_fichier` : 300 Mio écrits par blocs de 1 Mio, relus par sha256.
  - `test_ecriture_trou` : `pwrite` à 10 Mio sur un fichier vide → taille 10 Mio + n ; la lecture de
    [0, 10 Mio) rend des zéros ; `debugfs stat` montre moins de 1 Mio alloué.
  - `test_ecriture_troncature` : 300 → 10 Mio, puis 10 → 50 Mio (creux), puis → 0, puis → 4097 octets.
  - `test_ecriture_dossier_vide` : `mkdir` d'un dossier qui reste vide → `e2fsck -fn` propre (c'était
    le défaut de somme relevé par l'essai).
  - `test_ecriture_3000` : 3 000 fichiers dans un dossier (htree), puis suppression de la moitié.
  - `test_ecriture_renommage` : dans un dossier, entre dossiers, un dossier vers un autre parent (`..`
    correct), cible existante → `FL_EEXIST`, `to` sous `from` → `FL_EINVAL`.
  - `test_ecriture_suppression` : `unlink` sur un dossier → `FL_EISDIR` ; `rmdir` non vide →
    `FL_ENOTEMPTY` ; puis vidage et `rmdir` → propre.
  - `test_ecriture_noms_unicode` : accents, émoji, 255 octets.
  - `test_ext4_ecriture_concurrente` : 4 fils pendant 3 s (2 lecteurs d'un fichier existant, 2
    écrivains de fichiers distincts) → contenus justes, `e2fsck` propre, pas d'interblocage (`alarm`
    de 60 s).
  - `test_ecriture_lecture_seule_api` : un volume monté avec `want_rw = false` → toutes les opérations
    d'écriture rendent `FL_EROFS` et l'image est **identique octet pour octet** avant et après.
  - `test_ecriture_sync_flush` : sur `fault_bdev` (sans panne), fermer un fichier écrit appelle
    `flush` au moins une fois.
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : implémenter le correctif 0003 et la colle.**
- [ ] **Step 4 : vérifier.** `ctest -L ecriture` et la suite rapide passent sous ASan/UBSan.
- [ ] **Step 5 : commit**, en cochant T4 — `feat(ext4): écriture (fichiers, dossiers, propriétaire hérité, horodatages)`

---

### Task 5 : Journal sûr (sommes v3, séquence, barrières) et tests de coupure

**Files :**
- Create : `native/third_party/patches/lwext4/0004-journal-sommes-v3-sequence-barrieres.patch`,
  `native/tests/test_ext4_coupure.c`
- Modify : `native/src/fs/ext4_glue.c`

**Interfaces :**
- Correctif 0004 :
  - **Sommes jbd2 v3** : `jbd_block_csum` hache la séquence en big-endian, et `jbd_commit_csum_set`
    stocke `to_be32(…)`. C'est `csum_fix.diff` du rapport.
  - **Séquence** : `jbd_journal_stop` ne remet plus `trans_id` à 0. Le superbloc du journal garde
    `s_sequence` = prochain identifiant, comme jbd2, d'une session à l'autre.
  - **Barrières** : `struct ext4_blockdev_iface` gagne `int (*bflush)(struct ext4_blockdev *bdev)`
    (NULL accepté). lwext4 l'appelle à quatre moments :
    1. après l'écriture des blocs de descripteur et de données d'une transaction, avant le bloc de
       commit ;
    2. après le bloc de commit ;
    3. avant de mettre à jour `s_start` du superbloc du journal au point de contrôle ;
    4. à `ext4_cache_flush`.

    La colle le branche sur `fl_bdev_flush` (sur USB, `SYNCHRONIZE CACHE`).
- Comportement attendu après coupure (spec § 4.2 : « le journal doit permettre de récupérer à la
  prochaine ouverture sous Ubuntu ») : `e2fsck -fy` rejoue, puis `e2fsck -fn` est propre, et tout fichier
  fermé avant la dernière transaction validée est intact.

- [ ] **Step 1 : écrire les tests qui échouent** (`test_ext4_coupure.c`, label `ecriture`). Base :
  copie de `ext4-rw-reel.img` sous `fault_bdev(writes_ok = N, FL_EIO)`, puis abandon **sans
  démontage**. La colle doit libérer sans planter, sans fuite ASan, et rendre une erreur. Ensuite
  `fu_e2fsck_recover` doit valoir 0 :
  - `test_ext4_coupure_gros_fichier` : 300 Mio, N ∈ {50, 500, 2000, 3000, 4000, 4500}.
  - `test_ext4_coupure_3000` : 3 000 fichiers, N ∈ {500, 5000, 20000, 40000}.
  - `test_ext4_coupure_fichiers_fermes_intacts` : on écrit et ferme 20 fichiers de 64 Kio, puis on lance
    un gros fichier coupé à N = 2000 → après récupération, les 20 fichiers ont leur sha256.
  - `test_ext4_coupure_sessions` : une session propre, une deuxième session propre de 400 créations, puis
    une troisième coupée à N ∈ {100, 1000}. Après récupération, `e2fsck` ne signale ni lost+found ni
    inode « unused area », et les fichiers des sessions 1 et 2 sont intacts (défaut de séquence de
    l'essai).
  - `test_ext4_journal_sommes` : après une session propre de 50 créations, le journal est relu par un
    vérificateur C dans le test, qui reprend `jcheck.py` du rapport. Toutes les sommes de tags et de
    commit sont valides.
  - `test_ext4_barrieres` : pendant une création de fichier, `fl_fault_bdev_flushes` augmente d'au moins
    2 par transaction validée.
- [ ] **Step 2 : constater l'échec**, attendu au moins sur les sommes et les sessions.
- [ ] **Step 3 : implémenter le correctif 0004 et le branchement de `bflush`.**
- [ ] **Step 4 : vérifier.** `ctest -L ecriture` et la suite rapide passent sous ASan/UBSan.
- [ ] **Step 5 : commit**, en cochant T5 — `fix(ext4): journal jbd2 v3 correct, séquence conservée, barrières`

---

### Task 6 : Politique de montage du § 4.2 et robustesse

**Files :**
- Create : `native/src/fs/ext4_policy.c`, `native/src/fs/ext4_policy.h`,
  `native/third_party/patches/lwext4/0005-montage-etat-tables-inodes-sommes.patch`,
  `native/tests/test_ext4_politique.c`
- Modify : `native/src/fs/ext4_glue.c`, `native/src/fs/fs.c`

**Interfaces :**
- `ext4_policy.h` :
  ```c
  /* Décide, d'après le superbloc validé (1024 octets), le superbloc du journal et les descripteurs
   * de groupe, si l'écriture est permise. Renvoie FL_OK et remplit *rw et reason (≤ 127 octets, "" si rw). */
  int fl_ext4_write_policy(fl_bdev *dev, const uint8_t sb[1024], bool *rw, char reason[128]);
  ```
- **Règles, dans cet ordre** (la première qui s'applique donne la raison) :
  1. Le bdev est en lecture seule (`dev->read_only`) → `RO_WP`. Une demande de l'utilisateur
     (`want_rw = false`) est traitée avant la politique par `fs.c` (`RO_USER`, T4).
  2. Une option incompat hors `FILETYPE | EXTENTS | 64BIT | FLEX_BG | CSUM_SEED`, une option ro_compat
     hors `SPARSE_SUPER | LARGE_FILE | HUGE_FILE | DIR_NLINK | EXTRA_ISIZE | METADATA_CSUM |
     GDT_CSUM`, ou une option journal incompat hors `REVOKE | 64BIT | CSUM_V3` → `RO_FEATURES`. Cela
     couvre `meta_bg`, `orphan_present`, `inline_data`, `bigalloc`, `casefold`, `encrypt`, `verity`,
     `quota`, `mmp`, `csum_v2` et `async_commit`.
  3. `has_journal` absent → `RO_NOJOURNAL`.
  4. `RECOVER` (needs_recovery) → `RO_RECOVERY` *(décision du planificateur : le journal n'est jamais
     rejoué sur le téléphone ; Ubuntu le rejoue, comme le demande le § 4.2)*.
  5. `s_state` sans VALID, ou avec ERROR, ou `s_errors_count` ≠ 0 → `RO_ERRORS`.
  6. La somme du superbloc, d'**au moins un** descripteur de groupe ou du superbloc du journal est
     fausse → `RO_CSUM`.
  7. Taille de bloc hors {1024, 2048, 4096} → `RO_BLOCKSIZE`.
- Au montage, la colle valide le superbloc (déjà fait), appelle la politique, puis monte lwext4 en
  lecture seule ou en écriture. `fl_statfs.read_only` et `ro_reason` reflètent le résultat.
- **Lecture seule collante en cours de session** : toute erreur d'écriture du bdev, ou toute somme
  fausse détectée par lwext4, fait passer le volume en lecture seule (`RO_IOERR` ou `RO_CSUM`). Les
  écritures suivantes rendent alors `FL_EROFS`, mais la lecture continue.
- Correctif 0005 :
  - **`s_state`** : pendant un montage en écriture **avec journal**, lwext4 laisse `s_state` à VALID
    (au lieu de ERROR_FS, `ext4_fs.c:107-120`), comme le noyau ; `needs_recovery` suffit à signaler
    l'état.
  - **Tables d'inodes** : `ext4_fs_get_block_group_ref` ne remet **jamais** à zéro une table d'inodes
    (`ext4_fs.c:600-627`), et `bg_itable_unused` reste juste à chaque allocation d'inode.
  - **csum_seed** : `CSUM_SEED` est pris en charge en lecture et en écriture ; la graine vient de
    `s_checksum_seed`.
  - **Sommes fausses** : une somme fausse à la lecture d'un inode, d'un bloc d'extents, d'un bloc de
    dossier, d'un bitmap ou d'un descripteur de groupe rend `EBADMSG` (au lieu d'être seulement
    journalisée). La colle traduit `EBADMSG` en `FL_ECORRUPT` et passe le volume en lecture seule.
  - **NULL déréférencé** : `ext4_block_get_noread` ne pose `lb_id` qu'après la réussite de
    `ext4_block_cache_shake`.
  - **Superbloc** : au montage en écriture et au démontage, lwext4 met à jour `s_mtime`, `s_wtime` et
    `s_kbytes_written`.
- **Démontage quand le disque est mort** : si le bdev rend `FL_ENODEV` ou `FL_EIO`, `fl_fs_unmount`
  libère tout (pas de fuite, pas de pointeur gardé par lwext4) et rend l'erreur.

- [ ] **Step 1 : écrire les tests qui échouent** (`test_ext4_politique.c`, label `ecriture`) :
  - `test_politique_vrai_disque_rw` : `ext4-rw-reel.img`, `ext4-rw-1k.img`, `ext4-rw-uninit.img` et
    `ext4-rw-seed.img` → écriture permise. Une création y donne `e2fsck` propre.
  - `test_politique_sans_journal` → `RO_NOJOURNAL`, et l'image reste identique octet pour octet.
  - `test_politique_meta_bg` → `RO_FEATURES`.
  - `test_politique_orphan_present` : bit ro_compat 0x10000 posé par `debugfs -w ssv` → `RO_FEATURES`.
  - `test_politique_recover` : bit `RECOVER` posé → `RO_RECOVERY` ; la lecture marche, l'image est
    identique.
  - `test_politique_erreurs` : `s_state` = 2 → `RO_ERRORS`.
  - `test_politique_somme_descripteur` : un octet du descripteur de groupe 3 inversé → `RO_CSUM`.
  - `test_politique_somme_en_cours` : un bloc de dossier corrompu (un octet inversé) dans un sous-dossier
    → l'écriture dedans rend `FL_ECORRUPT`, puis `statfs` = lecture seule avec `RO_CSUM`.
  - `test_politique_etat_pendant_montage` : pendant un montage en écriture, `dumpe2fs -h` de l'image
    (copiée à chaud) montre l'état `clean` et `needs_recovery`.
  - `test_politique_premier_montage_sobre` : une copie neuve de `ext4-rw-gros.img` (8 Gio, tables
    d'inodes non initialisées), un montage en écriture puis la création d'un fichier de 4 Kio → **moins
    de 200** écritures de bloc (`fault_bdev`) ; `e2fsck` propre.
  - `test_politique_disque_mort` : `fault_bdev(N = 100, FL_ENODEV)` en pleine écriture, puis
    `fl_fs_unmount` → rend une erreur, sans fuite ASan, et un nouveau montage d'une autre copie
    réussit (les 8 emplacements de lwext4 ne s'épuisent pas).
  - `test_politique_eio_ecriture_differee` : le scénario de NULL de l'essai, sans plantage.
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : implémenter la politique, le correctif 0005 et la colle.**
- [ ] **Step 4 : vérifier.** `ctest -L ecriture` et la suite rapide passent sous ASan/UBSan.
- [ ] **Step 5 : commit**, en cochant T6 — `feat(ext4): règles de lecture seule du § 4.2, montage sobre et sûr`

---

### Task 7 : crc32c rapide

L'essai a montré que 96 % du temps d'écriture part dans le crc32c octet par octet de lwext4.

**Files :**
- Create : `native/third_party/patches/lwext4/0006-crc32c-rapide.patch`, `native/tests/test_crc32c.c`
- Modify : `native/cmake/third_party.cmake`, si une option de compilation est nécessaire

**Interfaces :**
- `ext4_crc32c(uint32_t crc, const void *buf, uint32_t size)` garde sa signature. Son implémentation
  devient :
  - par tranches de 8 (*slicing-by-8*, tables construites à la compilation ou une seule fois sous
    `pthread_once`) ;
  - en arm64, l'instruction `crc32c` (`__crc32cd`/`__crc32cb`) si `getauxval(AT_HWCAP) & HWCAP_CRC32`,
    dans une fonction compilée avec `__attribute__((target("+crc")))` ;
  - en x86_64, `_mm_crc32_u64` si `__builtin_cpu_supports("sse4.2")`.
- Résultat identique au bit près à l'implémentation d'origine, pour toute longueur et tout alignement.

- [ ] **Step 1 : écrire les tests qui échouent** (`test_crc32c.c`) :
  - `test_crc32c_vecteurs` : `"123456789"` → `0xE3069283` (crc32c standard, avec init et xor final
    comme dans lwext4) ; 32 octets à zéro → la valeur RFC 3720.
  - `test_crc32c_equivalence` : 10 000 tampons aléatoires (graine fixe), longueurs 0–9000, décalages
    0–7 → identiques à une copie de référence de l'ancien algorithme, gardée dans le test.
  - `test_crc32c_debit` *(label `lent`, sans seuil bloquant)* : affiche les Mo/s.
- [ ] **Step 2 : constater l'échec** (la fonction de chemin rapide n'existe pas).
- [ ] **Step 3 : implémenter le correctif 0006.**
- [ ] **Step 4 : vérifier.** La suite rapide et `ctest -L ecriture` passent. `test_ecriture_gros_fichier`
  est au moins 2 fois plus rapide qu'avant : noter les deux durées dans le rapport. Les tests natifs
  passent sur le téléphone (`tools/android/run_native_tests.sh 192.168.0.104:5555`), chemin matériel
  arm64 compris.
- [ ] **Step 5 : commit**, en cochant T7 — `perf(ext4): crc32c par tranches et instructions matérielles`

---

### Task 8 : Batterie de non-corruption complète

**Files :**
- Create : `native/tests/test_batterie.c`, `tools/fixtures/verify_rw_with_kernel.sh`
- Modify : `native/CMakeLists.txt`

**Interfaces :**
- `test_batterie` (label `ecriture` ; la variante longue porte le label `lent`) applique des opérations
  pseudo-aléatoires (graine affichée, rejouable par `FL_SEED=<n>`) en tenant le modèle de T3 :
  - création ;
  - ajout à la fin ;
  - réécriture au milieu ;
  - troncature haut/bas ;
  - renommage et déplacement ;
  - suppression ;
  - `mkdir`/`rmdir` ;
  - noms Unicode ;
  - profondeur jusqu'à 40.

  Tous les 200 pas, et à la fin : démontage, `fu_e2fsck_n` = 0, `tm_check_debugfs`, puis remontage.
- `verify_rw_with_kernel.sh <dossier>` (**sudo, lancé par l'utilisateur**) : il monte en lecture seule
  par loop (avec `cryptsetup open --readonly` pour LUKS) chaque image `*.ecrite.img` que la batterie
  laisse dans `build/native/batterie/`. Il compare le contenu au manifeste `*.manifest` (chemin, type,
  taille, sha256, uid, gid, mode) et affiche « NOYAU OK <image> » ou la différence.

- [ ] **Step 1 : écrire les tests :**
  - `test_batterie_reel` : 2 000 opérations sur `ext4-rw-reel.img`.
  - `test_batterie_1k`, `test_batterie_uninit`, `test_batterie_seed` : 1 000 opérations chacun.
  - `test_batterie_luks2` : 1 000 opérations sur `luks2-rw-reel.img`, puis `fu_extract_luks_ext4` et
    `e2fsck -fn`.
  - `test_batterie_secteurs_4k` : la même batterie sur un bdev à secteurs de 4096 (copie de
    `ext4-rw-reel.img` présentée en 4096).
  - `test_batterie_gros_fichiers` : sur `ext4-rw-gros.img`, un fichier creux de 5 Gio (écrit à 4,5 Gio)
    et, en variante `lent`, un fichier **dense** de 4,1 Gio relu par sha256.
  - `test_batterie_petits_fichiers` : 20 000 fichiers de 0 à 8 Kio répartis dans 50 dossiers.
  - `test_batterie_disque_plein` : on remplit `ext4-rw-1k.img` jusqu'à `FL_ENOSPC`. La dernière
    écriture rend `FL_ENOSPC`, le fichier partiel est lisible puis supprimable, `e2fsck` est propre, et
    la place revient (`statfs`).
  - `test_batterie_longue` *(lent)* : 20 000 opérations, graines 1 à 5.
- [ ] **Step 2 : lancer, corriger** ce que la batterie révèle : soit par un nouveau correctif
  `0007-…` (même règle d'ordre), soit dans la colle. Chaque défaut trouvé reçoit un test dédié dans
  `test_ext4_ecriture.c`.
- [ ] **Step 3 : vérifier.** `ctest -L ecriture` passe, puis `ctest -L lent` (noter la durée).
  Demander à l'utilisateur de lancer
  `sudo tools/fixtures/verify_rw_with_kernel.sh build/native/batterie` et noter le résultat dans le
  rapport.
- [ ] **Step 4 : commit**, en cochant T8 — `test: batterie de non-corruption ext4 (e2fsck, debugfs, noyau)`

---

### Task 9 : Écriture USB (WRITE, SYNCHRONIZE CACHE, protection)

**Files :**
- Modify : `native/src/usb/{scsi.c,usb_bdev.c,bot.c,usb_internal.h}`, `native/include/fluluks/usb.h`,
  `native/tests/support/fake_msc.{c,h}`, `native/tests/test_usb.c`

**Interfaces :**
- `usb.h` : on retire le texte « Phase 2 : lecture seule ». `fl_usb_bdev_open(t, lun, read_only, &out)`
  garde sa signature, et :
  - si `read_only` est faux, l'ouverture envoie `MODE SENSE(6)` (page 0x3F, 192 octets). Le bit WP
    (octet 2, bit 7) force `read_only = true`. Un échec de `MODE SENSE` n'empêche pas l'ouverture et
    ne force rien ;
  - nouveau :
    ```c
    /* Vrai si le disque a signalé une protection en écriture (MODE SENSE ou sens DATA PROTECT). */
    bool fl_usb_bdev_write_protected(const fl_bdev *d);
    ```
- Écriture : `WRITE(10)`, ou `WRITE(16)` au-delà de 32 bits, **64 Kio au plus par commande**. Les phases
  données et CSW ont un délai de 30 s.
  - Un sens DATA PROTECT → `FL_EROFS`, et le périphérique passe en protection collante.
  - Après `FL_ENODEV`, toute écriture rend `FL_ENODEV` sans rien envoyer.
- `flush` envoie `SYNCHRONIZE CACHE(10)` (délai de 30 s). Un ILLEGAL REQUEST (commande non prise en
  charge) vaut `FL_OK`.
- `fake_msc` : il écrit dans son image et compte les `WRITE` et `SYNCHRONIZE CACHE` reçus. Il simule la
  protection (`fake_msc_set_write_protect(true)`, en MODE SENSE et en sens 07/27/00) et une panne à la
  n-ième écriture.

- [ ] **Step 1 : écrire les tests qui échouent** (`test_usb.c`) :
  - `test_usb_ecriture_aller_retour` : on écrit 3 Mio à des décalages variés, on les relit
    identiques, et 1 Mio part en 16 commandes `WRITE(10)`.
  - `test_usb_ecriture_16` : un LBA > 2³² (capacité simulée) → `WRITE(16)`.
  - `test_usb_flush` : `fl_bdev_flush` → un `SYNCHRONIZE CACHE` reçu. Si le disque répond ILLEGAL
    REQUEST → `FL_OK`.
  - `test_usb_protection_mode_sense` : WP=1 → `read_only` vrai, `fl_usb_bdev_write_protected` vrai,
    aucun `WRITE` émis.
  - `test_usb_protection_sens` : MODE SENSE sans WP mais sens 07 à l'écriture → `FL_EROFS`, puis
    protection collante.
  - `test_usb_ecriture_disparition` : `FL_ENODEV` au 3ᵉ `WRITE` → `FL_ENODEV`, et plus rien n'est
    envoyé.
  - `test_usb_lecture_seule_respectee` : ouvert avec `read_only = true` → aucun `MODE SENSE` ni
    `WRITE` ; `fl_bdev_write` → `FL_EROFS`.
  - `test_usb_pile_ecriture` : `luks2-rw-reel.img` via `fake_msc` → partitions → LUKS2 → ext4 en
    écriture : création d'un fichier, démontage, puis `e2fsck -fn` propre sur l'ext4 extrait (label
    `ecriture`).
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier.** `ctest -R test_usb` passe sans rapport ASan, puis la suite rapide.
  Compilation `build/android-arm64`.
- [ ] **Step 5 : commit**, en cochant T9 — `feat(usb): écriture SCSI, SYNCHRONIZE CACHE, protection en écriture`

---

### Task 10 : JNI et VolumeManager en écriture (synchronisation, éjection, débranchement)

**Files :**
- Modify : `native/jni/{handles.c,handles.h,fluluks_jni.c}`, `native/tests/test_handles.c`,
  `android/app/src/main/kotlin/com/mawena/fluluks/{engine/Engine.kt,engine/NativeEngine.kt,engine/EngineModels.kt,volumes/VolumeManager.kt,volumes/DiskState.kt,service/VolumeService.kt}`,
  `android/app/src/test/…/VolumeManagerTest.kt`, `android/app/src/androidTest/…/EngineTest.kt`

**Interfaces :**
- `handles.h` (C) :
  ```c
  /* Image disque ; read_only = false pour les tests d'écriture (secteurs de 512, cache 64 Kio × 256). */
  int64_t fj_open_image(const char *path, bool read_only);
  /* Disque USB ouvert en lecture-écriture (MODE SENSE décide de la protection). */
  int64_t fj_open_usb(int fd, int iface, int ep_in, int ep_out);
  /* want_rw = false → RO_USER. */
  int64_t fj_mount(int64_t disk, uint32_t part_index, const void *pass, size_t passlen, bool want_rw);
  int64_t fj_open_file(int64_t vol, const char *path, int flags);   /* FL_O_* */
  int64_t fj_pwrite(int64_t file, const void *buf, size_t len, uint64_t off); /* len ≤ FJ_PREAD_MAX */
  int fj_truncate(int64_t file, uint64_t size);
  int fj_mkdir(int64_t vol, const char *path);
  int fj_unlink(int64_t vol, const char *path);
  int fj_rmdir(int64_t vol, const char *path);
  int fj_rename(int64_t vol, const char *from, const char *to);
  int fj_sync(int64_t vol);
  ```
  - `fj_close_file` d'un fichier écrit fait la synchronisation (T4).
  - `fj_unmount(vol, force)` d'un volume en écriture : `sync`, puis démontage. Si le disque est mort,
    il libère quand même (T6).
  - Le JSON `statfs` garde `read_only` et `ro_reason`. Un disque protégé ou une demande de
    l'utilisateur donnent `RO_WP` ou `RO_USER`.
- `Engine.kt` (Kotlin) : `openImage(path: ByteArray, readOnly: Boolean)`,
  `mount(disk, partIndex, pass: ByteArray?, writable: Boolean)`, `openFile(vol, path, flags: Int)`,
  `pwrite(file, offset, buf, len): Int`, `truncate(file, size: Long): Int`, `mkdir`, `unlink`,
  `rmdir`, `rename(vol, from: ByteArray, to: ByteArray)` et `sync(vol): Int`. Les chemins sont en
  octets UTF-8, comme en phase 2.
- `VolumeManager` :
  - `unlock(diskId, partIndex, pass, writable: Boolean)` et `mountPlain(diskId, partIndex, writable)`.
  - `VolumeState` gagne `readOnly: Boolean`.
  - `fun noteWrite(volumeId: String)` est appelée par le provider après chaque écriture. Elle programme
    un `sync` du volume **3 s après la dernière écriture** *(décision du planificateur)*, sur
    l'exécuteur unique.
  - `fun hasUnsyncedWrites(volumeId): Boolean`.
  - **Éjection** :
    - la notification passe à « Écriture des données en attente… » ;
    - puis `sync`, démontage normal (`SYNCHRONIZE CACHE` compris) et effacement de la clé ;
    - et seulement ensuite la libération de l'interface USB ;
    - en cas d'échec : « Éjection impossible : <message> », et le volume reste ouvert.
  - **Débranchement** avec des écritures non synchronisées : notification d'avertissement « Disque
    débranché pendant une écriture : vérifiez-le sous Ubuntu avant de le réutiliser » (canal existant,
    importance haute).

- [ ] **Step 1 : écrire les tests qui échouent :**
  - `test_handles.c` : `fj_mount(..., want_rw = true)` sur une copie de `ext4-rw-reel.img` → création,
    `fj_pwrite`, `fj_close_file` (synchronisation vérifiée par compteur), `fj_rename`, `fj_unmount`,
    puis `e2fsck -fn` propre (label `ecriture`) ; `want_rw = false` → `ro_reason` = `RO_USER`.
  - `EngineTest` (instrumenté, image copiée dans le cache de l'application) : écriture puis relecture
    d'un fichier de 5 Mio, `mkdir`/`rename`/`unlink`, nom émoji. Le script
    `tools/android/check_image.sh <serial>` rapatrie l'image et lance `e2fsck -fn` sur le PC → 0.
  - `VolumeManagerTest` (JVM, moteur factice) :
    - `writeSchedulesSyncAfter3s` : synchronisation 3 s après la dernière écriture, une seule pour une
      rafale ;
    - `ejectSyncsBeforeUnmountAndRelease` : l'ordre est sync → démontage → libération ;
    - `ejectFailureKeepsVolume` ;
    - `detachDuringWriteWarns`.
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier.** Les tests natifs, `testDebugUnitTest` et `connectedDebugAndroidTest` sur
  `192.168.0.104:5555` passent, puis `check_image.sh` → `e2fsck` propre.
- [ ] **Step 5 : commit**, en cochant T10 — `feat: écriture via JNI, synchronisation, éjection propre`

---

### Task 11 : DocumentsProvider en écriture

**Files :**
- Modify : `android/app/src/main/kotlin/com/mawena/fluluks/provider/{FluLuksDocumentsProvider.kt,DocumentIds.kt,RootSummary.kt}`
- Create : `android/app/src/main/kotlin/com/mawena/fluluks/provider/{WriteOps.kt,DocNames.kt}`,
  `android/app/src/androidTest/…/ProviderWriteTest.kt`, `android/app/src/test/…/DocNamesTest.kt`

**Interfaces :**
- **Drapeaux, volume en écriture seulement :**
  - racine : `FLAG_SUPPORTS_CREATE`, plus ceux de la phase 2 ;
  - document : `FLAG_SUPPORTS_WRITE` (fichiers), `FLAG_DIR_SUPPORTS_CREATE` (dossiers),
    `FLAG_SUPPORTS_DELETE`, `FLAG_SUPPORTS_RENAME`, `FLAG_SUPPORTS_MOVE` et `FLAG_SUPPORTS_COPY` ;
  - un volume en lecture seule garde les drapeaux de la phase 2. Son résumé de racine devient
    « Lecture seule : <raison> ».
- **`openDocument(id, mode)`** : modes analysés comme `ParcelFileDescriptor.parseMode` :
  - « r » : lecture ;
  - « w », « wt » et « rwt » : troncature à 0 ;
  - « wa » : ajout à la fin ;
  - « rw » : conservation du contenu.

  Un mode inconnu → `IllegalArgumentException`. Une écriture sur un volume en lecture seule →
  `FileNotFoundException("Volume en lecture seule : <raison>")`. Le rappel proxy (sur
  `HandlerThread("fluluks-io")`) fonctionne ainsi :
  - `onWrite(off, size, data)` → `pwrite` puis `VolumeManager.noteWrite` ; en cas d'erreur,
    `ErrnoException("onWrite", EIO)` (`ENOSPC` pour `FL_ENOSPC`, `EROFS` pour `FL_EROFS`) ;
  - `onGetSize` suit la plus grande fin écrite ;
  - `onFsync` → `sync` ;
  - `onRelease` → `closeFile` (synchronisation comprise), puis notification du dossier parent.
- **`createDocument(parent, mime, nom)`** :
  - **Nom déjà pris** → « nom (1).ext », « nom (2).ext »… jusqu'à 999 essais, puis
    `FileNotFoundException`.
  - **Nom interdit** → `FileNotFoundException("Nom invalide")` : vide, `.`, `..`, contient `/` ou NUL,
    ou fait plus de 255 octets UTF-8.
  - **Résultat** : un dossier si `mime` = `Document.MIME_TYPE_DIR`, sinon un fichier vide.
  - **Identifiant rendu** : `DocumentIds.encode`.
- **`deleteDocument(id)`** : un fichier, ou un dossier **récursivement**, en parcours itératif, en
  supprimant d'abord les enfants. La racine du volume → `FileNotFoundException`.
- **`renameDocument(id, nom)`** : même parent, mêmes règles de nom. Cible existante →
  `FileNotFoundException("Un élément porte déjà ce nom")`. Rend le nouvel identifiant.
- **`moveDocument(id, source, cible)`** : le même volume seulement, sinon `UnsupportedOperationException`.
  En cas de conflit, il prend le nom unique suivant.
- **`copyDocument(id, cible)`** : le même volume. Un fichier est copié par tranches de 1 Mio, avec les
  mêmes règles de propriétaire que pour une création. Un dossier est copié récursivement. Copier un
  dossier vers lui-même ou vers un de ses descendants → `FileNotFoundException`.
- Après chaque création, suppression, renommage, déplacement ou copie, puis à la fermeture d'un fichier
  écrit : `notifyChange` sur les enfants du ou des parents.

- [ ] **Step 1 : écrire les tests qui échouent** (`ProviderWriteTest`, volume image en écriture,
  par `ContentResolver` et `DocumentsContract`) :
  - `createWriteReadBack` : `createDocument` + `openOutputStream`, 3 Mio écrits puis relus à
    l'identique.
  - `modeWTruncates`, `modeWtTruncates`, `modeRwtTruncates` : un fichier de 1 Mio réécrit avec 10
    octets → taille 10.
  - `modeRwKeeps` → taille 1 Mio, les 10 premiers octets remplacés.
  - `modeWaAppends` → taille 1 Mio + 10.
  - `createNameConflict` : deux créations de « a.txt » → « a.txt » et « a (1).txt ».
  - `forbiddenNames` : "", ".", "..", "a/b", un nom de 256 octets → `FileNotFoundException` ; un nom
    de 255 octets et un émoji → OK.
  - `deleteRecursive` : un dossier de 3 niveaux et 50 fichiers → tout disparaît.
  - `renameAndMove`, `copyFileAndDir` : contenus et noms attendus.
  - `readOnlyVolumeRefusesWrite` : un volume monté en lecture seule → `openDocument("w")` lève une
    exception avec la raison, et aucun drapeau d'écriture n'est posé.
  - `readWhileWriting` : un fil lit un fichier existant pendant qu'un autre en écrit un nouveau de
    20 Mio → les deux contenus sont justes.
  - `diskFullFailsCleanly` : sur une petite image, écrire jusqu'à l'erreur → `IOException`, le fichier
    reste supprimable, et l'image rapatriée passe `e2fsck -fn`.
  - `DocNamesTest` (JVM) : « a.txt » → « a (1).txt », « a » → « a (1) », « .bashrc » → « .bashrc (1) »,
    « a.tar.gz » → « a.tar (1).gz » (comme Android, extension = dernier point).
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier.** `testDebugUnitTest` et `connectedDebugAndroidTest` passent, et l'image
  rapatriée passe `e2fsck -fn`.
- [ ] **Step 5 : commit**, en cochant T11 — `feat(provider): création, écriture, renommage, déplacement, copie, suppression`

---

### Task 12 : Interface Flutter (lecture seule, éjection, avertissements)

**Files :**
- Modify : `pigeons/fluluks_api.dart` (puis régénération Pigeon 27.3.0), `bridge/HostApiImpl.kt`,
  `lib/src/{strings_fr.dart,state/app_state.dart,state/host_api.dart,screens/disks_screen.dart,widgets/partition_tile.dart,widgets/unlock_dialog.dart}`,
  `test/{fake_host_api.dart,disks_screen_test.dart,unlock_dialog_test.dart}`

**Interfaces :**
- Pigeon :
  - `unlock(String diskId, int partIndex, Uint8List passphrase, bool writable)` et
    `mountPlain(String diskId, int partIndex, bool writable)` ;
  - `VolumeMsg` gagne `bool readOnly` ;
  - `bool getReadOnlyPreference()` et `void setReadOnlyPreference(bool value)`, gardés dans les
    `SharedPreferences` côté Kotlin. Valeur par défaut : **faux**.
- Écran Disques, chaînes exactes :
  - En haut, un interrupteur « Ouvrir les disques en lecture seule ». Quand il est actif, `writable` vaut
    faux pour tout nouveau montage.
  - Sous un volume ouvert en lecture seule : « Lecture seule : <roReason> ».
  - Sous un volume ouvert en écriture : « Ouvert en lecture et écriture — visible dans l'application
    Fichiers ». Le texte de la phase 2, « Ouvert — visible dans l'application Fichiers », reste pour la
    lecture seule.
  - Pendant l'éjection : un indicateur et « Écriture des données en attente… », avec les boutons
    désactivés.
  - Si l'éjection échoue : « Éjection impossible : <message> ».

- [ ] **Step 1 : écrire les tests qui échouent :**
  - `disks_screen_test` :
    - interrupteur actif → `unlock(..., writable: false)` ;
    - volume en lecture seule → « Lecture seule : journal à rejouer : branchez le disque sous Ubuntu » ;
    - volume en écriture → le nouveau texte ;
    - éjection en cours → indicateur et boutons désactivés.
  - `unlock_dialog_test` : `writable` est transmis.
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier.** `flutter test`, `flutter analyze` et `flutter build apk --debug`.
- [ ] **Step 5 : commit**, en cochant T12 — `feat(ui): lecture seule choisie ou imposée, éjection avec écriture`

---

### Task 13 : Essais sur le Pixel, puis version 0.2.0

Les étapes physiques sont faites **par l'utilisateur** : l'agent prépare, guide et consigne les résultats.

**Prérequis côté utilisateur :**
- une **clé USB sacrifiée branchable sur l'USB-C** : une clé USB-C, ou un adaptateur OTG USB-A
  femelle → USB-C mâle pour la clé de la phase 2. Elle se prépare avec `tools/device/prepare_test_key.sh` ;
- avant toute écriture sur le vrai disque, **une sauvegarde complète de son contenu**, faite par
  l'utilisateur (§ 7 de la spec).

- [ ] **Step 1 : installer** le build release, avec la capture `FluLuksPerf` comme en phase 2.
- [ ] **Step 2 : grille sur la clé sacrifiée**, notée par l'utilisateur :
  1. Déverrouiller : le volume est en lecture et écriture.
  2. Depuis « Fichiers », copier une vidéo d'au moins 500 Mo du téléphone vers la clé. Noter la durée,
     puis la lire dans VLC.
  3. Créer un dossier, renommer, déplacer, copier, puis supprimer un fichier et un dossier.
  4. Enregistrer une photo depuis une autre application (« Enregistrer sous » ou partage) sur la clé.
  5. Éjecter depuis la notification. Puis, sur Ubuntu :
     `sudo cryptsetup open --readonly /dev/sdX1 essai && sudo e2fsck -fn /dev/mapper/essai` → propre.
     Les fichiers appartiennent à l'utilisateur (`ls -ln`), avec les bonnes dates.
  6. Débrancher la clé **pendant** une copie : pas de plantage, et la notification d'avertissement
     s'affiche. Rebrancher sur le téléphone → lecture seule avec « journal à rejouer ». Sur Ubuntu,
     ouvrir puis monter la clé (le noyau rejoue le journal), puis `e2fsck -fn` → propre.
  7. Interrupteur « lecture seule » actif → le volume s'ouvre avec « ouvert en lecture seule à votre
     demande ».
- [ ] **Step 3 : vrai disque**, seulement si la grille est entièrement réussie **et** la sauvegarde
  faite. L'utilisateur crée un dossier `FluLuks-essai`, y copie quelques fichiers, en renomme et en
  supprime un, puis éjecte. Sur Ubuntu, `e2fsck -fn` sur le mapping ouvert en lecture seule → propre,
  et le dossier est visible avec le bon propriétaire.
- [ ] **Step 4 : consigner** les durées, les débits et les anomalies dans la feuille de route, puis
  cocher T13. Commit — `docs(suivi): essais de la phase 3 sur le Pixel 4 XL`.
- [ ] **Step 5 : clôture git flow.** Fusionner `feature/ext4-ecriture` dans `develop` (`--no-ff`),
  créer `release/0.2.0` (`version: 0.2.0+2`), fusionner dans `main`, poser l'étiquette `v0.2.0`,
  refusionner dans `develop`, supprimer les branches, et cocher « Version v0.2.0 ».
