# FluLuks — Phase 2 : Android en lecture (jalon « mon disque dans VLC ») — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** une application Android (Flutter + Kotlin + moteur natif de la phase 1) qui, sur le Pixel 4 XL
non rooté, ouvre un disque USB-C (LUKS2/ext4 ou ext4 en clair) **en lecture seule** et l'expose dans
l'application « Fichiers » via un DocumentsProvider, de sorte qu'une vidéo s'ouvre dans VLC avec avance
rapide.

**Architecture :**
- **Natif.** On ajoute au moteur C de la phase 1 :
  - un périphérique bloc USB (Bulk-Only Transport + SCSI) sur un transport abstrait, branché sur usbdevfs
    via le descripteur fourni par Android ;
  - un cache de blocs ;
  - une couche JNI à poignées.
- **Kotlin.** Kotlin gère la détection USB et la permission, un gestionnaire de volumes, un service au
  premier plan et le DocumentsProvider.
- **Flutter.** Flutter n'est qu'une interface au-dessus, reliée par Pigeon.

**Tech Stack :**
- Flutter 3.41.4 / Dart 3.11.1, Kotlin 2.2.20, Gradle 8.14, AGP 8.11.1.
- Android SDK 36 (compile/target), minSdk 26, NDK 28.2.13676358, CMake 3.22.1 (SDK).
- Pigeon 29.0.6, Material 3.
- Moteur C11 de la phase 1 (mbedTLS, Argon2, cJSON, lwext4 patché).

**Spec :** [docs/superpowers/specs/2026-10-17-fluluks-design.md](../specs/2026-10-17-fluluks-design.md) (§ 3, § 4, § 5, § 6 étape 4)

**Suivi :** cocher la tâche dans [la feuille de route](2026-10-17-fluluks-feuille-de-route.md) dans le
commit de fin de chaque tâche.

## Global Constraints

- Branche `feature/android-lecture` depuis `develop`. Les commits portent l'identité git de
  l'utilisateur, avec un message en français et **sans aucune attribution à Claude**. Un commit par
  tâche, plus les commits de correction.
- `applicationId`/espace de noms `com.mawena.fluluks`, nom affiché « FluLuks »
  *(décision du planificateur)*. Autorité du provider : `com.mawena.fluluks.documents`.
- `minSdk 26`, `compileSdk 36`, `targetSdk 36`, `ndkVersion "28.2.13676358"`, `abiFilters` = `arm64-v8a`,
  `x86_64`. Le code natif est compilé avec `-O2`, y compris dans la variante debug : Argon2 est 10 fois
  plus lent sans optimisation.
- SDK Android dans `~/Android`, comme le déclare déjà `~/.bashrc`. Avant T1, il faut **au moins 10 Gio
  libres** sur `/`.
- **Phase 2 = lecture seule.** Toute ouverture en écriture renvoie une erreur claire. Le disque USB est
  ouvert avec `read_only = true` jusqu'au bloc SCSI : aucune commande `WRITE` n'est jamais envoyée.
- La phrase de passe ne circule que sous forme d'**octets UTF-8** (`Uint8List` en Dart, `ByteArray` en
  Kotlin, `uint8_t*` en C). Chaque copie est mise à zéro juste après l'appel natif. Elle n'apparaît
  jamais dans `Log`, une exception, l'état Flutter ou une notification. La clé maîtresse ne quitte
  jamais le natif.
- Les noms de fichiers traversent JNI en **octets UTF-8** (JSON en `byte[]`), jamais via
  `NewStringUTF`, car les émojis sont invalides en « modified UTF-8 ».
- Interface entièrement en **français**. Les messages d'erreur viennent de `fl_strerror` (phase 1) ou
  des chaînes de `lib/src/strings_fr.dart`.
- Toute entrée disque reste **hostile** (règle de la phase 1). Toute entrée qui vient d'une autre
  application l'est aussi : identifiants de documents, chemins, modes d'ouverture.
- Appareil de référence : Pixel 4 XL, numéro de série adb `192.168.0.104:5555` par câble ou
  `192.168.0.104:5555` en Wi‑Fi. Quand le disque occupe le port USB-C, on n'utilise que le Wi‑Fi.
- Code natif portable : pas d'`explicit_bzero` dans `native/src` après T2 (on utilise `fl_memzero`).
  Les en-têtes publics ont des gardes `extern "C"`.

## Review Focus

1. **Débranchement en pleine lecture** (VLC qui lit) → aucun plantage. Les lectures en cours renvoient
   `EIO`, le volume disparaît de « Fichiers » et la clé est effacée. *Tests : T3
   `test_usb_disparition`, T7 `VolumeManagerTest.detachWhileFileOpen`.*
2. **Processus Android tué puis relancé** par « Fichiers », alors qu'une URI est encore ouverte →
   pas de racine, `FileNotFoundException` propre, pas de plantage. *Test : T8
   `ProviderTest.unknownDocumentAfterRestart`.*
3. **Lectures concurrentes** (VLC + génération de vignettes, plusieurs fils) → données justes, pas
   d'interblocage. *Tests : T4 `test_cache_concurrence`, T6 `EngineTest.parallelReads`.*
4. **Noms avec émojis ou accents** → affichés et ouvrables à l'identique. *Tests : T6
   `EngineTest.emojiName`, T8 `ProviderTest.emojiDocument`.*
5. **Mauvaise phrase de passe, ou mémoire insuffisante pendant Argon2 (1 Gio)** → message français
   clair, application toujours utilisable, nouvel essai possible. *Tests : T6
   `EngineTest.wrongPassphrase`, T9 `unlock_dialog_test.dart` (erreur affichée, champ vidé).*

---

## Arborescence

```
native/
  include/fluluks/usb.h            # transport USB abstrait + fl_usb_bdev_open (T3)
  include/fluluks/cache.h          # fl_cache_bdev_new (T4)
  src/util/memzero.c               # fl_memzero (T2)
  src/usb/bot.c scsi.c usb_bdev.c usbdevfs_transport.c   (T3)
  src/blockdev/cache_bdev.c        (T4)
  jni/fluluks_jni.c handles.c handles.h json_out.c json_out.h   (T6, compilés seulement si ANDROID)
  tests/support/fake_msc.c fake_msc.h  (T3)   tests/test_usb.c test_cache.c test_json_out.c
  cmake/android.cmake              # bibliothèque partagée fluluks_jni (T5)
tools/android/run_native_tests.sh  # tests natifs arm64 sur le téléphone (T2)
tools/device/prepare_test_key.sh   # prépare une clé USB sacrifiée (T10, sudo, DESTRUCTEUR)
pubspec.yaml  lib/main.dart  lib/src/{app.dart,strings_fr.dart,state/app_state.dart,
  screens/disks_screen.dart,widgets/unlock_dialog.dart,widgets/partition_tile.dart,bridge/fluluks_api.g.dart}
pigeons/fluluks_api.dart
android/app/build.gradle.kts  android/app/src/main/AndroidManifest.xml
android/app/src/main/res/xml/usb_device_filter.xml
android/app/src/main/kotlin/com/mawena/fluluks/
  MainActivity.kt
  engine/NativeEngine.kt engine/EngineModels.kt engine/EngineError.kt
  usb/UsbDisks.kt
  volumes/VolumeManager.kt volumes/DiskState.kt
  service/VolumeService.kt
  provider/FluLuksDocumentsProvider.kt provider/DocumentIds.kt provider/MimeTypes.kt
  bridge/FluLuksApi.g.kt bridge/HostApiImpl.kt
android/app/src/androidTest/kotlin/com/mawena/fluluks/{EngineTest.kt,VolumeManagerInstrumentedTest.kt,ProviderTest.kt,Fixtures.kt}
android/app/src/test/kotlin/com/mawena/fluluks/{DocumentIdsTest.kt,MimeTypesTest.kt,VolumeManagerTest.kt}
test/{disks_screen_test.dart,unlock_dialog_test.dart,fake_host_api.dart}
```

Commandes de référence :
```bash
# natif PC (phase 1) :
cmake -S native -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFL_TESTS=ON -DFL_SANITIZE=ON && cmake --build build/native && ctest --test-dir build/native --output-on-failure -LE lent
# Android :
flutter build apk --debug
(cd android && ./gradlew :app:testDebugUnitTest)                                         # JVM
(cd android && ./gradlew :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=…)  # sur le téléphone, ANDROID_SERIAL=192.168.0.104:5555
flutter test                                                                              # widgets
flutter install -d 192.168.0.104:5555    # ou 192.168.0.104:5555
```

---

### Task 1 : Environnement Android

**Files :** Modify `docs/superpowers/plans/2026-10-17-fluluks-feuille-de-route.md` (case T1). Rien d'autre
dans le dépôt.

- [ ] **Step 1 : vérifier l'espace.** `df -h /` doit montrer ≥ 10 Gio libres. Sinon, **STOP** et
  NEEDS_CONTEXT : l'utilisateur doit libérer de l'espace.
- [ ] **Step 2 : installer cmdline-tools** dans `~/Android/cmdline-tools/latest`, depuis l'archive
  officielle `commandlinetools-linux-*_latest.zip` de `dl.google.com/android/repository/`. Prendre la
  dernière version listée par `repository2-3.xml`.
- [ ] **Step 3 : licences, à faire par l'utilisateur.** Lui afficher la commande
  `~/Android/cmdline-tools/latest/bin/sdkmanager --licenses` et attendre qu'il l'ait lancée
  lui-même : accepter une licence lui revient.
- [ ] **Step 4 : installer les paquets** :
  `sdkmanager "platform-tools" "platforms;android-36" "build-tools;35.0.0" "ndk;28.2.13676358" "cmake;3.22.1"`,
  puis `flutter config --android-sdk ~/Android`.
- [ ] **Step 5 : vérifier.** `flutter doctor -v` affiche `[✓] Android toolchain`, et
  `flutter devices` liste `Pixel 4 XL`. Noter dans le rapport l'espace disque restant.
- [ ] **Step 6 : commit**, en cochant T1 — `chore: environnement Android prêt (SDK 36, NDK 28.2)`

---

### Task 2 : Moteur natif portable sur Android et tests natifs sur le téléphone

Elle reprend les points reportés depuis la phase 1 (voir la feuille de route).

**Files :**
- Create : `native/src/util/memzero.c`, `tools/android/run_native_tests.sh`
- Modify : `native/include/fluluks/secure.h`, tous les fichiers de `native/src` qui appellent
  `explicit_bzero`, chaque en-tête de `native/include/fluluks/*.h` (gardes `extern "C"`),
  `tools/fixtures/make_fixtures.sh`, `native/README.md` (cmake 3.20), `native/CMakeLists.txt`,
  la contrainte globale correspondante du plan de la phase 1

**Interfaces :**
- Produces : `void fl_memzero(void *p, size_t n);` (dans `secure.h`). Elle utilise `explicit_bzero` si
  `__GLIBC__` est défini ou si `__ANDROID_API__ >= 28`. Sinon, elle appelle `memset` via un pointeur de
  fonction `volatile`, suivi d'une barrière `__asm__ __volatile__("" : : "r"(p) : "memory")`.

- [ ] **Step 1 : test d'abord.** Ajouter `test_memzero` à `test_error.c` : remplir 64 octets avec 0xAA,
  appeler `fl_memzero`, vérifier qu'ils valent tous 0. Constater l'échec de compilation (symbole
  absent).
- [ ] **Step 2 : implémenter `fl_memzero`** et remplacer tous les appels à `explicit_bzero` dans
  `native/src` et `native/tools`. Ajouter les gardes `extern "C"`.
- [ ] **Step 3 : `make_fixtures.sh`.** Écrire un marqueur `.fixtures-dir` en tout début de génération.
  La garde accepte un dossier qui contient `.fixtures-dir` ou `.stamp`. Test manuel : interrompre la
  génération (`kill`), la relancer, et vérifier qu'elle repart.
- [ ] **Step 4 : compilation croisée** dans `build/android-arm64`, avec
  `-DCMAKE_TOOLCHAIN_FILE=$ANDROID_HOME/ndk/28.2.13676358/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=26 -DCMAKE_BUILD_TYPE=Release -DFL_TESTS=ON -DFL_SANITIZE=OFF`.
  Elle doit réussir sans avertissement. `nm -D` sur les binaires ne doit montrer aucune référence à
  `explicit_bzero`.
- [ ] **Step 5 : écrire `tools/android/run_native_tests.sh <serial>`.** Il pousse les binaires de test
  et un sous-ensemble d'images dans `/data/local/tmp/fluluks/`, en passant `FL_FIXTURES` à chaque
  test. Sous-ensemble : `ext4-plain.img`, `gpt-luks2.img`, `whole-luks2-4k.img`,
  `whole-luks2-2slots.img`, `whole-luks1-xts.img`, `mbr-luks1-essiv.img`, `whole-luks2-exact.img`,
  `pass*.txt`, `cipher-kat.json`, `tree/sous-dossier/données.bin`. Il lance `test_error`, `test_crypto`,
  `test_luks1`, `test_luks2`, `test_ext4` et `test_luks2_lent` via `adb shell`, puis affiche un
  récapitulatif.
- [ ] **Step 6 : vérifier.** Sur PC, `ctest -LE lent` passe. Sur le téléphone, tous les tests passent.
  Noter la durée de `test_luks2_lent` sur le téléphone (déverrouillage avec les paramètres exacts du
  disque) dans le rapport **et** dans la feuille de route. Cible : la spec demande ≤ ~15 s.
- [ ] **Step 7 : commit**, en cochant T2 — `feat: moteur natif portable Android (fl_memzero, extern "C") et tests natifs sur le téléphone`

---

### Task 3 : Périphérique bloc USB (Bulk-Only Transport + SCSI)

**Files :**
- Create : `native/include/fluluks/usb.h`, `native/src/usb/{bot.c,scsi.c,usb_bdev.c,usbdevfs_transport.c}`,
  `native/tests/support/{fake_msc.c,fake_msc.h}`, `native/tests/test_usb.c`
- Modify : `native/include/fluluks/error.h`, `native/src/error.c`, `native/CMakeLists.txt`

**Interfaces :**
- Produces :
  ```c
  /* error.h : nouveaux codes */
  FL_ENODEV = -14 /* "disque débranché" */, FL_ETIMEDOUT = -15 /* "le disque ne répond pas" */,
  FL_ESTALL = -16 /* "transfert USB bloqué" (interne) */, FL_EBUSY = -17 /* "volume occupé" */
  /* usb.h */
  typedef struct {
    void *ctx;
    int (*bulk_out)(void *ctx, const void *buf, size_t len, unsigned timeout_ms); /* octets transférés ou fl_err */
    int (*bulk_in)(void *ctx, void *buf, size_t len, unsigned timeout_ms);
    int (*clear_halt)(void *ctx, bool in_endpoint);
    int (*bot_reset)(void *ctx);            /* requête de classe 0xFF, interface ; puis les deux clear_halt */
  } fl_usb_transport;
  int fl_usb_bdev_open(const fl_usb_transport *t, uint8_t lun, bool read_only, fl_bdev **out);
  int fl_usbdevfs_transport_new(int fd, uint8_t iface, uint8_t ep_in, uint8_t ep_out, fl_usb_transport **out);
  void fl_usbdevfs_transport_free(fl_usb_transport *t);
  ```
- Comportement de `fl_usb_bdev_open` :
  - `INQUIRY`, puis `TEST UNIT READY` avec `REQUEST SENSE`. Il réessaie tant que le sens vaut
    NOT READY / « becoming ready », 20 s au plus en tout, le temps qu'un disque dur démarre.
  - `READ CAPACITY(10)`, puis `READ CAPACITY(16)` si la valeur renvoyée est 0xFFFFFFFF.
  - Il accepte une taille de bloc dans {512, 1024, 2048, 4096}, sinon `FL_ENOTSUP`.
- Lecture : `READ(10)`, ou `READ(16)` si le LBA ou le nombre de blocs dépasse 32 bits, avec **au plus
  64 Kio par commande**. `fl_bdev.write` renvoie toujours `FL_EROFS` en phase 2, et `flush` envoie
  `SYNCHRONIZE CACHE` seulement si `!read_only`.
- BOT :
  - CBW de 31 octets, signature `0x43425355`, tag incrémenté à chaque commande.
  - CSW de 13 octets, signature `0x53425355`, tag identique ; le résidu est pris en compte.
  - Statut 1 → `REQUEST SENSE`, puis traduction : MEDIUM ERROR → `FL_EIO`, DATA PROTECT → `FL_EROFS`,
    NOT READY → réessai.
  - Statut 2, CSW invalide ou tag faux → reprise (`bot_reset`), puis **un** réessai.
  - Blocage (`FL_ESTALL`) pendant la phase de données → `clear_halt` sur ce point d'accès, puis lecture
    du CSW.
  - `FL_ENODEV` n'est jamais réessayé. Il remonte, et le périphérique devient « mort » : toutes les
    lectures suivantes renvoient `FL_ENODEV` immédiatement.
- Un `pthread_mutex_t` par périphérique sérialise les commandes.
- `usbdevfs_transport.c` (Linux et Android) :
  - `USBDEVFS_BULK` avec un délai de 5 s ; `USBDEVFS_CLEAR_HALT` ; `USBDEVFS_CONTROL` pour la
    réinitialisation BOT (bmRequestType 0x21, bRequest 0xFF).
  - Correspondances errno : `ENODEV`/`ESHUTDOWN`/`EPROTO` après débranchement → `FL_ENODEV` ;
    `ETIMEDOUT` → `FL_ETIMEDOUT` ; `EPIPE` → `FL_ESTALL` ; le reste → `FL_EIO`.
  - Un transfert de plus de 16 Kio qui échoue avec `EINVAL` ou `ENOMEM` est relancé en morceaux de
    16 Kio.
  - Le transport ne possède pas `fd`, qui appartient à Kotlin.
- `fake_msc` : un faux périphérique de stockage de masse, adossé à une image, qui implémente
  `fl_usb_transport` avec une machine d'états BOT. On lui scripte des pannes :
  `fake_msc_fail_after(n, FL_ENODEV)`, `fake_msc_stall_next_data()`, `fake_msc_bad_csw_next()`,
  `fake_msc_not_ready(k)`, `fake_msc_set_block_size(4096)`. Il compte les commandes reçues.

- [ ] **Step 1 : écrire les tests qui échouent** (`test_usb.c`, sur `fake_msc` adossé à `gpt-luks2.img`) :
  - `test_usb_capacite` : `sector_size` = 512, `sector_count` = taille de l'image / 512.
  - `test_usb_lecture_identique` : 3 Mio lus à des décalages variés sont identiques au fichier, et une
    lecture de 1 Mio part en 16 commandes de 64 Kio.
  - `test_usb_disque_lent` : `fake_msc_not_ready(5)` → l'ouverture réussit après 5 essais.
  - `test_usb_blocage` : un blocage sur les données → `clear_halt`, puis la lecture réussit.
  - `test_usb_csw_invalide` : un CSW invalide → `bot_reset`, réessai, succès. Deux CSW invalides de
    suite → `FL_EIO`.
  - `test_usb_disparition` : `fail_after(3, FL_ENODEV)` → `FL_ENODEV`, et toute lecture suivante
    renvoie `FL_ENODEV` sans envoyer de commande.
  - `test_usb_4k` : une taille de bloc de 4096 → `sector_size` = 4096 ; la pile complète
    partitions → LUKS2 → ext4 sur `whole-luks2-4k.img` lit `hello.txt`.
  - `test_usb_lecture_seule` : `fl_bdev_write` → `FL_EROFS`, et `fake_msc` n'a reçu **aucun** `WRITE`.
  - `test_usb_pile_complete` : `gpt-luks2.img` via `fake_msc`, puis `fl_part_scan`, LUKS2 et ext4 →
    `hello.txt` = `"Bonjour FluLuks\n"`.
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : implémenter**, avec les messages `fl_strerror` exacts ci-dessus.
- [ ] **Step 4 : vérifier.** `ctest -R test_usb` passe sans rapport ASan, puis la suite rapide passe.
  `usbdevfs_transport.c` compile sur Linux et dans `build/android-arm64`.
- [ ] **Step 5 : commit**, en cochant T3 — `feat: périphérique bloc USB (Bulk-Only Transport, SCSI)`

---

### Task 4 : Cache de blocs

**Files :** Create `native/include/fluluks/cache.h`, `native/src/blockdev/cache_bdev.c`,
`native/tests/test_cache.c` ; Modify `native/CMakeLists.txt`

**Interfaces :**
- Produces :
  `int fl_cache_bdev_new(fl_bdev *parent, uint32_t block_bytes, uint32_t nblocks, fl_bdev **out);`
  - Ne possède pas `parent`. `block_bytes` est une puissance de 2, multiple de `parent->sector_size`.
    Valeurs utilisées par l'application : 65 536 octets × 256 blocs = 16 Mio.
  - Lecture : on découpe en blocs alignés, avec une LRU. Sur un défaut de cache, si l'accès précédent
    se terminait juste avant (lecture séquentielle), on lit en **un seul** appel parent jusqu'à
    `readahead` = 4 blocs.
  - Le dernier bloc partiel du disque est géré.
  - Écriture : en transmission directe vers le parent, avec mise à jour des blocs en cache (la phase 3
    s'en servira). `flush` est transmis tel quel.
  - Un `pthread_mutex_t` protège la LRU. Les lectures parent se font **hors** de ce verrou, avec un
    drapeau « bloc en cours de chargement » et une variable de condition, pour ne pas sérialiser deux
    lecteurs qui lisent des blocs différents.
  - Les erreurs du parent remontent telles quelles et ne sont jamais mises en cache.
- [ ] **Step 1 : écrire les tests qui échouent.** Ils utilisent un bdev « compteur » interne au test,
  qui enveloppe `ext4-plain.img` et compte les appels et les octets lus.
  - `test_cache_identique` : 2000 lectures aléatoires (graine fixe, tailles de 1 à 300 secteurs)
    donnent les mêmes octets que le parent.
  - `test_cache_sequentiel` : lire 8 Mio séquentiellement demande au plus 8 Mio / (4 × 64 Kio) + 2
    appels parent.
  - `test_cache_reutilise` : relire les mêmes 64 Kio ne déclenche aucun appel parent.
  - `test_cache_erreur_non_memorisee` : le parent échoue une fois avec `FL_EIO` → la lecture renvoie
    `FL_EIO`, et la relecture réussit.
  - `test_cache_concurrence` : 8 fils × 500 lectures aléatoires, octets justes, pas d'interblocage
    (délai de 30 s maximum).
  - `test_cache_fin_disque` : la lecture du dernier secteur d'un parent dont la taille n'est pas un
    multiple de `block_bytes` réussit.
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier.** `ctest -R test_cache` passe sous ASan, et aussi sous `-fsanitize=thread`
  dans un build séparé, `build/native-tsan`.
- [ ] **Step 5 : commit**, en cochant T4 — `feat: cache de blocs avec lecture anticipée`

---

### Task 5 : Projet Flutter et intégration native Gradle

**Files :**
- Create : le projet Flutter à la racine du dépôt (`pubspec.yaml`, `lib/main.dart`, `android/…`),
  `native/cmake/android.cmake`, `native/jni/fluluks_jni.c` (version minimale),
  `android/app/src/main/kotlin/com/mawena/fluluks/engine/NativeEngine.kt`,
  `android/app/src/androidTest/kotlin/com/mawena/fluluks/EngineTest.kt`
- Modify : `.gitignore` (les ajouts de Flutter), `native/CMakeLists.txt` (`if(ANDROID) include(cmake/android.cmake)`)

**Interfaces :**
- Produces : `NativeEngine.version(): String` (ASCII, `"fluluks <version git ou 0.1.0>"`), et la
  bibliothèque `libfluluks_jni.so` chargée par `System.loadLibrary("fluluks_jni")`.

- [ ] **Step 1 : créer le projet** :
  `flutter create --org com.mawena --project-name fluluks --platforms android --android-language kotlin .`
  Ne rien écraser parmi `docs/`, `native/`, `tools/` et `.gitignore` : fusionner `.gitignore` à la
  main. Supprimer le test d'exemple « counter » généré.
- [ ] **Step 2 : configurer `android/app/build.gradle.kts`** :
  - `namespace` et `applicationId` = `com.mawena.fluluks` ;
  - `minSdk = 26`, `compileSdk = 36`, `targetSdk = 36`, `ndkVersion = "28.2.13676358"` ;
  - `ndk { abiFilters += listOf("arm64-v8a", "x86_64") }` ;
  - `externalNativeBuild { cmake { path = file("../../native/CMakeLists.txt"); version = "3.22.1" } }`,
    avec `defaultConfig.externalNativeBuild.cmake.arguments += listOf("-DFL_TESTS=OFF", "-DFL_SANITIZE=OFF")` ;
  - dépendances androidTest : `androidx.test:runner`, `androidx.test.ext:junit`, `androidx.test:rules` ;
  - `testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"`.
  - Nom affiché « FluLuks » dans `AndroidManifest.xml`.
- [ ] **Step 3 : écrire `native/cmake/android.cmake`.** Il crée la bibliothèque partagée `fluluks_jni`
  à partir de `jni/*.c`, liée à `fluluks` et à `log`. Il ajoute `-O2` à toutes les cibles fluluks et
  tierces, quelle que soit la variante. La cible sous-module `mbedtls/framework` doit s'y trouver
  aussi.
- [ ] **Step 4 : test instrumenté qui échoue.** `EngineTest.versionLoads()` vérifie que
  `NativeEngine.version()` commence par `"fluluks "`. Le lancer sur le téléphone : il échoue, car la
  bibliothèque n'existe pas encore.
- [ ] **Step 5 : implémenter** `Java_com_mawena_fluluks_engine_NativeEngine_version` et
  `NativeEngine.kt`.
- [ ] **Step 6 : vérifier.** `flutter build apk --debug` réussit, et l'APK contient
  `lib/arm64-v8a/libfluluks_jni.so` (vérifier avec `unzip -l`). `connectedDebugAndroidTest` (classe
  `EngineTest`) passe sur `192.168.0.104:5555`. `flutter run` affiche l'application par défaut sans
  plantage.
- [ ] **Step 7 : commit**, en cochant T5 — `feat: projet Flutter Android avec le moteur natif intégré`

---

### Task 6 : Couche JNI et moteur Kotlin

**Files :**
- Create : `native/jni/{handles.c,handles.h,json_out.c,json_out.h}`, `native/tests/test_json_out.c`,
  `android/app/src/main/kotlin/com/mawena/fluluks/engine/{EngineModels.kt,EngineError.kt}`,
  `android/app/src/androidTest/kotlin/com/mawena/fluluks/Fixtures.kt`
- Modify : `native/jni/fluluks_jni.c`, `NativeEngine.kt`, `EngineTest.kt`, `native/CMakeLists.txt`
  (`json_out.c` est aussi compilé dans les tests PC)

**Interfaces :**
- Produces (Kotlin, `object NativeEngine`, tout en `external`) :
  ```kotlin
  fun version(): String
  fun strerror(code: Int): ByteArray                         // UTF-8
  fun openImage(path: ByteArray): Long                       // poignée disque > 0, ou code fl_err < 0 (lecture seule)
  fun openUsb(fd: Int, iface: Int, epIn: Int, epOut: Int): Long   // pile usb_bdev(read_only) + cache 64 Kio × 256
  fun closeDisk(disk: Long): Int
  fun scan(disk: Long): ByteArray                            // JSON UTF-8, voir ci-dessous
  fun mount(disk: Long, partIndex: Int, pass: ByteArray?): Long   // poignée volume ; met pass à zéro dans tous les cas
  fun unmount(vol: Long, force: Boolean): Int                // sans force → FL_EBUSY si fichiers ouverts ; avec force → les ferme
  fun statfs(vol: Long): ByteArray
  fun stat(vol: Long, path: ByteArray): ByteArray            // JSON ; {"err":<code>} en cas d'erreur
  fun list(vol: Long, path: ByteArray): ByteArray            // JSON tableau trié par nom (octets) ; {"err":…}
  fun openFile(vol: Long, path: ByteArray): Long
  fun pread(file: Long, offset: Long, buf: ByteArray, len: Int): Int   // octets lus, 0 = fin, < 0 = fl_err
  fun closeFile(file: Long): Int
  ```
  `EngineError(code: Int) : Exception(message = String(strerror(code), UTF_8))`. `EngineModels.kt` :
  `PartitionInfo`, `ScanResult`, `DirEntry`, `VolumeStat`, `FsStat`, analysés avec `org.json`, sans
  dépendance supplémentaire.
- JSON (natif → Kotlin), clés exactes :
  - `scan` →
    `{"sector_size":512,"size":N,"table":"gpt|mbr|none","parts":[{"index":1,"first_lba":2048,"size":N,"content":"LUKS2","label":"","uuid":"","supported":true,"luks":{"version":2,"cipher":"aes-xts-plain64","primary_damaged":false}}]}`.
    La clé `luks` n'existe que pour LUKS. La valeur de `content` est celle de `fl_content_name`.
    Deux règles pour remplir `luks` :
    - Quand `fl_probe` renvoie UNKNOWN mais que `fl_luks_open` réussit (en-tête principal abîmé,
      secondaire valide), `content` vaut `"LUKS2"` et `primary_damaged` vaut `true`.
    - `luks.max_kdf_memory_kib` donne la plus grande mémoire Argon2 parmi les slots utilisables (0 si
      PBKDF2). L'API publique doit l'exposer : ajouter
      `int fl_luks_max_kdf_memory_kib(const fl_luks *l, uint32_t *out)`, avec un test PC dans
      `test_luks2.c` qui vérifie 65536 sur `gpt-luks2.img`.
  - `list` → `[{"name":"…","type":"f|d|l|o","size":N,"mtime":S}]` ; `stat` → l'objet seul.
  - `statfs` → `{"total":N,"free":N,"label":"…","read_only":true,"ro_reason":"…","needs_recovery":false}`.
  - Les chaînes sont échappées (`"`, `\`, caractères de contrôle en `\u00XX`). Une séquence UTF-8
    invalide (nom ext4 non UTF-8) est remplacée par U+FFFD.
- Poignées (`handles.c`) : une table de 1024 entrées, chaque entrée portant un type
  (disque/volume/fichier), un index et une génération. Une poignée périmée ou de mauvais type renvoie
  `FL_EINVAL` et n'est jamais déréférencée. La table est protégée par un mutex. Fermer un disque
  démonte d'abord ses volumes de force.
- `mount` enchaîne :
  1. tranche de la partition, puis `fl_probe` ;
  2. si c'est LUKS (ou si `fl_luks_open` réussit malgré UNKNOWN) → déverrouillage ;
  3. `fl_fs_mount(EXT4, want_rw = false)`.
  Un contenu non pris en charge → `FL_ENOTSUP`. Le volume possède sa tranche, ses objets LUKS, son
  périphérique déchiffré et son fs, et les ferme dans l'ordre de la phase 1.

- [ ] **Step 1 : écrire les tests qui échouent.**
  - PC, `test_json_out.c` : échappement des guillemets et des caractères de contrôle, remplacement de
    l'UTF-8 invalide par U+FFFD, émoji conservé tel quel.
  - Téléphone, `EngineTest`. `Fixtures.kt` copie des images depuis `/data/local/tmp/fluluks/`
    (poussées par `tools/android/run_native_tests.sh` en T2) vers `context.filesDir`, via
    `ParcelFileDescriptor` d'`UiAutomation.executeShellCommand("cat …")`.
    - `scanGptLuks2` : 1 partition, `content` = `"LUKS2"`, cipher `aes-xts-plain64`.
    - `unlockAndRead` : `mount(p1, pass.txt)`, `list("/")` contient `hello.txt`, et `pread` donne
      `"Bonjour FluLuks\n"`.
    - `wrongPassphrase` : → `FL_EBADPASS`, puis un nouvel essai avec la bonne phrase réussit sur le
      même disque.
    - `passIsZeroed` : après `mount`, le `ByteArray` passé ne contient que des zéros.
    - `emojiName` : `list` contient le nom `accentué é ☀.txt` à l'identique, et `stat` sur ce nom
      réussit.
    - `parallelReads` : 8 fils lisent `gros.bin` à des décalages aléatoires, contenu = motif
      `(i*31+7) & 0xFF`.
    - `staleHandle` : un `pread` sur un fichier fermé, ou après un `unmount(force)`, renvoie
      `FL_EINVAL`, sans plantage.
    - `busyUnmount` : `unmount(vol, false)` avec un fichier ouvert → `FL_EBUSY`.
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : implémenter.** Le code JNI utilise `GetByteArrayRegion` et `SetByteArrayRegion`
  (pas d'accès « critique » pendant une E/S). La phrase de passe est copiée dans `fl_secure_alloc`,
  puis le tableau Java est mis à zéro avec `SetByteArrayRegion`.
- [ ] **Step 4 : vérifier.** Les tests PC passent, ainsi que `connectedDebugAndroidTest` pour la
  classe `EngineTest`.
- [ ] **Step 5 : commit**, en cochant T6 — `feat: couche JNI à poignées et moteur Kotlin`

---

### Task 7 : USB, gestionnaire de volumes et service au premier plan

**Files :**
- Create : `usb/UsbDisks.kt`, `volumes/{VolumeManager.kt,DiskState.kt}`, `service/VolumeService.kt`,
  `res/xml/usb_device_filter.xml`, `src/test/…/VolumeManagerTest.kt`,
  `src/androidTest/…/VolumeManagerInstrumentedTest.kt`
- Modify : `AndroidManifest.xml`, `MainActivity.kt`

**Interfaces :**
- Produces :
  ```kotlin
  data class DiskState(val id: String, val name: String, val sizeBytes: Long, val hasPermission: Boolean,
                       val scan: ScanResult?, val error: String?)
  data class VolumeState(val id: String, val diskId: String, val partIndex: Int, val title: String,
                         val totalBytes: Long, val freeBytes: Long, val needsRecovery: Boolean, val roReason: String)
  object VolumeManager {
    val disks: StateFlow<List<DiskState>>; val volumes: StateFlow<List<VolumeState>>
    fun init(context: Context)                                   // appelé par Application/MainActivity et par le provider
    fun onUsbAttached(device: UsbDevice); fun onUsbDetached(device: UsbDevice)
    fun requestPermission(diskId: String)
    suspend fun unlock(diskId: String, partIndex: Int, pass: ByteArray): VolumeState   // Dispatchers.IO ; lève EngineError
    suspend fun mountPlain(diskId: String, partIndex: Int): VolumeState
    fun eject(volumeId: String); fun ejectAll()
    fun volume(id: String): VolumeState?; fun nativeVolume(id: String): Long   // pour le provider
    fun addImageDiskForTests(path: String): String               // @VisibleForTesting
  }
  ```
  `VolumeManager` reçoit un moteur via l'interface `Engine`, qu'implémente `NativeEngine`. Cela permet
  des tests JVM avec un `FakeEngine`.
- Comportement :
  - `id` disque = `deviceName` USB. `id` volume = `"v" + compteur`.
  - Interface de stockage de masse : classe 8, sous-classe 6, protocole 0x50, sur
    `alternateSetting == 0`. Si elle n'existe pas → `DiskState.error` =
    `"Ce boîtier USB n'est pas pris en charge (UAS seulement)"`.
  - `claimInterface(intf, true)`, puis `setInterface(intf)`. Les points d'accès bulk IN et OUT sont
    trouvés par direction.
  - Débranchement : `ejectAll` pour ce disque, avec `unmount(force)`, puis `closeDisk` et
    `connection.close()`. Les flux d'état sont mis à jour, et le provider notifie
    `DocumentsContract.buildRootsUri`.
  - Service :
    - démarré (`startForegroundService`) au premier volume ouvert, arrêté au dernier fermé ;
    - `foregroundServiceType="connectedDevice"`, permissions `FOREGROUND_SERVICE`,
      `FOREGROUND_SERVICE_CONNECTED_DEVICE` et `POST_NOTIFICATIONS` (demandée à l'exécution sur
      API 33+) ;
    - canal « Disques ouverts » ;
    - notification `"Disque ouvert : <titre>"`, ou `"<n> disques ouverts"`, avec une action
      **« Éjecter »** qui appelle `ejectAll`.
  - `intent-filter` `USB_DEVICE_ATTACHED` sur `MainActivity` avec `usb_device_filter.xml`, filtré sur
    `class="8"`. Au démarrage, `UsbManager.deviceList` est aussi parcourue.
  - `requestPermission` : un `PendingIntent` explicite (`setPackage`) avec `FLAG_MUTABLE` (API 31+).
  - Mémoire avant le déverrouillage : si `luks.max_kdf_memory_kib × 1024` dépasse 80 % de
    `ActivityManager.MemoryInfo.availMem`, `unlock` lève d'abord
    `EngineError(FL_ENOMEM)` avec le message
    `"Mémoire insuffisante pour déverrouiller ce disque : fermez d'autres applications et réessayez"`.
    Il n'y a pas de natif dans ce cas. Test JVM : `unlockRefusedWhenLowMemory`, avec une source de
    mémoire injectée.
- [ ] **Step 1 : écrire les tests qui échouent.**
  - JVM, `VolumeManagerTest` avec `FakeEngine` :
    - `unlockPublishesVolume` ;
    - `detachWhileFileOpen` : `onUsbDetached` pendant qu'un fichier est ouvert → `unmount(force)` est
      appelé, et la liste des volumes se vide ;
    - `wrongPassKeepsDisk` : `EngineError(FL_EBADPASS)` remonte, et le disque reste dans la liste ;
    - `passZeroedOnError`.
  - Téléphone, `VolumeManagerInstrumentedTest` :
    - `addImageDiskForTests(gpt-luks2)`, `unlock`, puis `volumes.value.size == 1` et le service tourne
      (vérifié avec `ActivityManager.getRunningServices` ou un drapeau statique `VolumeService.running`) ;
    - `ejectAll` → le service s'arrête.
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier.** `testDebugUnitTest` et la classe instrumentée passent.
- [ ] **Step 5 : commit**, en cochant T7 — `feat: détection USB, gestionnaire de volumes et service au premier plan`

---

### Task 8 : DocumentsProvider en lecture seule

**Files :**
- Create : `provider/{FluLuksDocumentsProvider.kt,DocumentIds.kt,MimeTypes.kt}`,
  `src/test/…/{DocumentIdsTest.kt,MimeTypesTest.kt}`, `src/androidTest/…/ProviderTest.kt`
- Modify : `AndroidManifest.xml`

**Interfaces :**
- Produces :
  - `DocumentIds.encode(volumeId, path): String` = `"$volumeId:$path"`, avec `path` absolu commençant
    par `/`.
  - `DocumentIds.decode(id): Pair<String,String>?` renvoie `null` si l'identifiant est mal formé :
    pas de `:`, chemin non absolu, `..` ou composant vide.
  - `MimeTypes.forName(name): String` : par extension, en minuscules, avec `MimeTypeMap` puis une table
    locale (mkv, mp4, avi, webm, mp3, flac, ogg, m4a, jpg, jpeg, png, heic, gif, webp, pdf, txt, srt,
    zip, epub). Défaut : `application/octet-stream`.
- Manifeste : `<provider android:name=".provider.FluLuksDocumentsProvider" android:authorities="com.mawena.fluluks.documents" android:exported="true" android:grantUriPermissions="true" android:permission="android.permission.MANAGE_DOCUMENTS">`
  avec l'`intent-filter` `android.content.action.DOCUMENTS_PROVIDER`.
- Comportement :
  - `queryRoots` : une racine par volume. `COLUMN_TITLE` = titre du volume ; `COLUMN_SUMMARY` =
    `"Chiffré (LUKS2) — lecture seule"` ou `"Lecture seule"` ; `COLUMN_FLAGS` =
    `FLAG_LOCAL_ONLY or FLAG_SUPPORTS_IS_CHILD` ; `COLUMN_AVAILABLE_BYTES` = espace libre ;
    `COLUMN_DOCUMENT_ID` = `encode(v, "/")`.
  - `queryChildDocuments` passe par `list`, `queryDocument` par `stat`. Colonnes : `DISPLAY_NAME`,
    `MIME_TYPE` (`Document.MIME_TYPE_DIR` pour un dossier), `SIZE`, `LAST_MODIFIED` (mtime × 1000),
    `FLAGS` = 0 (aucune écriture). Les liens symboliques et les « autres » sont listés, mais
    `openDocument` les refuse.
  - `openDocument(id, mode)` : un mode autre que `"r"` →
    `FileNotFoundException("Lecture seule pour l'instant")`. Sinon :
    `StorageManager.openProxyFileDescriptor(ParcelFileDescriptor.MODE_READ_ONLY, callback, handler)`.
    - `handler` vit sur un `HandlerThread("fluluks-io")` partagé.
    - `onGetSize` renvoie la taille. `onRead` boucle sur `NativeEngine.pread` dans `data`. Une erreur
      lève `ErrnoException("pread", OsConstants.EIO)`, et `FL_ENODEV` lève `ENXIO`.
    - `onRelease` appelle `closeFile`, puis enregistre une ligne `Log.i("FluLuksPerf", "<octets> o en <ms> ms")`,
      sans nom de fichier ni phrase de passe.
  - Un identifiant inconnu (volume absent, par exemple après un redémarrage du processus) →
    `FileNotFoundException("Volume fermé")`.
  - `isChildDocument` compare les préfixes de chemin sur des composants entiers.
  - `onCreate` appelle `VolumeManager.init(context)`.
- [ ] **Step 1 : écrire les tests qui échouent.**
  - JVM, `DocumentIdsTest` : aller-retour ; rejet de `"v1:../x"`, `"v1:relatif"`, `"v1:/a//b"`,
    `"pasdedeuxpoints"`.
  - JVM, `MimeTypesTest` : `film.MKV` → `video/x-matroska`, `inconnu.xyz` → `application/octet-stream`.
  - Téléphone, `ProviderTest`, avec une image ouverte et déverrouillée via `VolumeManager`. L'appel se
    fait par `ContentResolver` dans le processus de l'application (même uid).
    - `rootsListed` : une racine, résumé `"Chiffré (LUKS2) — lecture seule"`.
    - `childrenOfRoot` : contient `hello.txt` (`text/plain`, taille 16) et `sous-dossier` (dossier).
    - `readFile` : `openFileDescriptor(uri, "r")`, lecture complète de `sous-dossier/données.bin`
      identique au motif.
    - `seekRead` : sur `gros.bin`, `FileInputStream.channel.position(4_000_000)`, puis lecture de
      4096 octets identiques au motif.
    - `writeRefused` : le mode `"w"` → `FileNotFoundException`.
    - `emojiDocument` : `accentué é ☀.txt` est listé et s'ouvre.
    - `unknownDocumentAfterRestart` : un id `"v999:/hello.txt"` → `FileNotFoundException`, sans
      plantage.
    - `ejectRemovesRoot` : après `ejectAll`, `queryRoots` est vide.
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** que les tests JVM et `ProviderTest` passent.
- [ ] **Step 5 : commit**, en cochant T8 — `feat: DocumentsProvider en lecture seule`

---

### Task 9 : Interface Flutter (écrans Disques et Déverrouillage) et pont Pigeon

**Files :**
- Create : `pigeons/fluluks_api.dart`, `lib/src/bridge/fluluks_api.g.dart` (généré),
  `bridge/FluLuksApi.g.kt` (généré), `bridge/HostApiImpl.kt`, `lib/src/{app.dart,strings_fr.dart}`,
  `lib/src/state/app_state.dart`, `lib/src/screens/disks_screen.dart`,
  `lib/src/widgets/{unlock_dialog.dart,partition_tile.dart}`,
  `test/{fake_host_api.dart,disks_screen_test.dart,unlock_dialog_test.dart}`
- Modify : `pubspec.yaml` (`dev_dependencies: pigeon: 29.0.6`), `lib/main.dart`, `MainActivity.kt`

**Interfaces :**
- Produces (Pigeon, `pigeons/fluluks_api.dart`) :
  ```dart
  class PartitionMsg { int index; int sizeBytes; String content; String label; bool supported; bool encrypted; String? cipher; String? volumeId; }
  class DiskMsg { String id; String name; int sizeBytes; bool hasPermission; String? error; List<PartitionMsg> partitions; }
  class VolumeMsg { String id; String diskId; int partIndex; String title; int totalBytes; int freeBytes; bool needsRecovery; String roReason; }
  @HostApi() abstract class FluLuksHostApi {
    List<DiskMsg> listDisks(); List<VolumeMsg> listVolumes();
    void requestPermission(String diskId);
    @async VolumeMsg unlock(String diskId, int partIndex, Uint8List passphrase);
    @async VolumeMsg mountPlain(String diskId, int partIndex);
    void eject(String volumeId);
    void openInFiles(String volumeId);   // Intent ACTION_VIEW sur DocumentsContract.buildRootUri(autorité, id), type "vnd.android.document/root"
  }
  @FlutterApi() abstract class FluLuksEventsApi { void onStateChanged(List<DiskMsg> disks, List<VolumeMsg> volumes); }
  ```
  Les erreurs sont des `FlutterError(code = "<code fl_err>", message = <message français>)`.
  `HostApiImpl` met `passphrase` à zéro après l'appel.
- Interface (chaînes exactes dans `strings_fr.dart`) :
  - Titre « FluLuks ».
  - Liste vide : « Branchez un disque USB-C ».
  - Disque sans permission : bouton « Autoriser l'accès ».
  - Ligne de partition : `<label ou « Partition n »> · <taille lisible en Go/Mo> · <content>`. Puis :
    - chiffrée et fermée → bouton « Déverrouiller » ;
    - en clair et prise en charge → « Ouvrir » ;
    - non prise en charge → « Non pris en charge (<content>) », sans bouton ;
    - ouverte → « Ouvert — visible dans l'application Fichiers », avec les boutons
      « Ouvrir dans Fichiers » et « Éjecter ».
  - Avertissement si `needsRecovery` : « Le volume n'a pas été démonté proprement : contenu possiblement incomplet ».
  - Dialogue de déverrouillage : champ « Phrase de passe » masqué (`obscureText`, `autocorrect: false`,
    `enableSuggestions: false`), bouton « Déverrouiller ».
    - Pendant l'appel : un indicateur et « Déverrouillage en cours… cela peut prendre quelques secondes »,
      bouton désactivé.
    - En cas d'erreur : le message (par exemple « phrase de passe incorrecte ») et le champ vidé.
    - Après l'appel, réussi ou non, le `Uint8List` encodé est mis à zéro et le contrôleur vidé.
  - Material 3 sobre, thème clair/sombre selon le système. Pas de gestionnaire d'état externe :
    `ChangeNotifier` + `ListenableBuilder` *(décision du planificateur)*.
- [ ] **Step 1 : générer Pigeon et écrire les tests de widgets qui échouent** (`FakeHostApi`).
  - `disks_screen_test` :
    - liste vide → « Branchez un disque USB-C » ;
    - un disque LUKS2 → « Déverrouiller » ;
    - un NTFS → « Non pris en charge (NTFS) » ;
    - un volume ouvert → « Ouvrir dans Fichiers » et « Éjecter », et Éjecter appelle `eject("v1")`.
  - `unlock_dialog_test` :
    - mauvaise phrase de passe → le message s'affiche, le champ est vide, le bouton est réactivé ;
    - les octets envoyés au fake valent l'UTF-8 de `"mot de passe été ☀"`, puis le tampon est à zéro
      après l'appel ;
    - pendant l'appel, l'indicateur est visible et le bouton désactivé.
- [ ] **Step 2 : constater l'échec** (`flutter test`).
- [ ] **Step 3 : implémenter** l'interface, `HostApiImpl.kt`, et l'abonnement de `onStateChanged` aux
  `StateFlow` du `VolumeManager`.
- [ ] **Step 4 : vérifier.** `flutter test` passe, `flutter analyze` ne signale rien, et
  `flutter build apk --debug` réussit.
- [ ] **Step 5 : commit**, en cochant T9 — `feat: interface Flutter (disques, déverrouillage) et pont Pigeon`

---

### Task 10 : Essais sur l'appareil, puis version 0.1.0

Les étapes physiques sont faites **par l'utilisateur**. L'agent prépare, guide et consigne les résultats.

**Files :** Create `tools/device/prepare_test_key.sh` ; Modify la feuille de route (résultats mesurés).

- [ ] **Step 1 : écrire `tools/device/prepare_test_key.sh /dev/sdX`** (sudo, **destructeur**).
  - Il refuse si le périphérique est `nvme*`, monté, ou plus grand que 256 Go.
  - Il demande de **retaper le nom du périphérique** pour confirmer.
  - Il crée une GPT avec une partition 1, puis LUKS2 avec les paramètres du vrai disque (argon2id t=7
    m=1048576 p=4, aes-xts-plain64 512, secteurs de 512), puis un ext4 aux options du vrai disque.
  - Il copie `tree/` et une vidéo d'essai donnée en argument facultatif (`--video fichier.mp4`).
  - Il ferme et démonte tout. L'agent ne le lance **pas**.
- [ ] **Step 2 : installer.** Installer l'application sur le téléphone (`flutter install`), passer adb
  en Wi‑Fi (`adb tcpip 5555` puis `adb connect 192.168.0.104:5555`), et laisser l'utilisateur brancher
  la **clé sacrifiée**.
- [ ] **Step 3 : grille d'essai, notée par l'utilisateur dans le rapport.**
  1. L'application propose de s'ouvrir au branchement, la permission est accordée, et la partition
     apparaît en « LUKS2 ».
  2. Le déverrouillage réussit. Noter la durée (chronomètre ou logcat). La notification
     « Disque ouvert » est visible.
  3. « Ouvrir dans Fichiers » montre la racine, et un PDF ou une image s'ouvre.
  4. La vidéo s'ouvre dans VLC. Avance rapide à 50 % puis à 90 % sans erreur. Relever
     `adb logcat -s FluLuksPerf` pour calculer le débit (cible spec : ≥ 20 Mo/s).
  5. « Éjecter » depuis la notification : la racine disparaît de « Fichiers ».
  6. Débranchement **pendant** la lecture VLC : pas de plantage de FluLuks, VLC affiche une erreur,
     et la racine disparaît.
  7. Mauvaise phrase de passe → message clair, puis nouvel essai réussi.
- [ ] **Step 4 : essai sur le vrai disque, en lecture seule.** Débrancher la clé, brancher le disque
  `Mawena` sur le téléphone, puis reprendre les points 1 à 5. La partition NTFS doit afficher « Non pris
  en charge (NTFS) ». Si le disque ne répond pas (alimentation insuffisante), le noter : un hub
  alimenté sera nécessaire (spec § 10).
- [ ] **Step 5 : consigner** les durées, le débit et les anomalies dans la feuille de route, puis
  cocher T10. Commit — `docs(suivi): essais de la phase 2 sur le Pixel 4 XL`.
- [ ] **Step 6 : clôture git flow.**
  1. Fusionner `feature/android-lecture` dans `develop` (`--no-ff`) et supprimer la branche.
  2. Créer `release/0.1.0` depuis `develop`, avec `version: 0.1.0+1` dans `pubspec.yaml`.
  3. Fusionner dans `main` (`--no-ff`), poser l'étiquette `v0.1.0`, refusionner dans `develop`, et
     supprimer `release/0.1.0`.
  4. Cocher « Version v0.1.0 » dans la feuille de route.
