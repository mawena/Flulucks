# Portail — Phase 9 : compression, corbeille et outils du gestionnaire — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** compresser une sélection en ZIP et extraire ZIP, 7z, RAR (lecture), TAR, TAR.GZ et TAR.XZ d'un
stockage vers n'importe quel autre, avec progression et annulation ; plus la corbeille, l'analyse de
l'espace, les propriétés avec SHA-256, le renommage par lot et les filtres rapides de l'accueil. Fin de
phase : version **v1.2.0**.

**Architecture :**
- **Moteur natif**, nouveau module `native/src/archive/` et en-tête `include/fluluks/archive.h`, sur
  **libarchive 3.8.10** (BSD) et **liblzma de xz 5.8.4** (0BSD, décodeurs seulement), zlib du système (PC)
  ou du NDK :
  - lecteur : `fl_arc_reader` sur une entrée à accès aléatoire (`fl_arc_in` : `pread` + taille), modèle
    « tirer » : entrée suivante (nom brut, type, taille annoncée, date, chiffrée ou non), puis données par
    morceaux ;
  - écrivain ZIP : `fl_arc_writer` sur une sortie séquentielle (`fl_arc_out`), entrées poussées une à une
    (nom UTF-8 avec le drapeau UTF-8, deflate ou stockage, ZIP64 au besoin).
  - Aucun rappel JNI : l'entrée et la sortie sont soit un **descripteur** (téléphone, carte SD), soit un
    **fichier ouvert du moteur** (disque USB, lu et écrit par `fj_pread`/`fj_pwrite` sans repasser par
    Kotlin).
- **JNI :** `fj_arc_open_fd|_file`, `fj_arc_next`, `fj_arc_read`, `fj_arc_close` ; `fj_arcw_open_fd|_file`,
  `fj_arcw_add`, `fj_arcw_write`, `fj_arcw_close(commit)`. Nouveau type de poignée `T_ARC`, enfant de la
  poignée de fichier quand l'archive est sur un disque (une éjection la ferme).
- **Kotlin**, dossier `files/` :
  - `Archives` : tâches « compresser » et « extraire » dans le **même créneau unique que les copies**
    (`Transfers`), avec la même progression, l'annulation, les noms temporaires et la vérification de place ;
  - `ArchivePaths` : règles de sûreté des chemins d'archive (pures, testées sur JVM) ;
  - `Trash`, `SpaceAnalysis`, `Details` (propriétés, SHA-256), `BatchRename`, `QuickFilters` (MediaStore).
- **Flutter :** actions « Compresser », « Extraire ici », « Extraire vers… », « Propriétés »,
  « Renommer… » (lot) dans l'explorateur ; écran Corbeille, écran Analyse de l'espace ; filtres sur
  l'accueil.

**Tech Stack :** C11, libarchive 3.8.10, liblzma (xz 5.8.4), zlib ; Kotlin, `androidx.exifinterface` 1.4.2 ;
Flutter 3.41.4, Pigeon 27.3.0. Juges indépendants : `unzip -t`, `zip -T`, `zipfile`/`tarfile`/`lzma` de
Python, `tar`, `xz -t`, `unrar t` ; archives 7z et RAR de référence tirées de la suite de tests de
libarchive (faites par 7-Zip et WinRAR).

**Spec :** [docs/superpowers/specs/2026-10-17-fluluks-design.md](../specs/2026-10-17-fluluks-design.md)
(§ 4.2, § 11, et **§ 15 « compression et outils du gestionnaire »**).

## Global Constraints

- **Branche et commits.**
  - Branche `feature/compression`, créée depuis `feature/formatage` *(décision du planificateur : la phase 8
    attend la fin de sa dernière batterie ; elle sera fusionnée dans `develop` avant cette branche)*, dans
    le worktree `.superpowers/worktrees/phase9`.
  - Commits sous l'identité `mawena <gamligocharles@gmail.com>`, en français (Conventional Commits),
    **sans aucune attribution à Claude**, en nommant les fichiers (`git commit -- <fichiers>`).
  - Un commit par tâche au moins ; la tâche est cochée dans la feuille de route (section Phase 9) dans le
    même commit.
- **Bibliothèques tierces** : copiées dans `native/third_party/<nom>/` depuis l'archive amont, empreinte
  SHA-256 comparée à celle publiée (et signature vérifiée si la clé est disponible), seuls les fichiers
  utiles gardés (pas de tests ni d'outils) avec la licence ; versions et empreintes notées dans
  `native/third_party/PATCHES.md`. Construites par nos propres cibles CMake (comme FatFs et ntfs-3g), avec
  un `config.h` écrit à la main pour glibc et pour Bionic (API 26).
- **Sûreté.**
  - Une archive est **hostile** : rien n'est jamais écrit hors du dossier de destination ; liens symboliques,
    liens physiques et fichiers spéciaux ne sont pas extraits (comptés et signalés, comme pour les copies) ;
    une entrée chiffrée est refusée (« Archive protégée par mot de passe : non prise en charge »).
  - Place : avant d'extraire, la somme des tailles annoncées doit tenir dans la place libre moins une
    **réserve de min(256 Mio, 5 % du stockage)** ; pendant l'extraction, l'opération s'arrête dès que ce
    budget est dépassé (tailles mensongères, flux sans taille annoncée).
  - Une seule opération longue à la fois (copie, déplacement, compression, extraction).
  - Les tests natifs n'écrivent que dans le dossier de construction ; sur le téléphone, seulement sous
    `/data/local/tmp/fluluks/` et dans le cache de l'application.
- **Valeurs fixées** *(décisions du planificateur)* :
  - archives reconnues à l'extension (sans tenir compte de la casse) : `.zip`, `.7z`, `.rar`, `.tar`,
    `.tar.gz`, `.tgz`, `.tar.xz`, `.txz`, et `.gz`, `.xz` seuls (un fichier) ;
  - noms d'entrée : UTF-8 s'il est valide, sinon CP437 (ZIP) ou ISO-8859-1 (autres), puis assainis comme
    pour une copie vers FAT ou NTFS si la destination l'exige ;
  - ZIP écrit : deflate niveau 6 ; **stockage sans compression** pour `jpg jpeg png gif webp heic mp4 mkv
    mov avi mp3 m4a ogg opus flac zip 7z rar gz xz bz2 apk` ; dates en heure locale et en UTC (champ
    étendu) ; nom par défaut : celui de l'élément seul, sinon « Archive », plus `.zip`, nom unique ;
  - « Extraire ici » crée un dossier au nom de l'archive sans son extension (nom unique) ;
  - corbeille : dossier `.Portail-corbeille` à la racine de chaque stockage, disposition freedesktop
    (`files/` et `info/<nom>.trashinfo` avec `Path=` et `DeletionDate=`), purge des éléments de plus de
    30 jours à l'ouverture de la corbeille ou à chaque suppression ; les suppressions venues d'autres
    applications (DocumentsProvider) restent définitives ;
  - analyse de l'espace : 100 plus gros fichiers, dossiers triés par taille, mêmes bornes que la recherche ;
  - renommage par lot : numérotation (`<base> (<n>)`, n sur au moins 2 chiffres, départ 1), remplacement
    de texte (sensible à la casse au choix), date de prise de vue `AAAA-MM-JJ HH.MM.SS` (EXIF
    `DateTimeOriginal`, sinon date de modification) ; extension toujours gardée ;
  - filtres rapides : téléphone et carte SD par MediaStore (index d'Android), 500 résultats au plus, triés
    du plus récent ; « Récents » : modifiés depuis 30 jours. Les disques USB n'y figurent pas (non
    indexés ; la recherche de l'explorateur les couvre).
- **Langue :** interface et messages en français.

## Review Focus

1. **Archive hostile** (`../x`, `/etc/x`, `a/../../x`, lien symbolique puis fichier à travers lui, noms avec
   NUL ou `\`, doublons, profondeur 1 000). Attendu : rien hors du dossier de destination ; les entrées
   refusées sont comptées et signalées. *Tests : T2 `test_arc_noms_bruts`, T5 `hostileEntriesSkipped`,
   `symlinkThenFileThroughIt`.*
2. **Bombe de décompression, tailles mensongères, flux `.gz` sans taille.** Attendu : arrêt avec « Pas
   assez de place » ou « Archive incohérente », fichier en cours supprimé, stockage jamais rempli au-delà
   de la réserve. *Tests : T5 `bombStopsAtBudget`, `lyingSizeStops`.*
3. **Noms non UTF-8** (ZIP de Windows en CP437) et **noms accentués écrits** (lisibles par Windows et
   Python grâce au drapeau UTF-8). *Tests : T2 `test_arc_zip_cp437`, T3 `test_arcw_noms_utf8`, T5
   `cp437NamesDecoded`.*
4. **Stockage fermé ou disque arraché pendant une compression ou une extraction.** Attendu : « Stockage
   fermé : opération interrompue », fichier partiel supprimé, aucune poignée native perdue. *Tests : T4
   `test_arc_disque_ferme`, T5 `sourceGoneDuringExtract`.*
5. **Corbeille : restaurer quand le dossier d'origine a disparu ou que le nom est pris ; supprimer dans la
   corbeille ; stockage en lecture seule.** Attendu : dossiers d'origine recréés, nom unique, suppression
   définitive dans la corbeille, refus clair en lecture seule. *Tests : T7 `restoreRecreatesParent`,
   `restoreUniqueName`, `deleteInsideTrashIsPermanent`, `readOnlySourceRefused`.*

---

### Task 1 : libarchive et liblzma dans le moteur

**Files :** Create `native/third_party/libarchive/` (dossier `libarchive/` amont et `COPYING`),
`native/third_party/xz/` (`src/liblzma/` utile, `COPYING*`), `native/cmake/libarchive/config.h`,
`native/cmake/xz/config.h`, `native/tests/test_arc_fumee.c` ; Modify `native/cmake/third_party.cmake`,
`native/CMakeLists.txt`, `native/third_party/PATCHES.md`.

**Interfaces :** Produces : cibles CMake `fl_lzma` (décodeurs LZMA1, LZMA2, delta, BCJ ; contrôles CRC32,
CRC64, SHA-256) et `fl_libarchive` (lecteurs zip, 7zip, rar, rar5, tar, raw ; filtres gzip, xz, lzma ;
écrivain zip), liées à `fluluks`.

- [x] **Step 1 :** télécharger `libarchive-3.8.10.tar.xz` et `xz-5.8.4.tar.xz` depuis leurs dépôts GitHub
  officiels ; comparer l'empreinte SHA-256 à celle publiée (champ `digest` de l'API GitHub) ; vérifier
  la signature `.sig`/`.asc` si la clé publique s'obtient ; noter versions et empreintes dans `PATCHES.md`.
  *Jamais xz 5.6.0 ni 5.6.1.*
- [x] **Step 2 : test de fumée** `test_arc_fumee` : lit avec l'API libarchive brute un `tar.xz` et un `zip`
  produits par `tar -cJf` et `zip` (fixtures ajoutées à `tools/fixtures/make_fixtures.sh`) et compare les
  noms et contenus. Run `ctest --test-dir build/native -R test_arc_fumee` → FAIL (cible absente).
- [x] **Step 3 :** cibles CMake et `config.h` ; options `-Werror` levées pour ces seules cibles (code
  amont), ASan/UBSan gardés ; construction NDK arm64 et x86_64 (`flutter build apk --debug`).
- [x] **Step 4 :** test vert ; suite rapide `ctest -LE lent` verte.
- [x] **Step 5 :** commit `build(moteur): libarchive 3.8.10 et liblzma 5.8.4`.

### Task 2 : lecture d'archives (moteur)

**Files :** Create `native/include/fluluks/archive.h`, `native/src/archive/reader.c`,
`native/tests/test_archive_read.c` ; fixtures 7z et RAR copiées (décodées des `.uu`) de
`libarchive/test/` vers `native/tests/data/archives/` avec leur licence.

**Interfaces :** Produces :
```c
typedef struct { void *ctx; int64_t size; int64_t (*pread)(void *ctx, void *buf, size_t len, uint64_t off); } fl_arc_in;
typedef enum { FL_ARC_FILE = 1, FL_ARC_DIR, FL_ARC_SYMLINK, FL_ARC_HARDLINK, FL_ARC_OTHER } fl_arc_type;
typedef struct { char name[4096]; size_t name_len; fl_arc_type type; int64_t size; int64_t mtime; bool encrypted; } fl_arc_entry;
int fl_arc_open(const fl_arc_in *in, fl_arc_reader **out);   /* FL_ENOTSUP : format inconnu */
int fl_arc_next(fl_arc_reader *r, fl_arc_entry *e);          /* 1 entrée, 0 fin, < 0 erreur */
int64_t fl_arc_read(fl_arc_reader *r, void *buf, size_t len); /* octets, 0 fin de l'entrée, < 0 erreur */
const char *fl_arc_format(const fl_arc_reader *r);           /* « ZIP », « 7-Zip », « RAR5 »… */
void fl_arc_close(fl_arc_reader *r);
```
`name` : octets bruts de l'archive (non convertis, non assainis) ; `size` : -1 si inconnue.

- [x] **Step 1 : tests** (`test_archive_read`) :
  - `test_arc_formats` : zip (deflate et stocké), tar, tar.gz, tar.xz, `.gz` seul, 7z (LZMA2, BCJ), RAR4,
    RAR5 : noms, types, tailles et contenus égaux à la référence (contenus des fixtures listés dans le test) ;
  - `test_arc_noms_bruts` : zip fabriqué par Python avec `../x`, `/abs`, `a/../../x`, nom avec `\` : rendus
    tels quels (la sûreté est en Kotlin) ;
  - `test_arc_zip_cp437` : nom `é` en CP437 sans drapeau UTF-8 → octet `0x82` rendu brut ;
  - `test_arc_chiffre` : `zip -P` → `encrypted` vrai, `fl_arc_read` < 0 ;
  - `test_arc_tronque` : archive coupée à moitié → erreur, jamais de plantage (ASan) ;
  - `test_arc_inconnu` : fichier texte → `FL_ENOTSUP`.
- [x] **Step 2 :** Run `ctest -R test_archive_read` → FAIL (en-tête absent).
- [x] **Step 3 :** implémenter sur `archive_read_new` + `archive_read_support_format_{zip_seekable,7zip,rar,
  rar5,tar,raw}` + filtres ; callbacks `read`/`seek`/`skip` sur `fl_arc_in` (tampon de 64 Kio) ; le format
  `raw` seulement si un filtre gzip/xz est actif (sinon tout fichier serait une « archive ») ; noms par
  `archive_entry_pathname` sans conversion de jeu de caractères (option `hdrcharset` non utilisée).
- [x] **Step 4 :** tests verts ; suite rapide verte.
- [x] **Step 5 :** commit `feat(archives): lecture de ZIP, 7z, RAR, TAR, GZ et XZ`.

### Task 3 : écriture ZIP (moteur)

**Files :** Create `native/src/archive/writer.c`, `native/tests/test_archive_write.c` ; Modify `archive.h`.

**Interfaces :** Produces :
```c
typedef struct { void *ctx; int (*write)(void *ctx, const void *buf, size_t len); } fl_arc_out;
int fl_arcw_open(const fl_arc_out *out, fl_arc_writer **w);
int fl_arcw_add(fl_arc_writer *w, const char *name_utf8, fl_arc_type type, int64_t size, int64_t mtime, bool store);
int fl_arcw_write(fl_arc_writer *w, const void *buf, size_t len);
int fl_arcw_close(fl_arc_writer *w, bool commit);  /* commit faux : abandon, rien de plus n'est écrit */
```

- [x] **Step 1 : tests** jugés par `unzip -t`, `zip -T` et `zipfile` de Python :
  - `test_arcw_arbre` : dossiers, fichiers vides, 3 Mio aléatoires, stocké et deflate ; relu octet par octet ;
  - `test_arcw_noms_utf8` : `été/日本.txt` → drapeau UTF-8 posé, `zipfile` rend le même nom ;
  - `test_arcw_dates` : date relue par `zipfile` égale (à 2 s près, résolution DOS) ;
  - `test_arcw_zip64_lent` (label `lent`) : fichier creux de 4,5 Gio → ZIP64, `unzip -t` propre ;
  - `test_arcw_abandon` : `commit` faux → l'appelant supprime le fichier, aucune fuite (ASan).
- [x] **Step 2 :** Run → FAIL.
- [x] **Step 3 :** `archive_write_set_format_zip`, `archive_write_zip_set_compression_deflate|store` avant
  chaque entrée, `archive_entry_set_pathname_utf8`, option `zip:zip64` automatique ; `fl_arcw_add` refuse
  un nom vide, absolu ou contenant `..` (`FL_EINVAL`).
- [x] **Step 4 :** tests verts ; suite rapide verte.
- [x] **Step 5 :** commit `feat(archives): écriture ZIP`.

### Task 4 : JNI et entrées-sorties des sources

**Files :** Modify `native/jni/handles.c/.h`, `native/jni/fluluks_jni.c`, `native/tests/test_handles.c`,
`engine/Engine.kt`, `engine/NativeEngine.kt`, `files/FileSource.kt`, `files/LocalSource.kt`,
`files/DiskSource.kt`.

**Interfaces :** Produces :
- C : `int64_t fj_arc_open_fd(int fd)`, `int64_t fj_arc_open_file(int64_t file)`,
  `int fj_arc_next(int64_t h, fl_json *out)` (`{"name": base64 des octets bruts, "type", "size", "mtime",
  "encrypted"}`, `FL_OK` sans objet à la fin), `int64_t fj_arc_read(int64_t h, void *buf, size_t len)`,
  `int fj_arc_close(int64_t h)` ; `int64_t fj_arcw_open_fd(int fd)`, `int64_t fj_arcw_open_file(int64_t
  file)`, `int fj_arcw_add(int64_t h, const char *json)`, `int fj_arcw_write(int64_t h, const void *buf,
  size_t len)`, `int fj_arcw_close(int64_t h, bool commit)`. Le descripteur appartient à la poignée (fermé
  par `close`).
- Kotlin : `Engine.arcOpenFd(fd: Int): Long`, `arcOpenFile(file: Long): Long`, `arcNext(h: Long): ByteArray?`,
  `arcRead(h, buf, len): Int`, `arcClose(h): Int`, `arcwOpenFd`, `arcwOpenFile`, `arcwAdd(h, json: ByteArray)`,
  `arcwWrite(h, buf, len)`, `arcwClose(h, commit: Boolean)` (défauts `FL_ENOTSUP`).
- `FileSource.openNative(path: String): NativeIn` et `createNative(parent, name): Pair<String, NativeOut>`
  avec `sealed class NativeIn { Fd(pfd), EngineFile(handle) }` (idem `NativeOut`), fermés par l'archive.

- [x] **Step 1 : tests** `test_handles` : `test_arc_par_jni` (zip sur une image de disque lu par
  `fj_arc_open_file`, puis écrit par `fj_arcw_open_file`, relu) ; `test_arc_disque_ferme` (archive
  ouverte, `fj_close_disk` → la poignée d'archive est fermée, `fj_arc_read` rend `FL_EINVAL`,
  `fj_debug_used() == 0`). JVM : `LocalSourceTest.openNativeReadsSameBytes`.
- [x] **Step 2 :** Run → FAIL.
- [x] **Step 3 :** type `T_ARC` (parent : la poignée de fichier, ou aucun pour un descripteur) ; entrée
  `fl_arc_in` sur `pread(2)` ou sur `fj_pread` ; sortie sur `write(2)` ou `fj_pwrite` (position
  tenue par l'archive) ; `fj_close_disk` ferme les archives de ses fichiers.
- [x] **Step 4 :** tests verts ; construction NDK ; JVM verte.
- [x] **Step 5 :** commit `feat(archives): poignées d'archive par JNI`.

### Task 5 : compresser et extraire (Kotlin)

**Files :** Create `files/Archives.kt`, `files/ArchivePaths.kt`, tests `ArchivesTest.kt`,
`ArchivePathsTest.kt`, `androidTest/ArchivesInstrumentedTest.kt` ; Modify `files/Transfers.kt`.

**Interfaces :**
- Consumes : Task 4.
- Produces :
  - `TransferState.kind: String` (`copy`, `move`, `compress`, `extract`) ; `Transfers` expose le créneau
    unique à `Archives` (`internal fun launch(kind, srcId, dstId, body: (JobContext) -> Unit)`) ;
  - `Archives.isArchive(name): Boolean`, `Archives.startCompress(srcId, paths, dstDir, name)`,
    `Archives.startExtract(srcId, archive, dstId, dstDir, intoNewFolder: Boolean)` ;
  - `ArchivePaths.decodeName(raw: ByteArray, zip: Boolean): String`, `ArchivePaths.safeRelative(name):
    String?` (null : entrée refusée).

- [x] **Step 1 : tests JVM** (moteur d'archives simulé qui rend des entrées choisies) :
  - `safeRelativeRules` : `a/b` gardé ; `../x`, `/x`, `a/../../x`, `a/./b` → normalisé ou refusé, `\` traité
    comme un caractère, NUL refusé, profondeur > `WriteOps.MAX_DEPTH` refusée ;
  - `cp437NamesDecoded` : `0x82` → « é » ;
  - `hostileEntriesSkipped` : rien n'est créé hors de la destination, compteur des entrées refusées
    reporté dans `TransferState.error` (« N élément(s) de l'archive ignoré(s) ») ;
  - `symlinkThenFileThroughIt` : lien `l → /` puis `l/x` : le lien n'est pas créé, `l/x` est créé comme un
    vrai dossier `l` sous la destination ;
  - `bombStopsAtBudget`, `lyingSizeStops` : arrêt, fichier en cours supprimé ;
  - `encryptedRefused`, `cancelRemovesPartial`, `sourceGoneDuringExtract`, `extractIntoNewFolderUnique`,
    `compressStoresMediaDeflatesText`, `compressNameUnique`, `oneOperationAtATime` (une copie en cours →
    `MSG_BUSY`).
- [x] **Step 2 :** Run `./gradlew testDebugUnitTest --tests '*Archive*'` → FAIL.
- [x] **Step 3 :** implémenter. Extraction : dossiers créés à la demande (`mkdir` par segment, sous la
  destination seulement), fichier écrit sous un nom temporaire puis renommé (mêmes règles que
  `Transfers.copyFile`) ; doublon d'entrée → nom unique. Compression : parcours borné de la sélection
  (comme `Transfers.scan`), fichier ZIP écrit sous un nom temporaire, renommé à la fin, supprimé en cas
  d'échec ou d'annulation ; l'archive en cours d'écriture n'est jamais ajoutée à elle-même.
- [x] **Step 4 :** tests JVM verts ; test instrumenté (compresser un dossier d'une image de disque,
  extraire sur une autre image, comparer ; extraire les fixtures 7z et RAR5) vert sur le Pixel.
- [x] **Step 5 :** commit `feat(archives): compresser et extraire d'un stockage à l'autre`.

### Task 6 : pont et écrans des archives

**Files :** Modify `pigeons/fluluks_api.dart` (+ sorties générées), `bridge/HostApiImpl.kt`,
`lib/src/state/app_state.dart`, `lib/src/state/host_api.dart`, `lib/src/screens/explorer_screen.dart`,
`lib/src/widgets/clipboard_bar.dart`, `lib/src/widgets/transfer_bar.dart`, `lib/src/strings_fr.dart`,
`test/fake_host_api.dart` ; Create `test/archives_test.dart`.

**Interfaces :** Pigeon : `TransferMsg.kind` ; `void startCompress(String srcId, List<String> paths,
String dstDir, String name)` ; `void startExtract(String srcId, String archive, String dstId, String dstDir,
bool intoNewFolder)`.

- [x] **Step 1 : tests Flutter** : `compressAskNameThenStarts` (nom proposé, `.zip` ajouté) ;
  `tapArchiveShowsExtractSheet` (« Extraire ici », « Extraire vers… », « Ouvrir avec ») ;
  `extractToPicksDestinationWithClipboardBar` (la barre propose « Extraire ici » dans un autre dossier) ;
  `transferBarShowsKind` (« Compression : … », « Extraction : … »).
- [x] **Step 2 :** Run `flutter test test/archives_test.dart` → FAIL.
- [x] **Step 3 :** implémenter (action « Compresser » dans la barre de sélection, feuille d'actions au tap
  d'une archive, extraction en attente portée par `AppState` comme le presse-papiers).
- [x] **Step 4 :** tests verts ; `flutter analyze` propre ; JVM verte.
- [x] **Step 5 :** commit `feat(archives): compresser et extraire depuis l'explorateur`.

### Task 7 : corbeille

**Files :** Create `files/Trash.kt`, `TrashTest.kt`, `lib/src/screens/trash_screen.dart`,
`test/trash_test.dart` ; Modify Pigeon, `HostApiImpl.kt`, `app_state.dart`, `explorer_screen.dart`,
`strings_fr.dart`.

**Interfaces :** `Trash.moveToTrash(src: FileSource, paths)`, `Trash.list(src): List<TrashItem>`,
`Trash.restore(src, ids)`, `Trash.purge(src, ids)`, `Trash.empty(src)`, `Trash.purgeExpired(src, nowMs)` ;
Pigeon : `deleteEntries(sourceId, paths, permanent: bool)`, `listTrash(sourceId) → List<TrashItemMsg>`
(`id, name, originalPath, deletedAt, size, isDir`), `restoreTrash(sourceId, ids)`, `purgeTrash(sourceId,
ids)`, `emptyTrash(sourceId)`.

- [x] **Step 1 : tests JVM** (`LocalSource` sur un dossier temporaire) : `deleteMovesToTrash`,
  `trashInfoFormat` (`[Trash Info]`, `Path=` encodé en URL, `DeletionDate=AAAA-MM-JJTHH:MM:SS`),
  `restoreRecreatesParent`, `restoreUniqueName`, `purgeExpiredAfter30Days`, `deleteInsideTrashIsPermanent`,
  `readOnlySourceRefused`, `trashHiddenFromSearch`, `sameNameTwiceKeepsBoth`. Flutter :
  `deleteDialogOffersPermanent`, `trashScreenRestoreAndEmpty`.
- [x] **Step 2 :** Run → FAIL.
- [x] **Step 3 :** implémenter ; déplacer dans la corbeille = `move` dans la même source (sans copie) ;
  la taille de la corbeille s'affiche dans son écran ; accès par le menu de l'explorateur (racine) et
  depuis la carte du stockage.
- [x] **Step 4 :** tests verts.
- [x] **Step 5 :** commit `feat(corbeille): suppression récupérable 30 jours`.

### Task 8 : analyse de l'espace

**Files :** Create `files/SpaceAnalysis.kt`, `SpaceAnalysisTest.kt`, `lib/src/screens/space_screen.dart`,
`test/space_test.dart` ; Modify Pigeon, `HostApiImpl.kt`, `app_state.dart`, `strings_fr.dart`.

**Interfaces :** `SpaceAnalysis.run(sourceId, path): SpaceReport` (une analyse à la fois, annulable,
résultat gardé en mémoire pour descendre dans les dossiers sans relire) ; Pigeon `analyzeSpace(sourceId,
path) → SpaceReportMsg` (`path, totalBytes, fileCount, children: List<SpaceItemMsg>, biggest:
List<EntryMsg>, truncated`), `spaceChildren(sourceId, path)`, `cancelAnalysis()`.

- [x] **Step 1 : tests** : `sizesSumUp`, `biggestFilesTop100`, `drillDownUsesCache`, `unreadableDirSkipped`,
  `cancelStops` ; Flutter `spaceScreenShowsBarsAndDrillsDown`.
- [x] **Step 2 → 4 :** FAIL, implémenter (barres proportionnelles, pourcentage), verts.
- [x] **Step 5 :** commit `feat(gestionnaire): analyse de l'espace`.

### Task 9 : propriétés et SHA-256

**Files :** Modify `native/jni/handles.c` (stat : `mode`, `uid`, `gid`), `engine/EngineModels.kt`,
`FileSource.kt` (`details(path): Details?`), `LocalSource.kt` (`Os.lstat`), `DiskSource.kt` ; Create
`files/Details.kt`, `DetailsTest.kt`, `lib/src/widgets/properties_sheet.dart`, `test/properties_test.dart`.

**Interfaces :** Pigeon `details(sourceId, path) → DetailsMsg` (`name, path, type, size, mtime, mode?, uid?,
gid?, fs`), `folderSize(sourceId, path) → FolderSizeMsg` (annulable), `sha256(sourceId, path) → String`
(annulable, progression par `onHashProgress(int? pct)`), `cancelHash()`.

- [x] **Step 1 : tests** : `test_handles` `test_stat_droits` (mode 0644, uid du parent) ;
  JVM `sha256KnownVector` (« abc »), `sha256Cancel`, `modeString` (`0755` → `rwxr-xr-x`) ; Flutter
  `propertiesShowOwnerOnExt4`, `hashButtonShowsProgressThenDigest`.
- [x] **Step 2 → 4 :** FAIL, implémenter, verts.
- [x] **Step 5 :** commit `feat(gestionnaire): propriétés et empreinte SHA-256`.

### Task 10 : renommage par lot

**Files :** Create `files/BatchRename.kt`, `BatchRenameTest.kt`, `lib/src/screens/batch_rename_screen.dart`,
`test/batch_rename_test.dart` ; Modify `android/app/build.gradle.kts` (`androidx.exifinterface:exifinterface:
1.4.2`), Pigeon, `HostApiImpl.kt`, `explorer_screen.dart`, `strings_fr.dart`.

**Interfaces :** `RenameRule` (`mode`: `number|replace|photoDate`, `base`, `start`, `find`, `replace`,
`caseSensitive`) ; `BatchRename.preview(src, paths, rule): List<RenamePreview>` (`path, newName,
conflict: String?`) ; `BatchRename.apply(src, paths, rule)` ; Pigeon `previewRename`, `applyRename`.

- [x] **Step 1 : tests** : `numberingKeepsExtension`, `replaceCaseInsensitive`, `photoDateFromExif`
  (JPEG de test avec `DateTimeOriginal`), `photoDateFallsBackToMtime`, `conflictWithExistingRefusedBeforeAnyRename`,
  `duplicatesInBatchFlagged`, `swapNamesWorks` (a→b, b→a, en deux temps par des noms temporaires),
  `invalidNameForFatRefused` ; Flutter `previewThenApply`.
- [x] **Step 2 → 4 :** FAIL, implémenter, verts.
- [x] **Step 5 :** commit `feat(gestionnaire): renommage par lot`.

### Task 11 : filtres rapides de l'accueil

**Files :** Create `files/QuickFilters.kt`, `QuickFiltersTest.kt`, `lib/src/screens/filter_screen.dart`,
`test/filters_test.dart`, `androidTest/QuickFiltersInstrumentedTest.kt` ; Modify Pigeon, `HostApiImpl.kt`,
`home_screen.dart`, `strings_fr.dart`.

**Interfaces :** `QuickFilters.kind(name, mime): Set<String>` (pure) ; `QuickFilters.query(filter): List<Entry
with sourceId>` sur `MediaStore.Files` (volumes externes) ; Pigeon `quickFilter(String filter) →
List<FilterItemMsg>` (`sourceId, path, name, size, mtime`).

- [x] **Step 1 : tests** : `classifyByExtensionAndMime` (photos, vidéos, musique, documents `pdf doc docx odt
  xls xlsx ods ppt pptx odp txt md epub`, archives = `Archives.isArchive`) ; `recentIs30Days` ;
  instrumenté : un fichier créé dans `Download/portail-tests/` apparaît dans « Récents » et « Documents » ;
  Flutter `homeShowsFilterChips`, `filterScreenOpensFile`.
- [x] **Step 2 → 4 :** FAIL, implémenter (chemin MediaStore converti en `sourceId` + chemin de la source :
  téléphone ou carte SD), verts.
- [x] **Step 5 :** commit `feat(gestionnaire): filtres rapides sur l'accueil`.

### Task 12 *(utilisateur)* : essais

**Files :** Create `docs/superpowers/research/2026-10-22-essais-phase9/essais.md`.

- [ ] Fiche d'essais : archives réelles (ZIP de Windows, 7z, RAR5, tar.xz) extraites du téléphone vers un
  disque et inversement ; ZIP fait par Portail ouvert sous Windows et Ubuntu ; corbeille (supprimer,
  restaurer, vider) ; analyse de l'espace du téléphone ; propriétés et SHA-256 comparé à `sha256sum` ;
  renommage de photos par date ; filtres de l'accueil. APK installée avant l'essai.

### Task 13 : documentation et v1.2.0

- [x] README (archives, corbeille, outils, limites : archives chiffrées non prises en charge, RAR en lecture
  seule, filtres sans les disques USB), feuille de route cochée ; commit `docs: compression et outils du
  gestionnaire`.
- [ ] Relecture finale de toute la branche, puis fusion `feature/compression` → `develop` (après la
  phase 8), `release/1.2.0` (pubspec `1.2.0+9`), `main`, étiquette signée `v1.2.0`. Pas de push sans
  l'accord de l'utilisateur.
