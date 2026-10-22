# Portail — Phase 7 : robustesse et performances — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** rendre Portail sûr au quotidien, pour la version **v1.0.0** :
- un **nouveau logo orange**, plus représentatif (demande du 2026-10-22) ;
- **verrouillage automatique** des disques chiffrés après un délai sans activité, choisi dans les
  Réglages (désactivé par défaut) ;
- **débranchement inattendu** : un conseil adapté au système de fichiers (ext4, FAT/exFAT, NTFS), et une
  copie interrompue toujours signalée, même depuis l'accueil ;
- **performances du § 5 mesurées** sur le Pixel, et le débit affiché à la fin de chaque copie ;
- les **points mineurs** laissés par la relecture de la phase 6 ;
- l'**essai d'ext4 en écriture sur le vrai disque** (tâche 13 de la phase 3), fait par l'utilisateur.

**Architecture :**
- **Moteur natif :** une seule modification. `fj_statfs` (`native/jni/handles.c`) ajoute la clé `"fs"`
  (nom de `fl_content_name`, par exemple `ext4`, `FAT32`, `exFAT`, `NTFS`) ; `jvol` garde le
  `fl_content` du montage.
- **Kotlin :**
  - `VolumeState` gagne `fs: String` et `encrypted: Boolean` ;
  - nouvel objet `volumes/AutoLock.kt` : la règle du verrouillage, testable sur la JVM ;
    `VolumeService` l'applique toutes les 30 s tant qu'un volume est ouvert ;
  - `VolumeManager.detach` choisit son avertissement selon `fs` ;
  - `Transfers` mesure la durée de la copie et écrit d'abord sous un nom temporaire.
- **Flutter :** réglage du verrouillage, barre de copie aussi sur l'accueil, résultat d'une copie montré
  une seule fois, débit affiché ; corrections de l'explorateur.
- **Pigeon :** `getAutoLockMinutes`, `setAutoLockMinutes`, `TransferMsg.elapsedMs`, et `openWith` /
  `share` deviennent `@async`.

**Tech Stack :** C11 (JNI), Kotlin et coroutines, Flutter 3.41.4 (Material 3), Pigeon 27.3.0. Tests :
ctest sur le PC, JUnit sur la JVM, tests instrumentés sur le Pixel, `flutter test`.

**Spec :** [docs/superpowers/specs/2026-10-17-fluluks-design.md](../specs/2026-10-17-fluluks-design.md)
(§ 4.2, § 4.3 « Débranchement », § 4.4 « Réglages », § 5, § 7, § 10, § 11, § 12).

## Global Constraints

- **Branche et commits.**
  - Branche `feature/robustesse`, créée depuis `develop`.
  - Commits sous l'identité git de l'utilisateur (`mawena <gamligocharles@gmail.com>`), en français
    (Conventional Commits), **sans aucune attribution à Claude**. Commiter en nommant les fichiers
    (`git commit -- <fichiers>`) : l'index peut contenir des fichiers de l'utilisateur.
  - Un commit par tâche, plus les commits de correction. La tâche est cochée dans la feuille de route
    (section Phase 7) **dans le même commit**.
- **Disques USB : aucune règle du § 4.2 relâchée.** Le verrouillage automatique passe par
  `VolumeManager.eject` (synchronisation, démontage, `SYNCHRONIZE CACHE`, clé effacée), jamais par un
  démontage forcé direct.
- **Téléphone de l'utilisateur.**
  - L'app et les tests n'écrivent que sous `/data/local/tmp/fluluks/`, dans les dossiers de l'application
    et dans `Download/portail-tests/` (supprimé par les tests).
  - **Aucune capture d'écran** du téléphone : vérifier l'interface par des tests de rendu dans l'app.
  - Débrancher pendant une écriture : **seulement avec une clé USB sacrifiée**, jamais le vrai disque.
  - Le vrai disque n'est écrit qu'après une sauvegarde par l'utilisateur.
  - Appareil : `adb devices` avant tout (USB `98291FFBA003RN`, ou Wi-Fi selon le lieu).
- **Langue :** interface et messages en français ; jamais de chemin absolu ni de texte anglais dans un
  message affiché.
- **Avant tout essai manuel :** APK de la branche construite et installée sur le Pixel, `versionName`
  vérifié.

## Review Focus

1. **Verrouillage pendant un usage réel** : vidéo en lecture continue dans VLC, copie en cours, écriture
   d'une autre application par le provider. Attendu : jamais de verrouillage tant que des lectures ou des
   écritures arrivent, ni pendant une copie ; le délai repart de la dernière activité.
   *Tests : T3 `activityPostponesLock`, `neverDuringTransfer`.*
2. **Verrouillage d'un volume qui ne peut pas s'éjecter** (fichier ouvert, disque qui refuse la
   synchronisation). Attendu : l'éjection suit ses règles (fermeture forcée après synchronisation, ou
   « Éjection impossible ») ; pas de nouvelle tentative toutes les 30 s qui inonde les notifications.
   *Tests : T3 `failedLockNotRetriedUntilNewActivity`.*
3. **Débranchement pendant un déplacement disque → téléphone.** Attendu : aucune perte (la source reste
   sur le disque, le partiel du téléphone est supprimé ou reste sous son nom temporaire), message
   `MSG_SOURCE_GONE` visible sur l'accueil. *Tests : T5 `detachDuringMoveKeepsSource`, T6
   `homeShowsInterruptedTransfer`.*
4. **Processus tué pendant une copie** (pas de service au premier plan entre téléphone et carte SD).
   Attendu : aucun fichier incomplet sous son vrai nom. *Tests : T7 `partialHasTemporaryName`.*
5. **Réglage du délai changé pendant qu'un disque est ouvert.** Attendu : appliqué au prochain contrôle,
   sans rouvrir le disque ; « Jamais » arrête tout verrouillage en cours de décompte.
   *Tests : T3 `settingReadAtEachCheck`.*

---

### Task 1 : logo orange et icône plus représentative *(demande du 2026-10-22)*

**Files :** Modify `assets/logo/portail.svg`, les drawables de l'icône adaptative (`ic_launcher_background`,
`ic_launcher_foreground`, `ic_launcher_monochrome`), `ic_portail` (notification),
`tools/logo/render_icons.py` si besoin, les PNG rendus, la spec (§ 11, ligne « Logo »).
Test : `androidTest/.../AppIdentityTest.kt`.

**Dessin** *(choisi par le planificateur, à la demande de l'utilisateur)* :
- **fond** : dégradé orange terracotta, `#E07B53` en haut à gauche → `#C2410C` en bas à droite ;
- **premier plan, blanc** : une **arche de portail** (montants et arc en plein cintre, trait épais,
  arrondi), qui encadre un **dossier** (onglet en haut à gauche) ; en bas à droite, un petit **cadenas**
  blanc à serrure orange qui déborde de l'arche. Sens : l'accès (portail), aux fichiers (dossier), même
  chiffrés (cadenas). Rien n'imite le logo de Claude : seule la couleur rappelle sa signature ;
- **monochrome** (Android 13, icônes à thème) : la même silhouette, cadenas en creux ;
- **notification** `ic_portail` : arche et cadenas seulement, une couleur, lisible à 24 dp ;
- tout tient dans la zone sûre de l'icône adaptative (cercle de 66 dp sur 108 dp).

- [ ] **Step 1 : test qui échoue** : `AppIdentityTest.iconIsOrange` : l'icône dessinée (`cacheDir/icone.png`)
  a, au centre du fond (hors premier plan), une teinte entre 10° et 30° (orange) ; les tests existants
  (`iconIsAdaptiveWithMonochrome`, nom « Portail ») restent verts.
- [ ] **Step 2 : lancer** sur le Pixel. Expected : FAIL (fond indigo).
- [ ] **Step 3 : dessiner** le SVG, le rendre avec librsvg (`tools/logo/render_icons.py`), puis transcrire
  en vector drawables. Vérifier le rendu à 48, 108 et 512 px sur le PC (images de travail dans le
  dossier temporaire, jamais une capture du téléphone).
- [ ] **Step 4 : vérifier** : `AppIdentityTest` sur le Pixel, APK installée.
- [ ] **Step 5 : commit** `feat(logo): logo orange, arche de portail avec dossier et cadenas`.

### Task 2 : système de fichiers et chiffrement de chaque volume

**Files :** Modify `native/jni/handles.c`, `engine/EngineModels.kt` (`VolumeStat.fs`),
`volumes/DiskState.kt`, `volumes/VolumeManager.kt` (`mountLocked`), le `MemEngine` des tests JVM.
Test : `native/tests/` (test JNI existant le plus proche, ou `EngineTest` instrumenté),
`VolumeManagerTest.kt`.

**Interfaces :**
- Produces : `VolumeStat.fs: String` (clé JSON `"fs"`, `""` si absente) ;
  `VolumeState.fs: String`, `VolumeState.encrypted: Boolean` (vrai si ouvert par `unlock`, c'est-à-dire
  avec une phrase de passe).

- [ ] **Step 1 : tests qui échouent.**
  - `EngineTest.statfsGivesFsName` (Pixel) : `ext4-rw-reel.img` → `"ext4"` ; une image FAT32 des
    fixtures → `"FAT32"`.
  - `VolumeManagerTest.unlockMarksEncryptedAndFs` : `unlock` → `encrypted = true`, `fs = "ext4"` ;
    `mountPlain` → `encrypted = false`.
- [ ] **Step 2 : lancer.** Expected : FAIL (clé absente, champs absents).
- [ ] **Step 3 : implémenter.** `jvol.kind` posé au montage (`pr.content`), écrit par `fj_statfs` après
  `needs_recovery`.
- [ ] **Step 4 : vérifier** : ctest rapide (`-LE lent`), JVM, `EngineTest` sur le Pixel.
- [ ] **Step 5 : commit** `feat(volumes): système de fichiers et chiffrement connus pour chaque volume`.

### Task 3 : verrouillage automatique (Kotlin)

**Files :** Create `volumes/AutoLock.kt`, `test/.../AutoLockTest.kt`. Modify `service/VolumeService.kt`,
`bridge/HostApiImpl.kt`, `pigeons/fluluks_api.dart` (puis génération).

**Interfaces :**
- Consumes : `VolumeManager.lastActivity(id)`, `VolumeManager.volumes`, `VolumeState.encrypted` (T2),
  `Transfers.isBusy(id)`.
- Produces :
  - `object AutoLock` :
    - `CHOICES = listOf(0, 5, 15, 30, 60)` (minutes ; 0 = jamais, valeur par défaut) ;
    - `var minutes: () -> Int` (lu à chaque contrôle ; branché sur les préférences) ;
    - `fun due(now: Long): List<String>` : volumes chiffrés dont `now - lastActivity ≥ minutes`, sans
      copie en cours ; un volume déjà verrouillé sans succès n'est rendu à nouveau qu'après une
      nouvelle activité ;
    - `fun check(now: Long)` : `VolumeManager.eject(id)` pour chacun, puis l'avertissement
      `msgLocked(minutes)` = « Disque verrouillé après $minutes min sans activité ».
  - Pigeon : `int getAutoLockMinutes()`, `void setAutoLockMinutes(int minutes)` (valeur hors
    `CHOICES` → 0) ; préférence `verrouillage_auto` dans les préférences existantes.
- *(décision du planificateur)* Seuls les volumes **chiffrés** sont verrouillés : un volume en clair n'a
  pas de clé à effacer, et l'éjecter seul surprendrait. L'utilisateur peut demander l'inverse à la
  relecture de ce plan.

- [ ] **Step 1 : tests qui échouent** (`AutoLockTest`, `VolumeManager.resetForTests` avec horloge
  injectée et deux volumes, l'un chiffré, l'autre en clair) :
  - `offByDefault` ; `locksEncryptedAfterTimeout` (15 min → éjecté à 15 min, pas à 14 min 59 s) ;
    `plainNeverLocked` ; `activityPostponesLock` ; `neverDuringTransfer` ;
    `failedLockNotRetriedUntilNewActivity` ; `settingReadAtEachCheck` ;
  - `invalidSettingMeansNever` (`setAutoLockMinutes(7)` → 0, testé sur la fonction de validation).
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.** `VolumeService` : boucle toutes les 30 s (coroutine du service, arrêtée
  avec lui) qui appelle `AutoLock.check(SystemClock.elapsedRealtime())`.
- [ ] **Step 4 : vérifier** : JVM, `flutter analyze`.
- [ ] **Step 5 : commit** `feat(volumes): verrouillage automatique des disques chiffrés`.

### Task 4 : réglage du verrouillage (Flutter)

**Files :** Modify `lib/src/screens/settings_screen.dart`, `lib/src/state/app_state.dart`,
`lib/src/strings_fr.dart`, `test/fake_host_api.dart`, `test/settings_screen_test.dart`.

**Interfaces :**
- Consumes : `getAutoLockMinutes` / `setAutoLockMinutes` (T3).
- Produces : dans les Réglages, une ligne `Fr.autoLock` = « Verrouillage automatique », sous-titre
  `Fr.autoLockHelp` = « Disques chiffrés, après ce délai sans activité », et un menu déroulant
  Jamais / 5 min / 15 min / 30 min / 1 h. `AppState.autoLockMinutes`, lu dans `refresh()`.

- [ ] **Step 1 : tests qui échouent** : `autoLockShowsCurrentValue` (15 → « 15 min ») ;
  `autoLockCallsApi` (choisir « 1 h » → `setAutoLockMinutes(60)`) ; à 412 dp de large, pas de
  débordement.
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** : `flutter analyze`, `flutter test`.
- [ ] **Step 5 : commit** `feat(réglages): verrouillage automatique après un délai`.

### Task 5 : débranchement inattendu — conseil selon le système de fichiers

**Files :** Modify `volumes/VolumeManager.kt` (`detach`), `test/.../VolumeManagerTest.kt`,
`test/.../TransfersTest.kt`.

**Interfaces :**
- Consumes : `VolumeState.fs` (T2).
- Produces : `VolumeManager.detachAdvice(fs: String): String`, avertissement envoyé par `detach` quand
  un volume avait des écritures non synchronisées (le message actuel `MSG_DETACH_DURING_WRITE` est
  remplacé) :
  - `ext4` : « Disque débranché pendant une écriture : branchez-le sous Ubuntu, le journal sera rejoué
    au montage ; en cas de doute, lancez e2fsck. »
  - `FAT12`/`FAT16`/`FAT32`/`exFAT` : « Disque débranché pendant une écriture : faites-le vérifier par un
    ordinateur (chkdsk sous Windows, fsck.fat ou fsck.exfat sous Linux). Portail l'ouvrira en lecture
    seule d'ici là. »
  - `NTFS` : « Disque débranché pendant une écriture : branchez-le sous Windows, qui le vérifiera, puis
    éjectez-le proprement. »
  - autre ou vide : le texte ext4 sans la phrase sur le journal, « faites-le vérifier sur un ordinateur ».
  - Plusieurs volumes concernés : un seul avertissement, celui du premier volume écrit.

- [ ] **Step 1 : tests qui échouent** : `detachAdviceByFs` (les quatre cas) ;
  `detachWarnsWithFsAdvice` (volume FAT32 écrit puis débranché → texte FAT) ;
  `detachWithoutWritesSilent` ; `TransfersTest.detachDuringMoveKeepsSource` (déplacement disque →
  téléphone, débranchement au 3ᵉ `pread` : source intacte sur le disque, `MSG_SOURCE_GONE`).
- [ ] **Step 2 : lancer.** Expected : FAIL (sauf `detachWithoutWritesSilent`, qui fixe l'existant).
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** : JVM.
- [ ] **Step 5 : commit** `feat(volumes): conseil adapté au système de fichiers après un débranchement`.

### Task 6 : copies — résultat sur l'accueil, montré une fois, avec le débit

**Files :** Modify `files/Transfers.kt`, `pigeons/…` (`TransferMsg.elapsedMs`), `HostApiImpl.kt`,
`lib/src/state/app_state.dart`, `lib/src/widgets/transfer_bar.dart`, `lib/src/screens/home_screen.dart`,
`lib/src/strings_fr.dart`. Tests : `TransfersTest.kt`, `test/home_screen_test.dart`,
`test/explorer_screen_test.dart`.

**Interfaces :**
- Produces :
  - `TransferState.elapsedMs: Long` (horloge injectée de `Transfers`, du début à la fin) ;
  - `AppState.transferResultSeen()` : appelé quand la barre a montré le résultat ; ensuite
    `AppState.transfer` vaut null pour tous les écrans ;
  - `Fr.transferDoneRate(int n, String rate)` = « Terminé : $n fichier(s), $rate » avec
    `rate` = « 34 Mo/s » (octets / durée, arrondi à l'entier ; absent sous 1 s ou sous 1 Mo) ;
  - la barre de copie (progression, Annuler, résultat) existe aussi sur l'accueil.

- [ ] **Step 1 : tests qui échouent** :
  - `TransfersTest.elapsedMeasured` (horloge factice : 2 000 ms) ;
  - `homeShowsInterruptedTransfer` : transfert fini avec `MSG_SOURCE_GONE` → visible sur l'accueil ;
  - `transferResultShownOnce` : résultat vu dans un explorateur, puis un autre explorateur ouvert →
    plus de résultat ;
  - `transferDoneShowsRate` : 100 Mo en 4 s → « 25 Mo/s ».
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** : JVM, `flutter analyze`, `flutter test`.
- [ ] **Step 5 : commit** `feat(copies): résultat visible partout, une seule fois, avec le débit`.

### Task 7 : copies — nom temporaire et noms longs

**Files :** Modify `files/Transfers.kt`, `provider/DocNames.kt`. Tests : `TransfersTest.kt`,
`DocNamesTest.kt`.

**Interfaces :**
- Produces :
  - chaque fichier est écrit sous `.<nom>.portail-partiel` puis renommé en son nom final (unique dans le
    dossier) seulement après la vérification de taille ; un échec supprime le temporaire si la
    destination répond encore ;
  - `DocNames.unique` raccourcit le nom de base (en gardant l'extension, sans couper un caractère UTF-8)
    quand le nom numéroté dépasserait 255 octets, au lieu de rendre null.

- [ ] **Step 1 : tests qui échouent** :
  - `partialHasTemporaryName` : pendant la copie (hook `onPwrite`), le dossier de destination ne
    contient que `.a.bin.portail-partiel` ; à la fin, seulement `a.bin` ;
  - `failureRemovesTemporary` (EFBIG au milieu) ;
  - `copyLongNameTwice` : nom de 300 octets copié deux fois dans le même dossier FAT → deux fichiers,
    noms ≤ 255 octets, pas d'échec ;
  - `DocNamesTest.uniqueShortensLongNames`.
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** : JVM.
- [ ] **Step 5 : commit** `fix(copies): nom temporaire pendant l'écriture, noms longs rendus uniques`.

### Task 8 : points mineurs de la relecture de la phase 6

**Files :** Modify `files/Thumbnails.kt`, `files/Search.kt`, `files/LocalSource.kt`, `files/Sources.kt`,
`HostApiImpl.kt` et `pigeons/…` (`openWith`, `share` en `@async`), `lib/src/screens/explorer_screen.dart`,
`lib/src/screens/home_screen.dart`, `lib/src/state/app_state.dart`. Tests : `ThumbnailsTest.kt`,
`SearchTest.kt`, `LocalSourceTest.kt`, `test/explorer_screen_test.dart`, `test/home_screen_test.dart`.

**Interfaces / comportements attendus :**
- vignette dont le calcul finit après l'éjection : non mise en cache ; à la fermeture d'un volume,
  `PaintingBinding.instance.imageCache.clear()` côté Flutter ;
- recherche : un dossier illisible d'un disque (`EngineError`) est sauté, comme un dossier protégé ;
- lien symbolique du téléphone qui pointe hors de la racine : renommable et supprimable (le lien
  lui-même, jamais sa cible) ;
- `openWith` et `share` hors du fil principal ;
- stockage amovible d'Android : libellé d'Android gardé, type `removable` et icône générique au lieu de
  « carte SD » *(décision du planificateur : Android ne dit pas si c'est une carte ou une clé)* ;
- téléphone sans accès : la raison affichée est `MSG_NO_ACCESS`, pas « lecture seule » ;
- explorateur : un seul message « Le disque a été fermé » même avec plusieurs explorateurs empilés ;
  sélection nettoyée des éléments disparus après un rechargement ; presse-papiers vidé quand sa source
  disparaît ;
- favori d'un disque sans UUID de partition : étoile désactivée (il ne se retrouverait jamais) ;
- favori ajouté dans un disque **chiffré** : confirmation « Le nom de ce dossier restera visible sur
  l'accueil, même disque verrouillé » *(décision du planificateur, à valider par l'utilisateur)*.

- [ ] **Step 1 : tests qui échouent**, un par point : `thumbnailNotCachedAfterDrop`,
  `searchSkipsUnreadableDiskFolder`, `deleteAndRenameLinkPointingOutside`, `noAccessReason`,
  `oneClosedMessageForStackedExplorers`, `selectionPrunedAfterReload`, `clipboardClearedWhenSourceGone`,
  `favoriteDisabledWithoutPartUuid`, `encryptedFavoriteAsksConfirmation`, `imageCacheClearedOnClose`,
  `removableKindForAndroidVolumes` (sur la fonction de classement, sans `StorageManager`).
- [ ] **Step 2 : lancer.** Expected : FAIL.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** : JVM, instrumenté (`FilesInstrumentedTest` pour `openWith`/`share`),
  `flutter analyze`, `flutter test`.
- [ ] **Step 5 : commit** `fix: points mineurs de la relecture de la phase 6`.

### Task 9 : performances du § 5 mesurées sur le Pixel

**Files :** Create `androidTest/.../PerfInstrumentedTest.kt`, `tools/android/run_phase7_tests.sh`
(sur le modèle de `run_phase6_tests.sh`, journaux dans `docs/superpowers/research/2026-10-22-essais-phase7/`).

**Interfaces :** aucune. Le test écrit ses mesures dans le journal (`Log.i("PortailPerf", …)`) ; le
script les recueille.

- [ ] **Step 1 : tests** (ils échouent si le seuil n'est pas tenu) :
  - `list1000FilesUnder2s` : 1 000 fichiers créés par `DiskSource` dans une copie de `ext4-rw-reel.img`,
    puis `Files.list` : moins de **2 s** (§ 5) ;
  - `listPhoneFolder1000Under2s` : même chose dans `Download/portail-tests/` ;
  - `copyThroughput` : 64 Mio téléphone → image, puis image → téléphone ; débit journalisé, sans seuil
    (le § 5 n'en fixe pas pour l'écriture) ;
  - `sequentialReadAtLeast20MBps` : lecture de 64 Mio par le provider : au moins **20 Mo/s** sur image.
- [ ] **Step 2 : lancer** `tools/android/run_phase7_tests.sh <serial>`. Expected : PASS, mesures dans le
  dossier d'essais.
- [ ] **Step 3 :** si un seuil n'est pas tenu : superpowers:systematic-debugging, puis correction avec
  son test.
- [ ] **Step 4 : commit** `test(performances): mesures du § 5 sur le Pixel`, avec le journal des mesures.

### Task 10 *(utilisateur)* : essais sur le Pixel, dont ext4 en écriture sur le vrai disque

APK de la branche installée avant. Fiche d'essai à cocher dans
`docs/superpowers/research/2026-10-22-essais-phase7/essais.md` :
- [ ] **Sauvegarde du vrai disque faite** (préalable à tout le reste du vrai disque).
- [ ] Vrai disque, ext4 en écriture : copier 1 Gio du téléphone vers le disque (noter le débit affiché),
  le recopier vers le téléphone, renommer, supprimer, éjecter. Puis, sous Ubuntu : `sudo e2fsck -fn` sur
  le volume déverrouillé, sans erreur, et les fichiers lisibles.
- [ ] Verrouillage automatique réglé à 5 min : disque chiffré ouvert, rien touché → verrouillé avec la
  notification ; vidéo en lecture dans VLC → pas verrouillé.
- [ ] **Clé USB sacrifiée seulement** : débrancher pendant une copie vers la clé → conseil adapté à son
  système de fichiers, copie signalée interrompue sur l'accueil, pas de fichier incomplet sous son vrai
  nom.
- [ ] Explorateur : `Android/data` affiche « Dossier protégé par Android ».

### Task 11 : documentation et v1.0.0

- [ ] **README :** verrouillage automatique, conseil après débranchement, mesures du § 5 (avec les
  chiffres), limite levée « écriture non essayée sur un vrai disque » si T10 est passée, feuille de
  route ; feuille de route cochée.
- [ ] **Commit** `docs: robustesse et performances, version 1.0.0`.
- [ ] Relecture finale de toute la branche, puis fusion `feature/robustesse` → `develop`. Ensuite
  `release/1.0.0` (pubspec `1.0.0+7`), fusion dans `main`, étiquette signée `v1.0.0`, retour dans
  `develop`. Pas de push sans l'accord de l'utilisateur.
