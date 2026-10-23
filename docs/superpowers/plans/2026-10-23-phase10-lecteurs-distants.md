# Portail — Phase 10 : lecteurs intégrés et dossiers distants — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** ouvrir images, textes, musique et vidéos dans Portail (visionneuse, éditeur, lecteur de musique en
arrière-plan, lecteur vidéo avec sous-titres) et parcourir des dossiers distants FTP et SFTP comme les autres
stockages. Fin de phase : version **v1.3.0**.

**Architecture :**
- **Kotlin, `files/`** :
  - `FileContent` : lecture d'une image (octets bruts, HEIC converti en JPEG) et d'un texte (UTF-8 strict, BOM et
    CRLF détectés), enregistrement d'un texte par fichier temporaire puis remplacement ;
  - `SeekableReader` : lecture à accès aléatoire sur un flux qu'on ne sait ouvrir qu'à un décalage (FTP `REST`,
    SFTP), qui ne rouvre le flux que pour un saut lointain.
- **Kotlin, `provider/StreamProvider`** : fournisseur de contenu interne (`<applicationId>.flux`) qui sert un fichier
  de n'importe quelle source par un descripteur mandataire (`StorageManager.openProxyFileDescriptor`) : c'est
  l'`contentUri` des sources distantes, lu par les lecteurs Media3, « Ouvrir avec », « Partager » et le lecteur
  d'archives natif.
- **Kotlin, `remote/`** :
  - `RemoteServers` : serveurs enregistrés (JSON) et secrets scellés par `SecretBox` (AES-GCM, clé du Keystore) ;
  - `RemoteClient` : interface d'un protocole ; `FtpClient` (Commons Net) et `SftpClient` (JSch + Bouncy Castle),
    chacun avec une petite réserve de connexions ou de canaux ;
  - `RemoteSource` : `FileSource` au-dessus d'un `RemoteClient` (noms hostiles écartés, place inconnue) ;
  - `Remotes` : connexions ouvertes, ajoutées à `Sources.list()`.
- **Kotlin, `media/`** : `VideoActivity` (Media3 `PlayerView`, sous-titres SubRip en `data:` URI) ;
  `MusicService` (Media3 `MediaSessionService`) et `Music` (pont : file d'attente, commandes, état).
- **Flutter :** `TextEditorScreen`, `ImageViewerScreen`, `NowPlayingScreen` et barre `MiniPlayer` ; section
  « Dossiers distants » de l'accueil et `ServerFormScreen` ; routage « Ouvrir dans Portail » dans l'explorateur.

**Tech Stack :** Kotlin, AndroidX Media3 1.11.1 (`media3-exoplayer`, `media3-ui`, `media3-session`), Apache Commons
Net 3.13.0, JSch 2.28.8 (mwiede), Bouncy Castle `bcprov-jdk18on` 1.86 ; tests JVM : Apache FtpServer 1.2.1, Apache MINA
SSHD 2.20.0 (`sshd-core`, `sshd-sftp`) ; Flutter 3.41.4, Pigeon 27.3.0.

**Spec :** [docs/superpowers/specs/2026-10-17-fluluks-design.md](../specs/2026-10-17-fluluks-design.md) (§ 4.2, § 11,
§ 12 et **§ 18 « lecteurs intégrés et dossiers distants »**).

## Global Constraints

- **Branche et commits.**
  - Branche `feature/lecteurs-distants`, créée depuis `develop` après la v1.2.0, dans l'arbre principal.
  - Commits sous l'identité `mawena <gamligocharles@gmail.com>`, en français (Conventional Commits), **sans aucune
    attribution à Claude**, en nommant les fichiers (`git commit -- <fichiers>`, nouveaux fichiers ajoutés d'abord).
  - Un commit par tâche au moins ; la tâche est cochée dans la feuille de route (section Phase 10) et ses étapes dans
    ce plan, dans le même commit.
- **Dépendances** : versions exactes ci-dessus, ajoutées dans `android/app/build.gradle.kts` par la tâche qui s'en
  sert ; licences citées dans le README. Règles R8 pour JSch et Bouncy Castle (chargés par réflexion) vérifiées par
  une construction release.
- **Sûreté et confidentialité.**
  - § 4.2 inchangé pour toute écriture sur un disque USB (éditeur compris).
  - Rien de ce que lisent les lecteurs n'est écrit sur le stockage du téléphone : octets en mémoire, sous-titres en
    `data:` URI, flux distants par descripteur mandataire, ZIP vers un serveur par un tube.
  - Secrets (mots de passe, clés privées, phrases de clé) : jamais dans les journaux ni les messages d'erreur ; en
    mémoire dans des `ByteArray` remis à zéro à la déconnexion ; sur disque seulement si « Mémoriser » est coché,
    scellés par `SecretBox`.
  - Un nom venu d'un serveur est hostile : vide, `.`, `..`, avec `/` ou NUL → écarté de la liste.
  - Tests sur le téléphone seulement sous `/data/local/tmp/fluluks/`, dans le cache de l'application et dans
    `Download/portail-tests/`.
- **Valeurs fixées** *(décisions du planificateur)* :
  - extensions (sans tenir compte de la casse) : images `jpg jpeg png gif webp bmp heic heif` ; musique `mp3 m4a
    aac ogg oga opus flac wav` ; vidéos `mp4 m4v mkv webm mov 3gp avi` ; textes `txt md markdown json xml csv tsv
    log ini conf cfg yaml yml toml srt vtt sh bash py kt kts java dart c h cpp hpp js ts css html htm properties
    gradle sql env gitignore` ;
  - image : 64 Mio au plus ; HEIC/HEIF : `ImageDecoder` (API 28+), plus grand côté ramené à 4096 px, JPEG qualité
    92 ; sur API 26-27 : « Format HEIC non pris en charge sur cette version d'Android » ;
  - texte : 2 Mio au plus ; un octet NUL ou un UTF-8 invalide → « Ce fichier n'est pas du texte UTF-8 » ; CRLF si le
    fichier contient `\r\n` (tout est réécrit en CRLF) ; temporaire `Transfers.partialName(nom)` ;
  - sous-titres : `<base>.srt` et `<base>.<langue>.srt` (langue de 2 ou 3 lettres) du même dossier, 2 Mio au plus,
    UTF-8 (BOM accepté) sinon Windows-1252 ; le premier trouvé est actif ;
  - lecteurs : recul et avance de 10 s ; position publiée toutes les 500 ms pendant la lecture ;
  - serveurs : ports par défaut 21 (FTP) et 22 (SFTP) ; FTP anonyme si l'utilisateur est vide
    (`anonymous` / `portail@`) ; délais : connexion 10 s, données 30 s, maintien SSH 30 s ; au plus 3 connexions
    (FTP) ou 3 canaux (SFTP) par serveur ; FTP en mode passif, binaire, UTF-8 si le serveur l'annonce (`FEAT`),
    sinon ISO-8859-1 ; `MLSD` si annoncé, sinon `LIST` ;
  - `SeekableReader` : un saut en avant de 256 Kio au plus est lu et jeté, au-delà le flux est rouvert ;
  - vignettes d'un dossier distant : images de 8 Mio au plus, pas de vignette vidéo.
- **Langue :** interface et messages en français.

## Review Focus

1. **Stockage fermé, disque arraché ou connexion perdue pendant une lecture** (musique en arrière-plan, vidéo,
   visionneuse, éditeur ouvert). Attendu : arrêt propre avec un message, aucun descripteur ni connexion perdus,
   éjection jamais bloquée par le lecteur, texte de l'éditeur gardé à l'écran. *Tests : T2 `saveFailureKeepsText`,
   T4 `readerClosedWhenSourceGone`, T7 `connectionDropMidRead`, T10 `stopsWhenSourceGone`.*
2. **Enregistrement de l'éditeur.** Attendu : jamais d'original tronqué ou perdu (échec d'écriture, stockage plein,
   fichier changé entre-temps) ; un texte non modifié se réécrit à l'octet près (BOM, CRLF). *Tests : T1
   `roundTripIdentical`, `changedSinceOpenNotSaved`, `failedWriteKeepsOriginal`.*
3. **Noms hostiles ou exotiques renvoyés par un serveur** (`..`, `/`, NUL, vide, accents en ISO-8859-1 sur un vieux
   FTP). Attendu : écartés ou décodés, jamais de chemin hors du dossier listé. *Tests : T5
   `hostileServerNamesSkipped`, T7 `latin1ListingDecoded`.*
4. **Clé SSH du serveur changée ; secrets.** Attendu : jamais acceptée en silence ; mot de passe non mémorisé par
   défaut ; secrets scellés sur disque, absents des messages. *Tests : T8 `changedHostKeyRefused`,
   `unknownHostKeyAskedOnce`, T5 `passwordNotStoredByDefault`, `secretsSealedOnDisk`.*
5. **Accès aléatoire sur un flux distant** (vidéo qu'on avance, ZIP lu par la fin, image ouverte deux fois).
   Attendu : octets justes à tout décalage, pas d'accumulation de connexions. *Tests : T4 `randomReadsMatch`,
   `farSeekReopensOnce`, T7 `readAtOffsets`, T8 `readAtOffsets`.*

---

### Task 1 : contenu des fichiers pour les lecteurs (Kotlin + Pigeon)

**Files :** Create `android/app/src/main/kotlin/com/mawena/fluluks/files/FileContent.kt`,
`android/app/src/test/kotlin/com/mawena/fluluks/FileContentTest.kt` ; Modify `pigeons/fluluks_api.dart`,
`bridge/HostApiImpl.kt`, `lib/src/state/host_api.dart`, `test/fake_host_api.dart` (fichiers générés régénérés).

**Interfaces :** Produces :
```kotlin
data class TextDoc(val text: String, val bom: Boolean, val crlf: Boolean, val mtime: Long, val size: Long, val readOnlyReason: String?)
data class TextSave(val saved: Boolean, val changed: Boolean, val mtime: Long, val size: Long)
object FileContent {
    const val MAX_IMAGE = 64L shl 20; const val MAX_TEXT = 2L shl 20
    @Volatile var heicToJpeg: ((ByteArray) -> ByteArray)?  // tests JVM ; null : ImageDecoder d'Android
    fun readImage(src: FileSource, path: String): ByteArray
    fun readText(src: FileSource, path: String): TextDoc
    fun writeText(src: FileSource, path: String, text: String, bom: Boolean, crlf: Boolean,
                  expectedMtime: Long, expectedSize: Long, force: Boolean): TextSave
}
```
Pigeon : `TextFileMsg(text, bom, crlf, mtime, size, readOnlyReason?)`, `TextSaveMsg(saved, changed, mtime, size)` ;
`@async Uint8List readImage(sourceId, path)`, `@async TextFileMsg readText(sourceId, path)`,
`@async TextSaveMsg writeText(sourceId, path, text, bom, crlf, expectedMtime, expectedSize, force)`.

- [x] **Step 1 : tests** (`FileContentTest`, source locale et disque en mémoire `MemEngine`) :
  - `readImageReturnsBytes`, `imageTooBigRefused` (64 Mio + 1), `heicConverted` (convertisseur simulé appelé, JPEG
    rendu) ;
  - `readTextDetectsBomAndCrlf` (`EF BB BF` + `a\r\nb` → texte `a\nb`, bom, crlf) ; `notUtf8Refused` (Latin-1,
    NUL) ; `textTooBigRefused` ;
  - `roundTripIdentical` : lire puis réécrire sans changement → mêmes octets, sur le téléphone et sur le disque ;
  - `changedSinceOpenNotSaved` : mtime ou taille différents → `saved = false, changed = true`, fichier intact ;
    `force = true` → enregistré ;
  - `failedWriteKeepsOriginal` : écriture du temporaire en échec (disque plein simulé) → original intact, aucun
    `.portail-partiel` laissé ;
  - `readOnlySourceRefused` : lecture permise avec `readOnlyReason`, écriture refusée.
- [x] **Step 2 :** Run `cd android && ./gradlew -q :app:testDebugUnitTest --tests '*FileContentTest'` → FAIL
  (classe absente).
- [x] **Step 3 :** implémenter : écriture dans `partialName(nom)` (ancien temporaire supprimé), fermeture, puis
  suppression de l'original et renommage du temporaire ; si le renommage échoue, message « Enregistré sous
  « <temporaire> » » ; HEIC par `ImageDecoder.decodeBitmap` avec `setTargetSize`. Méthodes Pigeon sur le
  dispatcher IO, erreurs par `Files.toFilesException`.
- [x] **Step 4 :** tests verts ; suite JVM verte.
- [x] **Step 5 :** commit `feat(lecteurs): lecture d'images et de textes, enregistrement sûr des textes`.

### Task 2 : « Ouvrir dans Portail » et éditeur de texte (Flutter)

**Files :** Create `lib/src/viewers.dart`, `lib/src/screens/text_editor_screen.dart`,
`test/text_editor_test.dart` ; Modify `ViewPrefs.kt` (`builtInViewers`), Pigeon `ViewPrefsMsg`,
`lib/src/screens/settings_screen.dart`, `lib/src/screens/explorer_screen.dart`, `lib/src/strings_fr.dart`.

**Interfaces :** Produces : `String? viewerFor(String name)` → `'image' | 'audio' | 'video' | 'text' | null` ;
`Future<void> openViewer(BuildContext, AppState, String sourceId, EntryMsg e, List<EntryMsg> folder)` (tâches
suivantes y branchent leur écran) ; `ViewPrefsMsg.builtInViewers` (défaut vrai) ; `TextEditorScreen(state,
sourceId, path, name)`.

- [x] **Step 1 : tests** (`text_editor_test`, faux pont) :
  - `viewerForExtensions` (casse ignorée, `.tar.gz` → null, sans extension → null) ;
  - `tapTextOpensEditor` ; réglage désactivé → `openWith` ;
  - `editAndSaveKeepsBomAndCrlf` : `writeText` reçoit le texte modifié, `bom` et `crlf` d'origine, mtime et taille
    lus ;
  - `unsavedChangesAskBeforeLeaving` (Enregistrer / Ne pas enregistrer / Annuler) ;
  - `changedFileAsksToOverwrite` : réponse `changed` → dialogue → second appel avec `force` ;
  - `readOnlyShowsReason` (champ non modifiable, bouton Enregistrer absent) ;
  - `saveFailureKeepsText` : erreur du pont → message, texte toujours à l'écran et marqué modifié ;
  - « Plus d'actions » d'un fichier seul : **Ouvrir avec…** et **Modifier comme texte**.
- [x] **Step 2 :** Run `flutter test test/text_editor_test.dart` → FAIL.
- [x] **Step 3 :** implémenter : champ multiligne en police à chasse fixe, titre avec « • » si modifié, `PopScope`
  pour la question ; réglage « Ouvrir dans Portail » dans les Réglages ; `_tap` de l'explorateur passe par
  `openViewer` quand le réglage est actif et le lecteur existe (les autres types restent sur « Ouvrir avec »
  jusqu'à leur tâche).
- [x] **Step 4 :** tests verts ; `flutter test`, `flutter analyze` et suite JVM verts.
- [x] **Step 5 :** commit `feat(lecteurs): éditeur de texte et ouverture dans Portail`.

### Task 3 : visionneuse d'images (Flutter)

**Files :** Create `lib/src/screens/image_viewer_screen.dart`, `test/image_viewer_test.dart` ; Modify
`lib/src/viewers.dart`, `lib/src/strings_fr.dart`.

**Interfaces :** Consumes : `readImage` (T1), `openViewer` (T2). Produces : `ImageViewerScreen(state, sourceId,
images: List<EntryMsg>, initialIndex)`.

- [x] **Step 1 : tests** (`image_viewer_test`) :
  - `opensAtTappedImage` : seules les images du dossier, dans l'ordre affiché ; titre « nom » et « 3 / 5 » ;
  - `swipeShowsNext` et chargement limité à l'image courante et à ses voisines (appels `readImage` comptés) ;
  - `doubleTapZooms` puis le glissement entre images est bloqué tant que l'image est agrandie ;
  - `readErrorShownOnPage` (message de l'erreur, les autres pages restent lisibles) ;
  - actions **Partager**, **Ouvrir avec…**, **Propriétés** sur l'image courante.
- [x] **Step 2 :** Run `flutter test test/image_viewer_test.dart` → FAIL.
- [x] **Step 3 :** implémenter : `PageView` + `InteractiveViewer` (échelle 1 à 8, double toucher ×2,5 au point
  touché), fond noir, barre masquée d'un toucher ; `Image.memory` décodé à au plus deux fois la largeur de l'écran
  (4096 px au plus) ; cache des octets de 3 images.
- [x] **Step 4 :** tests verts ; suites Flutter et analyse vertes.
- [x] **Step 5 :** commit `feat(lecteurs): visionneuse d'images`.

### Task 4 : accès aléatoire et fournisseur de flux

**Files :** Create `files/SeekableReader.kt`, `provider/StreamProvider.kt`,
`android/app/src/test/kotlin/com/mawena/fluluks/SeekableReaderTest.kt`, `StreamProviderTest.kt` ; Modify
`AndroidManifest.xml` (fournisseur `<applicationId>.flux`, non exporté, `grantUriPermissions`), `FileSource.kt`
(`openReadAt`, `NativeIn.Proxy`, `NativeOut.Pipe`), `Archives.kt`.

**Interfaces :** Produces :
```kotlin
class SeekableReader(val size: Long, private val open: (offset: Long) -> InputStream) : Closeable {
    val opens: Int                       // flux ouverts depuis le début (tests)
    fun read(offset: Long, buf: ByteArray, len: Int): Int   // -1 au-delà de la fin
}
interface FileSource { fun openReadAt(path: String, offset: Long): InputStream /* défaut : openRead + saut */ }
object StreamProvider { fun uriFor(src: FileSource, path: String): Uri; fun forget(sourceId: String) }
sealed class NativeIn { class Proxy(val size: Long, val open: () -> ParcelFileDescriptor) }
sealed class NativeOut { class Pipe(val sink: OutputStream, val onClosed: (commit: Boolean) -> Unit) }
```
Jeton aléatoire de 128 bits par fichier servi, gardé en mémoire ; `query` rend `DISPLAY_NAME` et `SIZE`.

- [x] **Step 1 : tests** :
  - `SeekableReaderTest` : `randomReadsMatch` (1 000 lectures à des décalages aléatoires sur 5 Mio = octets de
    référence) ; `sequentialReadsOpenOnce` ; `nearSeekSkips` (≤ 256 Kio sans réouverture) ; `farSeekReopensOnce` ;
    `closeClosesStream` ; `readerClosedWhenSourceGone` (l'ouvreur lève → erreur rendue, flux fermé) ;
  - `StreamProviderTest` (logique pure : jetons, oubli d'une source, nom et taille rendus, jeton inconnu refusé) ;
  - `ArchivesTest` : extraction depuis un `NativeIn.Proxy` et compression vers un `NativeOut.Pipe` (moteur simulé),
    tube fermé et partiel distant supprimé à l'annulation.
- [x] **Step 2 :** Run `./gradlew -q :app:testDebugUnitTest --tests '*SeekableReaderTest' --tests '*StreamProviderTest' --tests '*ArchivesTest'` → FAIL.
- [x] **Step 3 :** implémenter ; `openFile` par `openProxyFileDescriptor` (rappels sur un `HandlerThread` dédié),
  `NativeOut.Pipe` par `ParcelFileDescriptor.createPipe()` et un fil qui recopie vers le flux distant (l'écrivain
  ZIP natif n'écrit qu'en séquence, par `write`).
- [x] **Step 4 :** tests verts ; suite JVM verte.
- [x] **Step 5 :** commit `feat(distant): lecture à accès aléatoire et fournisseur de flux`.

### Task 5 : serveurs enregistrés et sources distantes

**Files :** Create `remote/RemoteServers.kt`, `remote/SecretBox.kt`, `remote/RemoteClient.kt`,
`remote/RemoteSource.kt`, `remote/Remotes.kt`, `lib/src/screens/server_form_screen.dart`,
`lib/src/widgets/server_tile.dart`, tests `RemoteServersTest.kt`, `RemoteSourceTest.kt` (avec
`FakeRemoteClient` sur un dossier temporaire), `test/remote_test.dart`, `androidTest/.../SecretBoxInstrumentedTest.kt` ;
Modify `Sources.kt`, `HostApiImpl.kt`, Pigeon, `home_screen.dart`, `AndroidManifest.xml` (`INTERNET`).

**Interfaces :** Produces :
```kotlin
data class ServerConfig(val id: String, val protocol: String /* ftp | sftp */, val name: String, val host: String,
    val port: Int, val user: String, val startPath: String, val remember: Boolean, val hostKey: String?, val keyInfo: String?)
interface SecretBox { fun seal(plain: ByteArray): ByteArray; fun open(sealed: ByteArray): ByteArray }
class KeystoreSecretBox : SecretBox      // AES-256-GCM, alias « portail-distant », AndroidKeyStore
interface RemoteClient : Closeable {
    fun list(path: String): List<Entry>; fun stat(path: String): Entry?
    fun openReadAt(path: String, offset: Long): InputStream; fun create(parent: String, name: String): OutputStream
    fun mkdir(parent: String, name: String); fun rename(path: String, newPath: String); fun delete(path: String, dir: Boolean)
}
class RemoteSource(val server: ServerConfig, val client: RemoteClient) : FileSource  // id « remote:<id> », kind « remote »
object Remotes { fun connect(serverId: String, password: ByteArray?): RemoteSource; fun disconnect(serverId: String); fun connected(): List<RemoteSource> }
class HostKeyUnknown(val fingerprint: String) : Exception(); class HostKeyChanged(val old: String, val new: String) : Exception()
```
Pigeon : `RemoteServerMsg`, `listServers`, `saveServer(msg, password?, keyPassphrase?)`, `deleteServer`,
`@async connectServer(serverId, password?)` → `SourceMsg` (erreurs : code `hostkey-unknown` avec l'empreinte,
`hostkey-changed`, `auth`), `disconnectServer`, `trustHostKey(serverId, fingerprint)`, `forgetHostKey(serverId)`.

- [x] **Step 1 : tests** :
  - `RemoteServersTest` : `serversRoundTrip` ; `passwordNotStoredByDefault` ; `secretsSealedOnDisk` (le fichier ne
    contient pas le mot de passe en clair ; ouverture par la boîte simulée) ; `deleteServerRemovesSecrets` ;
  - `RemoteSourceTest` : `hostileServerNamesSkipped` (`..`, `.`, `a/b`, NUL, vide) ; opérations de `FileSource`
    sur le faux client (lister, créer, renommer, déplacer, supprimer un dossier plein) ; `spaceUnknown` (−1, −1) ;
    `contentUriServedByStreamProvider` ;
  - `remote_test` (Flutter) : section « Dossiers distants » (serveurs, Connecter, Déconnecter), formulaire (port par
    défaut selon le protocole, avertissement FTP en clair, « Mémoriser » décoché par défaut), mot de passe demandé si
    non mémorisé, dialogue d'empreinte à la première connexion puis `trustHostKey` et nouvel essai, clé changée →
    message sans bouton de confiance ;
  - `SecretBoxInstrumentedTest` (Pixel, T11) : sceller puis ouvrir ; données altérées refusées.
- [x] **Step 2 :** Run les tests JVM et Flutter ci-dessus → FAIL.
- [x] **Step 3 :** implémenter ; fichiers `files/remote-servers.json` et `files/remote-secrets/<id>` écrits par
  fichier temporaire puis renommage ; secrets en mémoire remis à zéro à la déconnexion ; `Sources.list()` ajoute
  `Remotes.connected()`.
- [x] **Step 4 :** tests verts ; suites JVM, Flutter et analyse vertes.
- [x] **Step 5 :** commit `feat(distant): serveurs enregistrés et sources distantes`.

### Task 6 : dossiers distants dans le gestionnaire

**Files :** Modify `Files.kt`, `Trash.kt`, `Transfers.kt`, `Archives.kt`, `Thumbnails.kt`, `HostApiImpl.kt`,
`explorer_screen.dart`, `source_tile.dart`, `strings_fr.dart` ; tests `RemoteFilesTest.kt` (faux client),
`test/remote_test.dart`.

- [x] **Step 1 : tests** :
  - `deleteOnRemoteIsPermanent` (pas de `.Portail-corbeille` créé) ; boîte de suppression « définitive » pour une
    source `remote` ; pas de bouton Corbeille ;
  - `copyBothWaysWithRemote` (téléphone → distant → disque en mémoire, octets égaux, annulation : partiel distant
    supprimé) ; `unknownSpaceNotChecked` ;
  - `extractFromRemoteAndCompressToRemote` (via `NativeIn.Proxy` / `NativeOut.Pipe`, moteur simulé) ;
  - `remoteThumbnailsImagesOnly` (vidéo : aucune ; image > 8 Mio : aucune) ;
  - tuile d'un serveur connecté : « Espace inconnu ».
- [x] **Step 2 :** tests → FAIL.
- [x] **Step 3 :** implémenter.
- [x] **Step 4 :** tests verts ; suites vertes.
- [x] **Step 5 :** commit `feat(distant): dossiers distants dans le gestionnaire`.

### Task 7 : FTP

**Files :** Create `remote/FtpClient.kt`, `FtpClientTest.kt` ; Modify `build.gradle.kts` (`commons-net:commons-net:
3.13.0`, test `org.apache.ftpserver:ftpserver-core:1.2.1`), `Remotes.kt`.

- [x] **Step 1 : tests** (`FtpClientTest`, Apache FtpServer embarqué sur 127.0.0.1, port libre, utilisateur enfermé
  dans un dossier temporaire) : `listsWithAccents` (UTF-8) ; `latin1ListingDecoded` (serveur sans `UTF8` dans
  `FEAT`) ; `readAtOffsets` ; `uploadThenRename` ; `mkdirAndRecursiveDelete` ; `anonymousLogin` ; `badPasswordMessage`
  (« Identifiant ou mot de passe refusé ») ; `connectionDropMidRead` (serveur arrêté → « Connexion perdue »,
  connexion rendue) ; `poolNeverExceedsThree`.
- [x] **Step 2 :** Run `./gradlew -q :app:testDebugUnitTest --tests '*FtpClientTest'` → FAIL.
- [x] **Step 3 :** implémenter (`REST` + `retrieveFileStream`, `completePendingCommand` à la fermeture ;
  `storeFileStream` ; `MLSD`/`LIST`).
- [x] **Step 4 :** tests verts ; suite JVM verte.
- [x] **Step 5 :** commit `feat(distant): dossiers FTP`.

### Task 8 : SFTP

**Files :** Create `remote/SftpClient.kt`, `remote/PrivateKeys.kt`, `SftpClientTest.kt` ; Modify
`build.gradle.kts` (`com.github.mwiede:jsch:2.28.8`, `org.bouncycastle:bcprov-jdk18on:1.86`, tests
`org.apache.sshd:sshd-core:2.20.0`, `sshd-sftp:2.20.0`), `proguard-rules.pro`, `MainActivity.kt` (sélecteur de
fichier pour la clé), Pigeon (`importPrivateKey(serverId)` → type et empreinte), `server_form_screen.dart`.

- [x] **Step 1 : tests** (`SftpClientTest`, serveur MINA SSHD embarqué) : `unknownHostKeyAskedOnce` (empreinte
  `SHA256:…` rendue, puis connexion après `trust`) ; `changedHostKeyRefused` (même port, autre clé de serveur) ;
  `passwordAuth` ; `ed25519KeyAuth` et `encryptedKeyNeedsPassphrase` ; `readAtOffsets` ; `uploadRenameDelete` ;
  `symlinkListedNotFollowed` ; `badCredentialsMessage` ; `channelsNeverExceedThree` ; `secretsNotInMessages`.
- [x] **Step 2 :** Run `./gradlew -q :app:testDebugUnitTest --tests '*SftpClientTest'` → FAIL.
- [ ] **Step 3 :** implémenter (`HostKeyRepository` maison, `get(path, null, offset)`, `put(…, OVERWRITE)`,
  `ServerAliveInterval` 30 s) ; règles R8 ; construction release vérifiée.
- [x] **Step 4 :** tests verts ; suite JVM verte ; `flutter build apk --release` (si permis) ou `--debug`.
- [x] **Step 5 :** commit `feat(distant): dossiers SFTP`.

### Task 9 : lecteur vidéo

**Files :** Create `media/VideoActivity.kt`, `media/Subtitles.kt`, `SubtitlesTest.kt` ; Modify
`build.gradle.kts` (`media3-exoplayer`, `media3-ui` 1.11.1), `AndroidManifest.xml`, Pigeon (`playVideo(sourceId,
path)`), `viewers.dart`.

- [x] **Step 1 : tests** : `SubtitlesTest` : `findsSameNameAndLanguages` (`film.srt`, `film.fr.srt`,
  `film.eng.srt`, casse ignorée, `autre.srt` exclu) ; `windows1252Decoded` ; `dataUriRoundTrip` ; Flutter :
  toucher une vidéo appelle `playVideo`.
- [x] **Step 2 :** tests → FAIL.
- [x] **Step 3 :** implémenter : activité plein écran paysage-capteur, `PlayerView` (recul et avance 10 s, bouton
  des sous-titres), écran gardé allumé pendant la lecture ; erreur de lecture ou stockage fermé → message et
  fermeture.
- [x] **Step 4 :** tests verts ; suites vertes.
- [x] **Step 5 :** commit `feat(lecteurs): lecteur vidéo avec sous-titres`.

### Task 10 : lecteur de musique

**Files :** Create `media/MusicService.kt`, `media/Music.kt`, `MusicTest.kt`,
`lib/src/screens/now_playing_screen.dart`, `lib/src/widgets/mini_player.dart`, `test/music_test.dart` ; Modify
`build.gradle.kts` (`media3-session`), `AndroidManifest.xml` (service `mediaPlayback`,
`FOREGROUND_SERVICE_MEDIA_PLAYBACK`), Pigeon (`playAudio(sourceId, paths, startIndex)`, `playerCommand(command,
positionMs?)`, événement `onPlayerChanged(PlayerMsg?)`), `app_state.dart`, `home_screen.dart`, `explorer_screen.dart`.

- [x] **Step 1 : tests** : `MusicTest` : `queueFromFolder` (fichiers audio seulement, ordre affiché, départ sur le
  fichier touché) ; `stopsWhenSourceGone` (source retirée → arrêt, file vidée, message « Lecture arrêtée : stockage
  fermé ») ; Flutter : barre réduite (titre, lecture/pause), écran « Lecture en cours » (position, durée, précédent,
  suivant, curseur), disparition à l'arrêt.
- [x] **Step 2 :** tests → FAIL.
- [x] **Step 3 :** implémenter : ExoPlayer avec focus audio et pause au débranchement des écouteurs, notification
  Media3 par défaut, `MediaController` côté pont, position publiée toutes les 500 ms.
- [x] **Step 4 :** tests verts ; suites vertes.
- [x] **Step 5 :** commit `feat(lecteurs): lecteur de musique en arrière-plan`.

### Task 11 : essais instrumentés sur le Pixel

**Files :** Create `androidTest/.../ReadersInstrumentedTest.kt`, `StreamProviderInstrumentedTest.kt`,
`SshCryptoInstrumentedTest.kt`, `SecretBoxInstrumentedTest.kt`.

- [x] `ReadersInstrumentedTest` : WAV généré par le test lu par le lecteur de musique (état « lecture », position
  qui avance), vidéo MP4 de référence (fixture poussée sous `/data/local/tmp/fluluks/fixtures/`) lue par
  `VideoActivity` avec un sous-titre en Windows-1252 ; `StreamProviderInstrumentedTest` : lectures à des décalages
  aléatoires par un vrai descripteur mandataire ; `SshCryptoInstrumentedTest` : Ed25519 et Curve25519 de JSch avec
  Bouncy Castle sur ART ; `SecretBoxInstrumentedTest`. Téléphone au repos vérifié avant, APK de l'utilisateur
  sauvegardée puis remise.
- [x] Commit `test(lecteurs): essais instrumentés sur le Pixel`.

### Task 12 *(utilisateur)* : essais

**Files :** Create `docs/superpowers/research/2026-10-23-essais-phase10/essais.md`.

- [ ] Fiche d'essais : photos (glisser, zoom, HEIC si possible), musique d'un disque chiffré écran éteint puis
  débranchement, vidéo avec `.srt` accentué, texte modifié sur ext4 puis vérifié sous Ubuntu, serveur FTP
  (`python3 -m pyftpdlib`) et SFTP (`openssh-server` d'Ubuntu, par mot de passe puis par clé) : parcourir, copier
  dans les deux sens, ouvrir une vidéo distante et l'avancer, extraire une archive distante. APK installée avant.

### Task 13 : documentation et v1.3.0

- [ ] README (lecteurs, dossiers distants, limites : pas de corbeille distante, FTP en clair, liens non suivis ;
  licences des nouvelles bibliothèques), feuille de route cochée ; commit `docs: lecteurs intégrés et dossiers
  distants`.
- [ ] Relecture finale de toute la branche, puis fusion `feature/lecteurs-distants` → `develop`, `release/1.3.0`
  (pubspec `1.3.0+10`), `main`, étiquette signée `v1.3.0`. Pas de push sans l'accord de l'utilisateur.
