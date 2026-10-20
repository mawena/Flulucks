# Portail — Phase 6 : nom, logo et gestionnaire de fichiers — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** FluLuks devient **Portail**, avec un nouveau logo (dossier-bouclier). C'est un gestionnaire de
fichiers à part entière :
- l'écran d'accueil montre le stockage du téléphone, la carte SD, les disques USB (chiffrés ou non) et
  les favoris ;
- on parcourt chacun en liste ou en grille, avec vignettes des photos et vidéos, tri et fichiers
  cachés ;
- on cherche par nom ;
- on ouvre avec une autre application, on partage, on crée, on renomme, on supprime ;
- on copie et on déplace d'un stockage à l'autre, avec une progression qu'on peut annuler.

Les disques USB gardent toutes leurs règles de sûreté. Fin de phase : version **v0.5.0**.

**Architecture :**
- **Moteur natif inchangé.**
- **Kotlin, paquet `files/` :**
  - une interface `FileSource` et deux implémentations :
    - `LocalSource` : `java.io.File`, pour le stockage interne et la carte SD, avec l'accès à tous les
      fichiers ;
    - `DiskSource` : `Engine` et `WriteOps` sur un volume ouvert, sous le verrou d'écriture du
      DocumentsProvider, déplacé dans `VolumeManager`.
  - Au-dessus de `FileSource` :
    - `Sources` : la liste des stockages ;
    - `Transfers` : copier et déplacer entre deux sources quelconques ;
    - `Search` : recherche par nom ;
    - `Thumbnails` : vignettes, gardées **en mémoire seulement** ;
    - `ViewPrefs` : tri, affichage et favoris.
- **Flutter :**
  - `HomeScreen` remplace `DisksScreen` comme écran d'accueil ; ses cartes de disques sont réutilisées ;
  - `ExplorerScreen`, `SearchScreen`, `SettingsScreen` ;
  - tout passe par Pigeon, et l'état reste dans `AppState`.
- **Nom visible seulement :** le paquet `com.mawena.fluluks`, l'autorité du provider, la bibliothèque
  native et le dépôt ne changent pas. Changer d'`applicationId` créerait une autre application : il
  faudrait désinstaller, et les réglages seraient perdus.

**Tech Stack :** Kotlin et coroutines, `MANAGE_EXTERNAL_STORAGE` (API 30 et plus) ou
`READ/WRITE_EXTERNAL_STORAGE` (API 26 à 29), `StorageManager`, `FileProvider` (androidx.core 1.15),
`BitmapFactory` et `MediaMetadataRetriever`, Flutter 3.41.4 (Material 3), Pigeon 27.3.0. Tests : JUnit
sur la JVM, tests instrumentés sur le Pixel, `flutter test`. Le logo est rendu par librsvg (PyGObject
`Rsvg`), sur le PC.

**Spec :** [docs/superpowers/specs/2026-10-17-fluluks-design.md](../specs/2026-10-17-fluluks-design.md)
(§ 2, § 4.2, § 4.3, § 4.4, et **§ 11 « Évolution du 2026-10-20 »**, ajouté avec ce plan).

## Global Constraints

- **Branche et commits.**
  - Branche `feature/portail`, créée depuis `develop`.
  - Commits sous l'identité git de l'utilisateur, en français (Conventional Commits), **sans aucune
    attribution à Claude**.
  - Un commit par tâche, plus les commits de correction. La tâche est cochée dans la feuille de route
    (section Phase 6) **dans le même commit**.
- **Disques USB : aucune règle du § 4.2 relâchée.**
  - Toute écriture sur un disque passe par `DiskSource`, donc par `WriteOps` ou `Engine`, sous
    `VolumeManager.writeLock`.
  - Elle est suivie de `VolumeManager.noteWrite(volumeId)` et `VolumeManager.notifyChanged(volumeId,
    parents…)`, comme pour le provider.
- **Confidentialité.**
  - Les vignettes et les résultats de recherche d'un disque ne sont **jamais écrits** sur le stockage
    du téléphone : cache en mémoire seulement, vidé à l'éjection du volume.
  - Aucun chemin ni nom de fichier dans les journaux (`Log`) : classe de l'exception seulement.
  - La clé et la phrase de passe ne sont touchées par rien de nouveau.
- **Entrées hostiles.**
  - Chemins venus de Dart, validés par `PathRules.check(path)`, nouvelle fonction qui reprend les règles
    de `DocumentIds.decode` sur le chemin seul. Un chemin refusé donne `FlutterError` « Document
    introuvable ou illisible ».
  - `LocalSource` refuse tout chemin dont le chemin canonique sort de sa racine.
  - Noms écrits sur un disque depuis une autre source : validés par `DocNames.isValid`, sinon remplacés
    par `DocNames.sanitize` (caractères `/ \ : * ? " < > |` et de contrôle remplacés par « _ », point et
    espaces finaux retirés, 255 octets UTF-8 au plus en gardant l'extension, `fichier` si vide). Ils
    sont ensuite rendus uniques par `DocNames.unique`.
- **Identifiants de sources** :
  - `phone` : stockage interne, `Environment.getExternalStorageDirectory()` ;
  - `sd:<uuid>` : carte SD, chaque `StorageVolume` amovible monté ;
  - l'identifiant du volume ouvert pour un disque, `VolumeState.id`.

  Les favoris sont rangés par **clé stable** : `phone`, `sd:<uuid>`, ou `disk:<uuid de la partition>`
  (`PartitionInfo.uuid`). Ils sont résolus vers la source ouverte du moment.
- **Interface en français, textes exacts** *(décision du planificateur)*. Chaînes Dart dans
  `lib/src/strings_fr.dart`, chaînes Kotlin en constantes :

  | Clé | Texte |
  |---|---|
  | `Fr.title` | `Portail` |
  | `Fr.phone` | `Téléphone` |
  | `Fr.sdCard` | `Carte SD` |
  | `Fr.usbDisks` | `Disques USB` |
  | `Fr.favorites` | `Favoris` |
  | `Fr.freeOf(free, total)` | `$free libres sur $total` (tailles par `Fr.humanSize`) |
  | `Fr.allFilesTitle` | `Accès aux fichiers du téléphone` |
  | `Fr.allFilesBody` | `Autorisez Portail à gérer tous les fichiers pour parcourir le téléphone et la carte SD.` |
  | `Fr.allFilesButton` | `Autoriser` |
  | `Fr.browse` | `Parcourir` |
  | `Fr.settings` | `Réglages` |
  | `Fr.readOnlyHelp` | `S'applique aux prochaines ouvertures de disques` |
  | `Fr.newFolder` · `Fr.folderName` | `Nouveau dossier` · `Nom du dossier` |
  | `Fr.rename` · `Fr.newName` | `Renommer` · `Nouveau nom` |
  | `Fr.delete` | `Supprimer` |
  | `Fr.deleteOne(name)` · `Fr.deleteMany(n)` | `Supprimer « $name » ?` · `Supprimer $n éléments ?` |
  | `Fr.deleteBody` | `La suppression est définitive.` |
  | `Fr.copy` · `Fr.cut` · `Fr.pasteHere` | `Copier` · `Couper` · `Coller ici` |
  | `Fr.clipboard(n, move)` | `$n élément(s) à déplacer`, ou `… à copier` |
  | `Fr.share` · `Fr.openWith` | `Partager` · `Ouvrir avec…` |
  | `Fr.emptyFolder` | `Dossier vide` |
  | `Fr.protectedFolder` | `Dossier protégé par Android` |
  | `Fr.volumeClosed` | `Le disque a été fermé` |
  | `Fr.selected(n)` | `$n sélectionné(s)` |
  | `Fr.sortBy` · `Fr.sortName` · `Fr.sortSize` · `Fr.sortDate` | `Trier par` · `Nom` · `Taille` · `Date` |
  | `Fr.ascending` · `Fr.descending` | `Croissant` · `Décroissant` |
  | `Fr.grid` · `Fr.list` | `Grille` · `Liste` |
  | `Fr.showHidden` | `Afficher les fichiers cachés` |
  | `Fr.addFavorite` · `Fr.removeFavorite` | `Ajouter aux favoris` · `Retirer des favoris` |
  | `Fr.favoriteUnavailable` | `Disque non ouvert` |
  | `Fr.search` · `Fr.searchHint` | `Rechercher` · `Nom du fichier` |
  | `Fr.noResult` · `Fr.tooManyResults` | `Aucun résultat` · `Plus de 500 résultats : précisez la recherche` |
  | `Fr.transferLine(move, name, i, n, pct)` | `Copie : $name ($i/$n) — $pct %`, ou `Déplacement : …` |
  | `Fr.transferDone(n)` · `Fr.transferCancelled` | `Terminé : $n fichier(s)` · `Opération annulée` |
  | `Transfers.MSG_BUSY` | `Une copie est déjà en cours` |
  | `Transfers.msgNoSpace(need, free)` | `Pas assez de place : $need nécessaires, $free libres` |
  | `Transfers.MSG_INTO_ITSELF` | `Un dossier ne peut pas être copié dans lui-même` |
  | `Transfers.MSG_SOURCE_GONE` | `Stockage fermé : opération interrompue` |
  | `Transfers.MSG_LINKS` | `Les liens symboliques et fichiers spéciaux ne sont pas copiés` |
  | `Files.MSG_NO_VIEWER` | `Aucune application ne sait ouvrir ce fichier` |
  | `Files.MSG_SHARE_DIR` | `Un dossier ne peut pas être partagé` |
  | `Files.MSG_NO_ACCESS` | `Accès aux fichiers du téléphone non autorisé` |

- **Réglages persistés** dans les `SharedPreferences` « fluluks », déjà utilisées pour `lecture_seule` :
  - `tri` : `name`, `size` ou `date` ;
  - `tri_croissant` (booléen) ;
  - `grille` (booléen) ;
  - `caches` (booléen) ;
  - `favoris` : un tableau JSON `[{"key":…,"path":…,"label":…}]`, 100 entrées au plus.
- **Limites** : recherche bornée à 500 résultats et à une profondeur de 64 ; vignettes de 256 px au plus,
  cache en mémoire de 32 Mio côté Kotlin, 3 calculs de vignettes en parallèle au plus.
- **Appareil.** Pixel 4 XL, `192.168.0.104:5555` (Wi‑Fi) ou `98291FFBA003RN` (USB).
  - Les agents n'écrivent que sous `/data/local/tmp/fluluks/`, dans les dossiers de l'application, et,
    pour les tests instrumentés de `LocalSource`, dans un dossier jetable `Download/portail-tests/`
    supprimé en fin de test.
  - **Avant tout essai par l'utilisateur, l'APK à jour est construite et installée.**
- **Commandes de vérification :**
  ```sh
  (cd android && ./gradlew :app:testDebugUnitTest)                       # JVM
  flutter analyze && flutter test                                        # Dart
  dart run pigeon --input pigeons/fluluks_api.dart                       # après tout changement du pont
  (cd android && ANDROID_SERIAL=192.168.0.104:5555 ./gradlew :app:connectedDebugAndroidTest \
    -Pandroid.testInstrumentationRunnerArguments.class=com.mawena.fluluks.<Classe>)   # Pixel
  ```

## Review Focus

1. **Déplacement entre deux stockages interrompu** (annulation, débranchement du disque, carte SD
   retirée). Attendu :
   - la source n'est supprimée qu'après la copie complète **et vérifiée par la taille** de chaque
     élément ;
   - aucun fichier n'est perdu ;
   - le fichier partiel de la destination est supprimé si la destination est encore là.

   *Tests : T4 `moveDeletesSourceOnlyAfterCopy`, `moveCancelledKeepsSource`, `destinationGoneFails`.*
2. **Copier un dossier dans lui-même ou dans un de ses descendants**, sur la même source. Attendu :
   `MSG_INTO_ITSELF`, avant toute écriture. *Tests : T4 `copyIntoItselfRefused`.*
3. **Noms du téléphone écrits sur FAT ou NTFS** (`photo:1.jpg`, `a?b`, nom de 300 octets, collision de
   casse avec un fichier existant). Attendu : nom assaini puis unique, jamais d'échec de toute la
   copie. *Tests : T4 `copySanitizesNames`, `copyUniqueNamesCaseInsensitive`.*
4. **Stockage qui disparaît pendant qu'on l'affiche** : éjection depuis la notification, débranchement,
   carte SD retirée. Attendu :
   - retour à l'accueil avec « Le disque a été fermé » ;
   - copie arrêtée avec `MSG_SOURCE_GONE` ;
   - vignettes et résultats en mémoire du disque oubliés.

   *Tests : T6 `cacheDroppedOnEject`, T9 `explorerPopsWhenSourceCloses`.*
5. **Dossier inaccessible du téléphone** (`Android/data`, `Android/obb`) **ou accès non accordé.**
   Attendu : `Fr.protectedFolder` ou la carte d'autorisation, jamais d'exception, ni de liste vide
   trompeuse. *Tests : T2 `protectedDirReported`, `noAccessReported`.*

---

## Arborescence

```
assets/logo/portail.svg                     (T1) source du logo, 512 × 512
tools/logo/render_icons.py                  (T1) PNG des mipmaps et aperçu, par librsvg
android/app/src/main/res/
  mipmap-anydpi-v26/ic_launcher.xml         (T1) icône adaptative (fond, avant-plan, monochrome)
  drawable/ic_launcher_fond.xml, ic_launcher_avant.xml, ic_launcher_mono.xml, ic_portail.xml (T1)
  mipmap-*/ic_launcher.png                  (T1) régénérées depuis le SVG
  xml/file_paths.xml                        (T3) chemins du FileProvider
android/app/src/main/kotlin/com/mawena/fluluks/files/
  FileSource.kt   (T2) interface, Entry, PathRules
  LocalSource.kt  (T2)        DiskSource.kt (T2)        Sources.kt (T2)
  Files.kt        (T3) opérations, intents      Transfers.kt (T4)
  Search.kt       (T5)        Thumbnails.kt (T6)        ViewPrefs.kt (T7)
lib/src/screens/home_screen.dart, explorer_screen.dart, search_screen.dart, settings_screen.dart (T8–T10)
lib/src/widgets/thumb.dart, transfer_bar.dart, clipboard_bar.dart, source_tile.dart (T8–T10)
```

---

### Task 1 : nom « Portail » et logo

**Files :**
- Create :
  - `assets/logo/portail.svg`, à partir du brouillon validé par l'utilisateur ;
  - `tools/logo/render_icons.py` ;
  - les ressources de `res/` de l'arborescence.
- Modify :
  - `AndroidManifest.xml` : `android:label="Portail"`, `android:roundIcon`, `android:icon` ;
  - `VolumeService.kt` : titre « Portail » et petite icône `ic_portail` ;
  - `strings_fr.dart` : `Fr.title` ;
  - `test/disks_screen_test.dart`, `README.md` (titre « Portail », avec « anciennement FluLuks » une fois).

**Interfaces :**
- Produces :
  - `render_icons.py <svg> <res-dir>`, qui écrit :
    - `mipmap-{m,h,xh,xxh,xxxh}dpi/ic_launcher.png`, en 48, 72, 96, 144 et 192 px ;
    - `ic_launcher_round.png`, aux mêmes tailles ;
    - `docs/logo/apercu.png`, en 512 px.
  - Icône adaptative :
    - fond : dégradé `#4F46E5` → `#9333EA` en `<gradient>` de vecteur ;
    - avant-plan : le dossier-bouclier et sa serrure, tenus dans la zone sûre de 66 dp sur 108 ;
    - monochrome : la silhouette, pour les icônes à thème d'Android 13.
  - `ic_portail` : silhouette blanche 24 dp, la serrure en creux, pour la notification.

- [ ] **Step 1 : tests qui échouent.**
  - `test/disks_screen_test.dart` : la barre de titre affiche `Portail`.
  - Instrumenté `AppIdentityTest.labelIsPortail` : `packageManager.getApplicationLabel(...) ==
    "Portail"`.
- [ ] **Step 2 : lancer** `flutter test`. Expected : FAIL sur le titre.
- [ ] **Step 3 : implémenter.**
  - Lancer `render_icons.py` et commiter les PNG produites.
  - Vérifier l'aperçu à 48 px avec l'outil Read : la serrure doit rester lisible.
- [ ] **Step 4 : vérifier.**
  - `flutter analyze`, `flutter test`, JVM, APK construite et installée sur le Pixel.
  - Capture d'écran du lanceur (`adb exec-out screencap -p`), lue par l'agent : icône et nom visibles.
- [ ] **Step 5 : commit** `feat(identité): l'application s'appelle Portail, nouveau logo`.

### Task 2 : sources de fichiers et accès au téléphone

**Files :**
- Create :
  - `files/FileSource.kt`, `files/LocalSource.kt`, `files/DiskSource.kt`, `files/Sources.kt` ;
  - `test/.../MemEngine.kt`, extrait de `WriteOpsTest` ;
  - `test/.../LocalSourceTest.kt`, `test/.../DiskSourceTest.kt`.
- Modify :
  - `VolumeManager.kt` : `writeLock`, `notifyChanged`, `noteActivity` déplacés ou ajoutés ;
  - `FluLuksDocumentsProvider.kt` : utilise `VolumeManager.writeLock` ;
  - `AndroidManifest.xml` : `MANAGE_EXTERNAL_STORAGE`, puis `READ_EXTERNAL_STORAGE` et
    `WRITE_EXTERNAL_STORAGE` avec `maxSdkVersion="29"`, `requestLegacyExternalStorage="true"` ;
  - `MainActivity.kt` (demande de permission), `pigeons/fluluks_api.dart`, `HostApiImpl.kt`, la partie
    Dart.

**Interfaces :**
- Produces (Kotlin) :
  - `data class Entry(val name: String, val path: String, val type: FileType, val size: Long, val mtime: Long)`,
    avec `mtime` en secondes UTC.
  - `object PathRules { fun check(path: String): String? }` : le chemin normalisé, ou null.
  - `interface FileSource`, avec :
    - les propriétés `id`, `stableKey`, `label`, `readOnlyReason: String?` (null en écriture),
      `caseInsensitive: Boolean` ;
    - les méthodes :
      - `list(path): List<Entry>` : lève `ProtectedDirException` si la lecture est refusée ;
      - `stat(path): Entry?` ;
      - `space(): Pair<Long, Long>` : place libre et totale ;
      - `mkdir(parent, name): String` ;
      - `rename(path, newName): String` ;
      - `delete(path)` : récursif ;
      - `openRead(path): InputStream` ;
      - `createFile(parent, name): Pair<String, OutputStream>` ;
      - `contentUri(path): Uri`.
  - `LocalSource(id, stableKey, label, root: File)`.
  - `DiskSource(volumeId, engine: Engine = NativeEngine)`, sur `WriteOps` :
    - `openRead` et `createFile` sont des flux sur `pread` et `pwrite` par morceaux de 1 Mio ;
    - `close` du flux d'écriture ferme le fichier moteur, puis appelle `noteWrite` ;
    - `contentUri` est l'URI du provider.
  - `object Sources` : `list(): List<FileSource>` (téléphone, cartes SD montées, volumes ouverts),
    `get(id): FileSource?`, `hasAllFilesAccess(): Boolean`.
- Produces (Pigeon) :
  - `class SourceMsg { String id; String stableKey; String kind; String label; int totalBytes; int freeBytes; bool readOnly; String roReason; }`,
    avec `kind` ∈ {`phone`, `sd`, `disk`} ;
  - `List<SourceMsg> listSources()` ;
  - `bool hasAllFilesAccess()` ;
  - `void requestAllFilesAccess()` : `Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION` avec
    `package:` sur API 30 et plus, demande des permissions à l'exécution en dessous.
  - `AppState.sources`, rafraîchi à chaque `onStateChanged` et au retour au premier plan
    (`AppLifecycleListener`).

- [ ] **Step 1 : tests qui échouent.**
  - **`LocalSourceTest`** (JVM, `TemporaryFolder`) :
    - `listsSortedEntriesWithTypes` ;
    - `canonicalEscapeRefused` : un lien symbolique vers `/` ou un chemin `/../x` est refusé ;
    - `protectedDirReported` : dossier sans droit de lecture, d'où `ProtectedDirException` ;
    - `mkdirRenameDeleteRecursive` ;
    - `createFileAndReadBack`.
  - **`DiskSourceTest`** (JVM, `MemEngine` monté par `VolumeManager.resetForTests`) :
    - `writesTakeSharedLockAndNoteWrite` ;
    - `readOnlyVolumeRefusesWrites` : « Lecture seule : <raison> » ;
    - `streamsRoundTrip3MiB` ;
    - `everyCallNotesActivity`.
  - **`noAccessReported`** (instrumenté) : sans l'accès à tous les fichiers, `listDir("phone", "/")`
    rend `Files.MSG_NO_ACCESS`. Il est vérifié seulement si l'accès n'est pas accordé sur l'appareil,
    sinon sauté avec `assumeTrue`.
- [ ] **Step 2 : lancer** les tests JVM ciblés. Expected : FAIL, classes absentes.
- [ ] **Step 3 : implémenter.**
  - Extraire `MemEngine` sans changer son comportement.
  - Déplacer `writeLock` et `notifyChanged` du provider vers `VolumeManager`. Le provider appelle
    `noteActivity` dans `resolve()`, `onRead` et `onWrite`.
- [ ] **Step 4 : vérifier.**
  - JVM tout vert, `flutter analyze`, `flutter test`.
  - `connectedDebugAndroidTest` pour `ProviderTest` et `ProviderWriteTest` : le verrou a bougé.
- [ ] **Step 5 : commit** `feat(fichiers): sources téléphone, carte SD et disques, accès à tous les fichiers`.

### Task 3 : opérations — lister, créer, renommer, supprimer, ouvrir avec, partager

**Files :** Create `files/Files.kt`, `res/xml/file_paths.xml`, `test/.../FilesTest.kt` et
`androidTest/.../FilesInstrumentedTest.kt`. Modify le manifeste (`<provider
androidx.core.content.FileProvider>`, autorité `com.mawena.fluluks.fichiers`), `pigeons/…`,
`HostApiImpl.kt` et la partie Dart.

**Interfaces :**
- Produces (Kotlin), `object Files` :
  - `list(sourceId, path, showHidden): List<Entry>` : cachés = nom commençant par « . » ;
  - `createFolder(sourceId, parent, name): String` ;
  - `rename(sourceId, path, newName): String` ;
  - `delete(sourceId, paths)` ;
  - `viewIntent(sourceId, path): Intent` :
    - `ACTION_VIEW` sur `contentUri`, type `MimeTypes.forName` ;
    - droit de lecture, plus le droit d'écriture si la source est en écriture ;
    - enveloppée dans `createChooser(…, "Ouvrir avec…")` ;
  - `shareIntent(sourceId, paths): Intent` :
    - `ACTION_SEND` ou `ACTION_SEND_MULTIPLE`, avec `ClipData` ;
    - type commun s'il est unique, sinon `*/*` ;
    - un dossier dans la liste donne `MSG_SHARE_DIR`.
- Produces (Pigeon) :
  - `class EntryMsg { String name; String path; String type; int size; int mtime; }`, avec `type` ∈
    {`dir`, `file`, `link`, `other`} ;
  - `@async List<EntryMsg> listDir(String sourceId, String path, bool showHidden)` ;
  - `@async String createFolder(String sourceId, String parent, String name)` ;
  - `@async String rename(String sourceId, String path, String newName)` ;
  - `@async void deleteEntries(String sourceId, List<String> paths)` ;
  - `void openWith(String sourceId, String path)` ;
  - `void share(String sourceId, List<String> paths)`.
  - Erreurs : `FlutterError(code, message)`. Le message vient de `ProviderErrors.message(code, op)` pour
    un disque, ou du message de l'exception, toujours en français.

- [ ] **Step 1 : tests qui échouent.**
  - `FilesTest` (JVM, une source locale et une source disque) :
    - `listSortsDirsFirstThenNameCaseInsensitive` : `/b.txt`, `/A`, `/a.txt` et `/c` donnent `A`, `c`,
      `a.txt`, `b.txt` ;
    - `hiddenFilesFiltered` ;
    - `hostilePathRejectedBeforeEngine` ;
    - `createFolderUnique` : `Photos (1)` ;
    - `renameInvalidOnDisk` : « Nom non autorisé sur ce disque » ;
    - `deleteManyNotesWriteOnce`.
  - `FilesInstrumentedTest` (Pixel, `ext4-rw-reel.img` et `Download/portail-tests/`) :
    - `viewIntentUriReadable` : pour un fichier local et pour un fichier du disque ;
    - `shareIntentHasAllUris` ;
    - `shareRefusesDirectory`.
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** : JVM, instrumenté, `flutter analyze`, `flutter test`.
- [ ] **Step 5 : commit** `feat(fichiers): lister, créer, renommer, supprimer, ouvrir avec et partager`.

### Task 4 : copier et déplacer entre stockages

**Files :** Create `files/Transfers.kt` et `test/.../TransfersTest.kt`. Modify `DocNames.kt`
(`sanitize`), `DocNamesTest.kt`, `pigeons/…`, `HostApiImpl.kt` et la partie Dart.

**Interfaces :**
- Produces :
  - `data class TransferState` : `move: Boolean`, `currentName`, `fileIndex` (à partir de 1),
    `fileCount`, `doneBytes`, `totalBytes`, `finished`, `cancelled`, `error: String?`.
  - `object Transfers` :
    - `val state: StateFlow<TransferState?>` ;
    - `start(srcId, paths, dstId, dstDir, move: Boolean)` ;
    - `cancel()` ;
    - `isBusy(sourceId): Boolean` : sert au verrouillage automatique de la phase 7.
  - Règles :
    - **Une seule opération à la fois**, sinon `MSG_BUSY`. Travail sur `Dispatchers.IO`, par morceaux
      de 1 Mio.
    - **Publication de l'état** au plus toutes les 200 ms, et à chaque fin de fichier.
    - **Avant toute écriture :**
      - parcours borné de la source (`WriteOps.MAX_DEPTH`, `MAX_ENTRIES`) ;
      - place libre de la destination comparée à la somme des tailles, sinon `msgNoSpace` ;
      - copie dans soi-même refusée (`MSG_INTO_ITSELF`).
    - **Liens et fichiers spéciaux** ignorés, puis `error = MSG_LINKS` à la fin, sans échec.
    - **Noms** : vers un disque, `sanitize` puis `unique`. Vers une source locale, `unique` seulement.
    - **Déplacement sur la même source** : `rename` (disque : `WriteOps.move`, local :
      `File.renameTo`). Sinon, copie, puis suppression de **chaque élément de premier niveau** une fois
      tous ses fichiers copiés et leur taille vérifiée par `stat` de la destination.
    - **Échec ou annulation** : le fichier en cours de la destination est supprimé si la destination
      existe encore ; ce qui est déjà complet est gardé.
    - **Source ou destination disparue** : l'opération s'arrête avec `MSG_SOURCE_GONE`.
    - **Pendant la copie** : `noteActivity` à chaque morceau pour un disque, puis `noteWrite` et
      `notifyChanged` à chaque fichier écrit sur un disque.
  - Pigeon :
    - `class TransferMsg`, champs de `TransferState` ;
    - `void startTransfer(String srcId, List<String> paths, String dstId, String dstDir, bool move)` ;
    - `void cancelTransfer()` ;
    - `@FlutterApi void onTransferChanged(TransferMsg? transfer)`.

- [ ] **Step 1 : tests qui échouent** (`TransfersTest`, sources local `TemporaryFolder` et disque
  `MemEngine`) :
  - `copyLocalToDiskWithProgress` : 3 fichiers et un dossier, état final `finished`, `doneBytes ==
    totalBytes`, contenus identiques ;
  - `copyDiskToLocal` ;
  - `copyRefusedWithoutSpace` ;
  - `copyIntoItselfRefused` ;
  - `copySanitizesNames` : `photo:1.jpg` donne `photo_1.jpg` ;
  - `copyUniqueNamesCaseInsensitive` : `X.txt` existe sur une source insensible à la casse, d'où
    `x (1).txt` ;
  - `moveSameSourceRenames` : aucune copie d'octets ;
  - `moveDeletesSourceOnlyAfterCopy` ;
  - `moveCancelledKeepsSource` ;
  - `cancelRemovesPartial` ;
  - `efbigRemovesPartialAndReports` : `MemEngine` rend `FL_EFBIG`, message « Fichier trop gros pour ce
    disque… » ;
  - `destinationGoneFails` : volume retiré pendant la copie, d'où `MSG_SOURCE_GONE` ;
  - `secondTransferRefused`.
  - `DocNamesTest.sanitize*` : caractères interdits, point et espaces finaux, 300 octets avec
    extension, nom vide.
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier.**
  - Test instrumenté `FilesInstrumentedTest.copyPhoneToDiskAndBack` : 8 Mio entre
    `Download/portail-tests/` et une copie d'`ext4-rw-reel.img`, contenus identiques.
  - Puis JVM, `flutter analyze`, `flutter test`.
- [ ] **Step 5 : commit** `feat(fichiers): copier et déplacer entre stockages, avec progression et annulation`.

### Task 5 : recherche

**Files :** Create `files/Search.kt` et `test/.../SearchTest.kt`. Modify `pigeons/…`, `HostApiImpl.kt` et
la partie Dart.

**Interfaces :**
- Produces :
  - `Search.run(sourceId, path, query, showHidden): SearchResult(entries: List<Entry>, truncated: Boolean)`.
    - La comparaison porte sur le nom, sans tenir compte de la casse ni des accents
      (`Normalizer.NFD`, puis suppression des marques).
    - Parcours en largeur, profondeur 64, 500 résultats au plus (`truncated`).
    - Les dossiers protégés sont sautés sans erreur.
  - `Search.cancel()` : la recherche en cours se termine avec ce qu'elle a trouvé.
  - Pigeon :
    - `class SearchResultMsg { List<EntryMsg> entries; bool truncated; }` ;
    - `@async SearchResultMsg search(String sourceId, String path, String query, bool showHidden)` ;
    - `void cancelSearch()`.

- [ ] **Step 1 : tests qui échouent** (`SearchTest`, source locale et source disque) :
  - `findsAccentAndCaseInsensitive` : `ete` trouve `Été.jpg` ;
  - `truncatesAt500` ;
  - `skipsProtectedDirs` ;
  - `cancelReturnsPartial` ;
  - `emptyQueryReturnsNothing`.
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier.** JVM, `flutter analyze`.
- [ ] **Step 5 : commit** `feat(fichiers): recherche par nom`.

### Task 6 : vignettes des photos et vidéos

**Files :** Create `files/Thumbnails.kt`, `test/.../ThumbnailsTest.kt` et
`androidTest/.../ThumbnailsInstrumentedTest.kt`. Modify `VolumeManager.kt` (appel de
`Thumbnails.drop(volumeId)` quand un volume est oublié), `pigeons/…`, `HostApiImpl.kt` et la partie Dart.

**Interfaces :**
- Produces :
  - `Thumbnails.get(sourceId, path, sizePx): ByteArray?` : JPEG de qualité 80, ou null si le type n'est
    pas une image ou une vidéo connue, ou si le décodage échoue.
    - **Images** (`image/*` selon `MimeTypes`) : `BitmapFactory`, une passe `inJustDecodeBounds` puis le
      décodage avec `inSampleSize`, le flux étant rouvert entre les deux.
    - **Vidéos** (`video/*`) : `MediaMetadataRetriever.getScaledFrameAtTime(1_000_000, OPTION_CLOSEST_SYNC, …)`.
      Une source locale passe son chemin ; un disque passe le descripteur obtenu par
      `ContentResolver.openFileDescriptor(contentUri, "r")`.
    - **Cache :** `LruCache` de 32 Mio, clé `sourceId|path|size|mtime`, en mémoire seulement. Au plus
      3 calculs à la fois (`Semaphore`).
  - `Thumbnails.drop(sourceId)`.
  - Pigeon : `@async Uint8List? thumbnail(String sourceId, String path, int sizePx)`.
  - Dart : `Thumb` (widget) met les octets en cache dans un `Map` de 200 entrées au plus, par écran, et
    affiche l'icône du type en attendant ou en cas d'échec.

- [ ] **Step 1 : tests qui échouent.**
  - `ThumbnailsTest` (JVM, avec un décodeur injecté) :
    - `cacheHitDoesNotDecodeTwice` ;
    - `cacheDroppedOnEject` ;
    - `unknownTypeReturnsNull` ;
    - `keyChangesWithMtime`.
  - `ThumbnailsInstrumentedTest` (Pixel) :
    - `jpegFromPhoneAndDisk` : JPEG de 4000 × 3000 généré dans le test, vignette d'au plus 256 px ;
    - `videoFrameFromPhone` : MP4 généré par `MediaCodec` ou `MediaMuxer` dans le test, et sauté
      (`assumeTrue`) si l'encodeur n'est pas disponible.
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier.** JVM, instrumenté, `flutter analyze`.
- [ ] **Step 5 : commit** `feat(fichiers): vignettes des photos et vidéos, en mémoire seulement`.

### Task 7 : tri, affichage et favoris

**Files :** Create `files/ViewPrefs.kt` et `test/.../ViewPrefsTest.kt`. Modify `pigeons/…`,
`HostApiImpl.kt` et la partie Dart.

**Interfaces :**
- Produces :
  - `class ViewPrefsMsg { String sort; bool ascending; bool grid; bool showHidden; }` ;
  - `ViewPrefsMsg getViewPrefs()`, `void setViewPrefs(ViewPrefsMsg p)` : une valeur inconnue revient au
    défaut (`name`, croissant, liste, cachés masqués) ;
  - `class FavoriteMsg { String key; String path; String label; String? sourceId; }` : `sourceId` vaut
    null si le stockage n'est pas ouvert ;
  - `List<FavoriteMsg> listFavorites()`, `void addFavorite(String sourceId, String path, String label)`,
    `void removeFavorite(String key, String path)`. 100 favoris au plus, pas de doublon.
  - Tri appliqué côté Dart, dossiers toujours en tête : `sortEntries(List<EntryMsg>, String sort, bool asc)`
    dans `lib/src/state/sorting.dart`. Le nom se compare sans casse, la taille et la date se départagent
    par le nom.

- [ ] **Step 1 : tests qui échouent.**
  - `ViewPrefsTest` :
    - `defaultsAndUnknownValues` ;
    - `favoritesPersistAndDedupe` ;
    - `favoriteResolvesToOpenVolumeByPartitionUuid` ;
    - `favoriteLimit100`.
  - `test/sorting_test.dart` :
    - `dirsFirstThenNameCaseInsensitive` ;
    - `sizeDescendingTiesByName` ;
    - `dateAscending`.
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier.** JVM, `flutter test`.
- [ ] **Step 5 : commit** `feat(fichiers): tri, affichage en grille, fichiers cachés et favoris`.

### Task 8 : écran d'accueil et Réglages

**Files :** Create `home_screen.dart`, `settings_screen.dart`, `source_tile.dart`,
`test/home_screen_test.dart` et `test/settings_screen_test.dart`. Modify `app.dart` (accueil =
`HomeScreen`), `disks_screen.dart` (ses cartes deviennent un widget réutilisé par `HomeScreen`, et l'écran
lui-même disparaît), `partition_tile.dart` (bouton `Fr.browse`), `fake_host_api.dart` et
`strings_fr.dart`.

**Interfaces :**
- Produces :
  - **`HomeScreen({required AppState state})`** :
    - barre `Fr.title`, avec l'icône Réglages ;
    - carte `Fr.allFilesTitle` (`Fr.allFilesBody`, bouton `Fr.allFilesButton`) tant que l'accès n'est
      pas accordé ;
    - section « Stockage » : `Téléphone`, puis `Carte SD` si présente, chacun avec `Fr.freeOf` et une
      barre d'occupation ;
    - section `Fr.usbDisks` : les cartes de disques actuelles, avec `Fr.browse` sur un volume ouvert ;
    - section `Fr.favorites` : grisée et `Fr.favoriteUnavailable` si le stockage n'est pas ouvert ;
    - toucher une source ou un favori ouvre `ExplorerScreen` (T9).
  - **`SettingsScreen({required AppState state})`** :
    - l'interrupteur `Fr.readOnlySwitch` (avec `Fr.readOnlyHelp`), déplacé depuis l'accueil ;
    - `Fr.allFilesTitle`, qui indique « Autorisé » ou un bouton `Fr.allFilesButton` ;
    - `Fr.showHidden`, le défaut des préférences d'affichage.

- [ ] **Step 1 : tests qui échouent** :
  - `homeShowsPhoneSdDisksFavorites` ;
  - `accessCardUntilGranted` ;
  - `browseOnOpenVolume` ;
  - `unavailableFavoriteGreyed` ;
  - `settingsReadOnlySwitch` ;
  - `settingsAccessButton`.

  Les tests de `disks_screen_test.dart` sont repris tels quels sur `HomeScreen`.
- [ ] **Step 2 : lancer** `flutter test`. Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier.** `flutter analyze`, `flutter test`, APK installée, capture d'écran de
  l'accueil sur le Pixel lue par l'agent.
- [ ] **Step 5 : commit** `feat(interface): accueil Portail (stockages, disques, favoris) et Réglages`.

### Task 9 : explorateur

**Files :** Create `explorer_screen.dart`, `thumb.dart`, `transfer_bar.dart`, `clipboard_bar.dart` et
`test/explorer_screen_test.dart`. Modify `app_state.dart` (presse-papiers, transfert).

**Interfaces :**
- Consumes : T3 à T7.
- Produces :
  - **`ExplorerScreen({required AppState state, required String sourceId, String path = '/'})`.**
  - **Barre du haut :**
    - titre = nom du dossier, ou le libellé de la source à la racine ;
    - actions : `Fr.search` (ouvre `SearchScreen`, T10), étoile (`Fr.addFavorite` /
      `Fr.removeFavorite`), et un menu avec `Fr.sortBy` (`Fr.sortName`, `Fr.sortSize`, `Fr.sortDate`,
      `Fr.ascending` / `Fr.descending`), `Fr.grid` / `Fr.list` et `Fr.showHidden`. Chaque changement est
      persisté par `setViewPrefs`.
    - « retour » remonte d'un dossier, puis revient à l'accueil.
  - **Contenu :**
    - liste, ou grille de 3 colonnes ;
    - `Thumb` pour les images et vidéos, icône du type sinon ;
    - sous-titre `taille · date` ;
    - `Fr.emptyFolder` ou `Fr.protectedFolder` selon le cas ;
    - tirer pour rafraîchir.
  - **Toucher :** dossier = navigation, fichier = `openWith`.
  - **Appui long :** sélection, avec `Fr.selected(n)`.
    - Actions : `Fr.copy`, `Fr.cut` (désactivé en lecture seule), `Fr.share` (fichiers seulement),
      `Fr.rename` (un élément), `Fr.delete` (confirmation `deleteOne` ou `deleteMany`, avec
      `deleteBody`).
  - **`ClipboardBar`** si le presse-papiers n'est pas vide : `Fr.clipboard(n, move)`, puis
    `Fr.pasteHere`, qui appelle `startTransfer` vers le dossier affiché et désactive le bouton si la
    destination est en lecture seule, et `Fr.cancel`.
  - **Bouton flottant** (source en écriture) : `Fr.newFolder`.
  - **`TransferBar`** en bas pendant une opération :
    - barre de progression et `Fr.transferLine`, avec `Fr.cancel` ;
    - à la fin : `transferDone`, `transferCancelled` ou l'erreur, pendant 4 s ;
    - rafraîchit la liste affichée à chaque fichier écrit dans ce dossier.
  - **Source disparue** de `AppState.sources` : `popUntil(isFirst)` et SnackBar `Fr.volumeClosed`.
  - **Source en lecture seule** : bandeau `Fr.readOnly(roReason)`.

- [ ] **Step 1 : tests qui échouent** :
  - `listsAndNavigates` ;
  - `gridShowsThumbnails` ;
  - `sortMenuPersists` ;
  - `tapFileOpensWith` ;
  - `selectionCopyPasteStartsTransfer` ;
  - `cutPasteMoves` ;
  - `pasteDisabledOnReadOnly` ;
  - `deleteAfterConfirm` ;
  - `renameDialog` ;
  - `shareDisabledForFolder` ;
  - `transferBarProgressAndCancel` ;
  - `explorerPopsWhenSourceCloses` ;
  - `favoriteStarToggles` ;
  - `protectedFolderMessage`.
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier.** `flutter analyze`, `flutter test`, APK installée, captures d'écran de
  l'explorateur (liste et grille) sur le Pixel, lues par l'agent.
- [ ] **Step 5 : commit** `feat(interface): explorateur — grille, vignettes, tri, presse-papiers, copies`.

### Task 10 : écran de recherche

**Files :** Create `search_screen.dart` et `test/search_screen_test.dart`.

**Interfaces :**
- Produces : `SearchScreen({required AppState state, required String sourceId, required String path})`.
  - Champ `Fr.searchHint`, recherche lancée 400 ms après la dernière frappe ; la recherche précédente est
    annulée par `cancelSearch`.
  - Résultats avec le chemin du dossier en sous-titre.
  - Toucher un fichier l'ouvre avec `openWith` ; toucher un dossier ouvre `ExplorerScreen` sur lui.
  - `Fr.noResult`, ou `Fr.tooManyResults` si la liste est tronquée.

- [ ] **Step 1 : tests qui échouent** :
  - `debouncesAndCancelsPrevious` ;
  - `showsResultsWithParentPath` ;
  - `tooManyResultsBanner` ;
  - `tapFolderOpensExplorer`.
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier.** `flutter analyze`, `flutter test`.
- [ ] **Step 5 : commit** `feat(interface): recherche`.

### Task 11 *(utilisateur)* : essais sur le Pixel

L'agent construit et installe l'APK à jour, puis passe la main. L'utilisateur essaie :
1. **Identité :** le nom et le logo dans le lanceur, dans la notification et dans le sélecteur de
   « Fichiers ».
2. **Accès :** l'autorisation d'accès aux fichiers, puis la navigation dans le téléphone et sur une carte
   SD si le Pixel en reçoit une (sinon, sur une clé USB en clair).
3. **Affichage :** la grille avec les vignettes, le tri, les favoris (y compris un favori sur le disque
   chiffré, qui revient à la réouverture) et la recherche.
4. **Copies :** copier et déplacer des photos du téléphone vers le disque chiffré et inversement, dont un
   gros fichier avec annulation.
5. **Fichiers :** ouvrir avec VLC, partager vers une messagerie.

### Task 12 : documentation et v0.5.0

- [ ] **Step 1 : écrire.**
  - **README :**
    - Portail, gestionnaire de fichiers ;
    - accès à tous les fichiers (pourquoi, et comment le retirer) ;
    - vignettes gardées en mémoire seulement ;
    - limites (`Android/data`, liens non copiés) ;
    - feuille de route.
  - **Feuille de route :** tâches cochées.
- [ ] **Step 2 : commit** `docs: Portail, gestionnaire de fichiers`.
- [ ] Relecture finale de toute la branche, puis fusion `feature/portail` → `develop`. Ensuite
  `release/0.5.0` (pubspec `0.5.0+6`), fusion dans `main`, étiquette `v0.5.0`, retour dans `develop`.
  Pas de push sans l'accord de l'utilisateur.

---

**Phase 7 (plan écrit à la fin de la phase 6) :**
- verrouillage automatique après X minutes d'inactivité, réglage désactivé par défaut ;
- débranchement inattendu : conseil selon le système de fichiers, copies interrompues ;
- performances du § 5 mesurées sur le Pixel : listage de 1 000 fichiers, copies téléphone ↔ disque ;
- essai d'ext4 en écriture sur le vrai disque (tâche 13 de la phase 3) ;
- version **v1.0.0**.
