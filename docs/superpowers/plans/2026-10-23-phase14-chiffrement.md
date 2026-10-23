# Portail — Phase 14 : chiffrement de fichiers, de dossiers et coffres — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** Portail chiffre et déchiffre des fichiers et des dossiers (format age, ou ZIP AES-256 pour l'échange), ouvre un
fichier chiffré sans en écrire de copie en clair, et crée des **coffres** (fichiers conteneurs LUKS2 + exFAT ou ext4)
qui s'ouvrent comme des stockages du gestionnaire et de l'application Fichiers ; chaque élément a son mot de passe,
jamais mémorisé. En plus, propositions du planificateur : générateur et force des mots de passe, vérification et
changement du mot de passe d'un fichier, mot de passe et clé de secours d'un coffre, déverrouillage biométrique,
fermeture à l'écran éteint et masquage dans les captures. Fin de phase : version **v1.7.0**.

**Architecture :**
- **Moteur natif (C11)** : `fl_scrypt` (scrypt de référence de Tarsnap, PBKDF2-SHA256 de mbedTLS) ; `fl_age_*` écrit et lit
  le format age v1 (destinataire scrypt, ChaCha20-Poly1305 par morceaux de 64 Kio, HKDF et HMAC de mbedTLS) **en
  flux** sur les `fl_arc_out` / `fl_arc_in` de la phase 9 (lecture à accès aléatoire par morceau) ; libarchive est
  compilé avec mbedTLS pour lire et écrire le ZIP AES ; un ZIP peut s'écrire **dans** un flux age et se lire **depuis**
  un flux age (dossier chiffré) sans fichier intermédiaire. Coffres : disque du moteur posé sur un **fichier** (un
  descripteur du téléphone ou un fichier d'un volume du moteur), formatage LUKS2 **sans table**, gestion des
  emplacements de clé LUKS2 (changer, ajouter, retirer) sûre en cas de coupure.
- **Kotlin** : `crypt/` (`Crypt` : chiffrer, déchiffrer, vérifier, changer le mot de passe, dans le créneau unique des
  opérations de `Transfers` ; `DecryptedSource` : un fichier déchiffré à la volée vu comme une source en lecture
  seule) ; `vaults/` (`Vaults` : registre, création, ouverture par `VolumeManager` comme un disque, fermeture en
  cascade ; `BiometricBox` : secret scellé par le Keystore).
- **Flutter** : `PasswordField` (force, générateur, afficher, copie sensible), dialogues Chiffrer et Déchiffrer, écrans
  des coffres et section « Coffres » de l'accueil.
- **Rien en clair sur le téléphone** : chiffrement et déchiffrement en flux de la source vers la destination ;
  ouverture d'un fichier chiffré par le fournisseur de flux (phase 10), déchiffrée en mémoire ; seuls des noms
  temporaires `.nom.portail-partiel` sur la destination, supprimés en cas d'échec.

**Tech Stack :** C11 (mbedTLS 3.6.5, Argon2, libarchive 3.8.10, lwext4, FatFs : déjà présents ; **scrypt 1.3.2** de
Tarsnap, fichier `crypto_scrypt-ref.c`, BSD-2-Clause, seul code tiers nouveau), Kotlin, Pigeon 27.3.0, Flutter 3.41,
liste de mots **EFF large wordlist** (CC BY 3.0 US, donnée), Python 3 avec `cryptography` (juges, déjà prérequis),
JUnit 4, AndroidX Test.

**Spec :** [docs/superpowers/specs/2026-10-17-fluluks-design.md](../specs/2026-10-17-fluluks-design.md), **§ 22** (et
§ 4.2, § 11, § 13, § 14, § 15, § 18).

## Global Constraints

- **Ordre :** la phase 13 (`feature/ssh`, v1.6.0) est fusionnée d'abord. Branche `feature/chiffrement` créée depuis
  `develop` **après** la v1.6.0, dans l'arbre `../Portail-wt-chiffrement` (sous-modules initialisés avec
  `--reference ../Portail/native/third_party/<n>`, `mbedtls/framework` compris ; `android/gradlew` et le jar du
  wrapper copiés).
- **Commits :** identité `mawena <gamligocharles@gmail.com>`, Conventional Commits en français, **sans aucune
  attribution à Claude**, fichiers nommés (`git commit -- <fichiers>`, nouveaux fichiers ajoutés d'abord). Chaque
  tâche coche ses étapes ici et sa ligne dans la feuille de route, sur la branche **et** aussitôt sur `develop`
  (commit `docs(feuille-de-route): phase 14, Tn faite sur feature/chiffrement` qui ne nomme que la feuille de route,
  au texte identique à celui de la branche).
- **Version :** `v1.7.0`, pubspec `1.7.0+14`.
- **Dépendances :** aucune nouvelle bibliothèque Java, Kotlin ni Dart. Code tiers nouveau : `crypto_scrypt-ref.c` et
  `sysendian.h` de scrypt 1.3.2, copiés **sans modification** dans `native/third_party/scrypt/` avec `COPYRIGHT`,
  empreinte SHA-256 de l'archive d'amont notée dans `native/third_party/PATCHES.md`. Donnée : `assets/words/eff_large.txt`
  (7 776 mots) avec sa licence, ajoutée à « Licences des bibliothèques ». La biométrie passe par
  `android.hardware.biometrics.BiometricPrompt` (API 28+), pas par `androidx.biometric`.
- **Langue :** toute chaîne visible est en français, dans `Fr` (Dart) ou en constantes testées (Kotlin), avec la
  typographie du test `frenchTypography` (` ` avant « : », ` ` avant « ? ! ; »).
- **Secrets :** mots de passe, clés de secours et secrets biométriques en `ByteArray` / `Uint8List` (jamais en `String`
  côté Kotlin), remis à zéro après usage sur tous les chemins ; côté natif en mémoire sécurisée (`fl_secure_alloc`),
  effacés par `fl_memzero` ; jamais dans un journal, un message d'erreur, une notification ou un fichier. Aucun
  mot de passe n'est mémorisé.
- **Clair :** aucun octet en clair d'un contenu chiffré n'est écrit ailleurs que sur la destination choisie par
  l'utilisateur pour une copie en clair ; pas de fichier temporaire dans le cache de l'application.
- **Valeurs fixées** *(décisions du planificateur, voir la fin du plan)* :
  - age : scrypt **log2 N = 18** (r = 8, p = 1 ; 256 Mio) à l'écriture ; lecture acceptée jusqu'à **20** si la mémoire
    le permet (0,8 × mémoire disponible, comme LUKS) ; morceaux de **64 Kio** ; extension `.age`, dossier `.zip.age`
    (ZIP deflate niveau 6 dedans) ;
  - ZIP chiffré : WinZip **AES-256** (AE-2), extension `.zip` ;
  - mots de passe : **8** caractères au moins, sans caractère NUL ; force par estimation d'entropie : très faible
    < 28 bits, faible < 36, moyen < 60, bon < 80, excellent ≥ 80 ; générés : **20** caractères (minuscules,
    majuscules, chiffres, `-_.!?@#%+=`) ou **6** mots EFF séparés par `-` (≈ 77 bits) ; copie sensible effacée après
    **60 s** ;
  - coffres : `nom.luks`, LUKS2 comme le formatage de la phase 8 (aes-xts-plain64, clé de 512 bits, Argon2id t = 4,
    **1 Gio**, 256 Mio si la mémoire manque, p = 4), sans table de partitions ; **exFAT** par défaut, ext4 au choix
    (racine à l'uid et au gid 1000, comme la phase 8) ; taille de **32 Mio** à place libre moins `Archives.reserve`,
    **4 Gio − 1 octet** au plus sur un stockage FAT12/16/32 ; place écrite (zéros) avant le formatage ;
  - emplacements LUKS2 d'un coffre (convention de Portail) : **0** mot de passe, **1** clé de secours, **2** biométrie ;
    clé de secours : **24** caractères Crockford base32 en 6 groupes de 4 (120 bits), même Argon2 que l'emplacement 0 ;
    secret biométrique : **32** octets aléatoires, Argon2id t = 1, **64 Mio**, p = 4 ;
  - coffres ouverts : comptés dans les **8** volumes au plus de `VolumeManager`.
- **Téléphone :** jamais de `flutter drive` sans `--keep-app-running`, jamais `connectedAndroidTest` ; essais
  instrumentés par `adb install -r -t` (application et APK de test), `am instrument -w -r -e class …`, puis
  désinstallation de `com.mawena.fluluks.test` seulement. APK de l'utilisateur sauvegardée avant, rendue après. Les
  essais n'écrivent que dans `/data/local/tmp/fluluks/` ou le dossier privé de l'application.
- **Tests :** à la fin de chaque tâche, `flutter analyze`, `flutter test` et
  `cd android && ./gradlew -q --offline :app:testDebugUnitTest` sont verts ; une tâche qui touche `native/` lance aussi
  `cmake -S native -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFL_TESTS=ON -DFL_SANITIZE=ON`,
  `cmake --build build/native` et `ctest --test-dir build/native --output-on-failure -LE lent` (ASan/UBSan propres),
  puis `tools/android/run_native_tests.sh <numéro-adb>` à la fin des tâches 1 à 4. Jamais `dart format` sur des
  dossiers entiers (le code n'est pas au format de `dart format`).

## Review Focus

1. **Clair ou secret qui fuit** : contenu déchiffré écrit sur le téléphone (temporaire, cache), fichier partiel laissé
   après un échec, mot de passe gardé en `String` Kotlin, dans un journal ou un message. → T5 :
   `noPlaintextOutsideDestination`, `partialRemovedOnFailureAndCancel`, `passwordsZeroedOnAllPaths` ; T6 :
   `decryptedViewNeverWritesToDisk`, `decryptedViewDroppedWithSource` ; T1 : `secretsWipedAfterClose`.
2. **Original perdu** : original supprimé avant la vérification du chiffré, alors qu'il a changé pendant le
   chiffrement, ou mis à la corbeille (copie en clair gardée). → T5 : `originalKeptUntilVerified`,
   `originalKeptIfChangedDuringEncrypt`, `originalKeptIfVerifyFails`, `deleteOriginalIsPermanentNotTrash`.
3. **Format faux ou falsifiable** : fichier age ou ZIP AES illisible par les autres outils ; morceau modifié,
   tronqué, permuté ou mot de passe faux non détectés. → T1 : `ageMatchesPythonJudge`, `cctvScryptVectors`,
   `truncatedStreamRejected`, `swappedChunksRejected`, `flippedByteRejected`, `wrongPasswordIsBadPass` ; T2 :
   `zipAesJudgedByPython`, `zipAesWrongPasswordRefused`.
4. **Coffre rendu inouvrable** par un changement d'emplacement LUKS2 interrompu, le retrait du dernier emplacement ou
   un en-tête que `cryptsetup` refuse. → T3 : `slotChangeSurvivesCutAtEveryWrite`, `lastSlotNeverRemoved`,
   `cryptsetupAcceptsChangedHeader`, `newerSecondaryWins`.
5. **Coffre mal fermé** : stockage porteur éjecté, débranché ou verrouillé avant le coffre ; fichier du coffre ouvert
   supprimé, déplacé ou copié ; interblocage entre le coffre et son volume porteur. → T3 :
   `closingHostFileClosesVaultDisk`, `vaultOnEngineFileNoDeadlock` ; T9 : `ejectHostClosesVaultFirst`,
   `detachHostForceClosesVault`, `autoLockClosesVault`, `openVaultFileProtected`.

---

### Task 1 : scrypt et format age dans le moteur

**Files :**
- Create : `native/third_party/scrypt/` (copie d'amont), `native/src/crypto/scrypt_glue.c` (fournit `PBKDF2_SHA256` sur
  mbedTLS au fichier d'amont, par un `sha256.h` local), `native/include/fluluks/age.h`, `native/src/age/age_writer.c`,
  `native/src/age/age_reader.c`, `native/src/age/age_header.c`, `native/src/age/age_internal.h`,
  `tools/fixtures/agejuge.py` (juge indépendant : `cryptography` de Python, Scrypt, ChaCha20Poly1305, HKDF, HMAC)
- Modify : `native/cmake/third_party.cmake` (cible `fl_scrypt`), `native/CMakeLists.txt`, `native/third_party/PATCHES.md`
- Test : `native/tests/test_scrypt.c`, `native/tests/test_age.c`, `native/tests/data/age/` (vecteurs `scrypt*` du testkit
  age de C2SP/CCTV, avec leur licence)

**Interfaces :**
- Produces (C) :
  - `int fl_scrypt(const void *pass, size_t passlen, const void *salt, size_t saltlen, uint8_t log2n, uint32_t r, uint32_t p, void *out, size_t outlen)` ;
  - `typedef struct fl_agew fl_agew;` `int fl_agew_open(const fl_arc_out *out, const void *pass, size_t passlen, uint8_t log2n, fl_agew **w)`
    (en-tête et nonce écrits), `int fl_agew_write(fl_agew *w, const void *buf, size_t len)`,
    `int fl_agew_close(fl_agew *w, bool commit)` (commit : dernier morceau marqué ; sinon plus rien n'est écrit) ;
  - `typedef struct fl_ager fl_ager;` `int fl_ager_open(const fl_arc_in *in, const void *pass, size_t passlen, uint8_t max_log2n, fl_ager **r)`
    → `FL_ENOTSUP` (pas age, ou destinataire autre que scrypt seul, ou armure ASCII), `FL_EBADPASS`, `FL_ECORRUPT`
    (en-tête, MAC, longueur impossible, dernier morceau invalide), `FL_ENOMEM` (log2 N > max) ;
    `int64_t fl_ager_size(const fl_ager *r)` (taille en clair, calculée), `int64_t fl_ager_pread(fl_ager *r, void *buf, size_t len, uint64_t off)`,
    `void fl_ager_close(fl_ager *r)` ;
  - `int fl_age_peek(const fl_arc_in *in, uint8_t *log2n)` : 1 si c'est un fichier age scrypt (sans mot de passe) ;
  - `int fl_age_rewrap(const fl_arc_in *in, const fl_arc_out *out, const void *old, size_t oldlen, const void *nw, size_t nwlen, uint8_t log2n)`
    (même clé de fichier, nouvel en-tête, charge utile recopiée sans être déchiffrée, puis vérifiée).

- [ ] **Step 1 : copier scrypt 1.3.2** (`lib/crypto/crypto_scrypt-ref.c`, `sysendian.h`, `COPYRIGHT`) sans
  modification ; si le fichier de référence n'existe plus dans cette version : `Ruling:` et arrêt
  (pas de scrypt écrit à la main). Récupérer `age/testdata/scrypt*` du dépôt C2SP/CCTV et vérifier sa licence ; si
  elle ne convient pas : `Ruling:`, juge Python seul.
- [ ] **Step 2 : tests en échec** (`test_scrypt`) : `rfc7914Vectors` (les quatre vecteurs du § 12 du RFC 7914, le
  dernier sous l'étiquette `lent`), `rejectsBadParams` (log2 N = 0 ou > 22, r ou p nuls).
- [ ] **Step 3 : tests en échec** (`test_age`) : `roundTripSizes` (0, 1, 65 535, 65 536, 65 537, 3 × 65 536 octets,
  et 5 Mio aléatoires), `ageMatchesPythonJudge` (le juge déchiffre ce que le moteur écrit, et le moteur déchiffre ce
  que le juge écrit), `cctvScryptVectors` (chaque vecteur : succès et contenu attendus, ou échec attendu),
  `wrongPasswordIsBadPass`, `truncatedStreamRejected` (coupé à la fin d'un morceau plein : refusé, le dernier
  morceau doit porter son drapeau), `swappedChunksRejected`, `flippedByteRejected` (un octet changé dans l'en-tête,
  le MAC ou un morceau), `emptyLastChunkOnlyForEmptyFile`, `preadRandomAccess` (lectures à décalages aléatoires =
  tranches du clair), `workFactorAboveMaxRefused`, `otherRecipientsRefused` (X25519, scrypt avec un second
  destinataire), `rewrapKeepsPayload` (charge utile identique octet pour octet, ancien mot de passe refusé),
  `secretsWipedAfterClose` (crochet de test sur `fl_secure_free` : clé de fichier, clé de charge et clé d'enveloppe
  effacées).
- [ ] **Step 4 : implémentation** selon la spécification age v1 : en-tête `age-encryption.org/v1`, stance
  `-> scrypt <sel b64> <log2N>` (sel = « age-encryption.org/v1/scrypt » ‖ 16 octets), corps = ChaCha20-Poly1305 de la
  clé de fichier (16 octets) sous la clé scrypt et un nonce nul, lignes base64 sans remplissage coupées à 64
  colonnes, MAC HMAC-SHA256 (clé HKDF « header ») sur l'en-tête jusqu'à `---` inclus ; nonce de 16 octets ; clé de
  charge HKDF-SHA256(sel = nonce, info « payload ») ; morceau `i` : nonce de 11 octets gros-boutiste ‖ drapeau de fin.
  Le lecteur vérifie le dernier morceau à l'ouverture (troncature) et chaque morceau lu. Mémoire scrypt et clés en
  mémoire sécurisée.
- [ ] **Step 5 :** `ctest` vert, `tools/android/run_native_tests.sh` vert (temps de scrypt 2^18 sur le Pixel noté
  dans le registre).
- [ ] **Step 6 : commit** `feat(moteur): scrypt et format age, en flux et à accès aléatoire`.

### Task 2 : ZIP chiffré AES-256 et archives ZIP protégées

**Files :**
- Modify : `native/cmake/libarchive/linux/config.h`, `native/cmake/libarchive/android/config.h` (`HAVE_LIBMBEDCRYPTO`,
  `HAVE_MBEDTLS_AES_H`, `HAVE_MBEDTLS_MD_H`, `HAVE_MBEDTLS_PKCS5_H`), `native/cmake/third_party.cmake` (`fl_libarchive`
  lié à `fl_mbedcrypto`), `native/include/fluluks/archive.h`, `native/src/archive/reader.c`, `native/src/archive/writer.c`,
  `native/third_party/PATCHES.md`
- Create : `tools/fixtures/zipaesjuge.py` (PBKDF2-SHA1 1000 tours, AES-CTR petit-boutiste, HMAC-SHA1 de 10 octets, zlib)
- Test : `native/tests/test_archive_crypt.c` ; archives de `tools/fixtures/make_archive_fixtures.sh` (+ un ZIP AES et un
  ZIP « ZipCrypto » écrits par le juge)

**Interfaces :**
- Produces (C) : `int fl_arcw_open2(const fl_arc_out *out, const void *pass, size_t passlen, fl_arc_writer **w)`
  (`pass` NULL : comme `fl_arcw_open` ; sinon AES-256 sur toutes les entrées de fichier) ;
  `int fl_arc_open2(const fl_arc_in *in, const void *pass, size_t passlen, fl_arc_reader **out)` ;
  `fl_arc_read` rend `FL_EBADPASS` pour une entrée chiffrée au mauvais mot de passe, `FL_ENOTSUP` sans mot de passe ;
  `int fl_arc_open_age(const fl_arc_in *in, const void *pass, size_t passlen, uint8_t max_log2n, fl_arc_reader **out)`
  et `int fl_arcw_open_age(const fl_arc_out *out, const void *pass, size_t passlen, uint8_t log2n, fl_arc_writer **w)`
  (ZIP en clair à l'intérieur d'un flux age, sans intermédiaire).

- [ ] **Step 1 : tests en échec** : `zipAesJudgedByPython` (fichiers de 0 à 5 Mio, noms UTF-8, dossier vide :
  le juge relit tout), `judgeZipAesReadByEngine`, `zipCryptoReadWithPassword`, `zipAesWrongPasswordRefused`
  (`FL_EBADPASS`, aucun octet rendu), `noPasswordStillSaysEncrypted` (comportement de la phase 9 inchangé),
  `zipInsideAgeRoundTrip` (dossier de 200 fichiers ; le juge age puis `zipfile` de Python relisent),
  `sevenZipEncryptedStillRefused`, `passphraseCopyWiped` (libarchive efface sa copie : relu dans le code d'amont et
  vérifié par un crochet `free`).
- [ ] **Step 2 : implémentation** : options `zip:encryption=aes256` et `archive_write_set_passphrase` ;
  `archive_read_add_passphrase` ; les phrases passent en C par une copie terminée par NUL en mémoire sécurisée.
  `mbedtls_pkcs5_pbkdf2_hmac` (dépréciée mais présente dans 3.6.5, sans `MBEDTLS_DEPRECATED_REMOVED`) noté dans
  `PATCHES.md`.
- [ ] **Step 3 :** `ctest` vert (archives de la phase 9 comprises), tests natifs sur le Pixel verts.
- [ ] **Step 4 : commit** `feat(moteur): ZIP chiffré AES-256 et archives ZIP protégées par mot de passe`.

### Task 3 : coffres dans le moteur (disque sur un fichier, formatage sans table, emplacements LUKS2)

**Files :**
- Create : `native/src/blockdev/fsfile_bdev.c` (périphérique bloc sur un `fl_file` d'un autre volume),
  `native/src/luks/luks2_slots.c`, `tools/fixtures/verify_vault_with_kernel.sh`
- Modify : `native/include/fluluks/blockdev.h`, `native/src/blockdev/file_bdev.c` (ouverture sur un descripteur),
  `native/include/fluluks/luks.h`, `native/src/luks/luks2.c`, `native/include/fluluks/format.h`,
  `native/src/format/format.c`
- Test : `native/tests/test_vault.c`, `native/tests/test_luks2_slots.c` (avec `support/fault_bdev`)

**Interfaces :**
- Produces (C) :
  - `int fl_fd_bdev_open(int fd, uint32_t sector_size, bool read_only, fl_bdev **out)` (le descripteur appartient au
    périphérique) ; `int fl_fsfile_bdev_new(fl_file *f, uint64_t size, bool read_only, fl_bdev **out)` (ne possède pas `f`) ;
  - `fl_fmt_opts.no_table` : LUKS2 et système de fichiers sur tout le périphérique, sans table ni effacement de
    secteurs de table ;
  - `int fl_luks2_slot_set(fl_bdev *dev, int slot, const void *auth, size_t authlen, const void *nw, size_t nwlen, uint32_t kdf_mem_kib, uint32_t kdf_time)`
    (crée ou remplace l'emplacement `slot` ; `auth` ouvre n'importe quel emplacement) ;
    `int fl_luks2_slot_remove(fl_bdev *dev, int slot, const void *auth, size_t authlen)` (`FL_EBUSY` si c'est le
    dernier) ; `int fl_luks_slots(fl_bdev *dev, uint32_t *mask)` ; `int fl_luks_unlock_slot(fl_luks *l, int slot, const void *pass, size_t passlen, fl_bdev **decrypted)`
    (`slot` −1 : tous) ; LUKS1 : `FL_ENOTSUP` pour les modifications.

- [ ] **Step 1 : tests en échec** (`test_vault`) : `vaultFormatHasNoTable` (scan : table `none`, partie 0 LUKS2),
  `vaultExfatAndExt4MountAndWrite`, `fdBdevOwnsDescriptor`, `vaultOnEngineFileRoundTrip` (coffre de 64 Mio dans un
  fichier d'une image ext4 puis d'une image exFAT : écriture, démontage, `e2fsck -fn` de l'hôte et `fsck.exfat -n`
  du coffre propres), `vaultOnEngineFileNoDeadlock` (4 fils : écritures dans le coffre et dans son hôte en même
  temps, 10 s, sans blocage, propre sous ASan), `hostFat32Caps4GiB`
  (`FL_EFBIG` au-delà de 4 Gio − 1 octet), `cryptsetupOpensVault` (`cryptsetup luksDump` et
  `cryptsetup open --test-passphrase` sur le fichier, sans root ; test ignoré si `cryptsetup` est absent, en le
  disant).
- [ ] **Step 2 : tests en échec** (`test_luks2_slots`) : `changeSlot0` (nouveau mot de passe ouvre, ancien refusé, données
  intactes), `addRecoveryAndBiometricSlots`, `removeSlot`, `lastSlotNeverRemoved`, `wrongAuthChangesNothing` (en-têtes
  identiques octet pour octet), `slotChangeSurvivesCutAtEveryWrite` (coupure par `fault_bdev` à chaque écriture
  possible : ensuite l'ancien **ou** le nouveau mot de passe ouvre, jamais aucun), `newerSecondaryWins` (primaire
  abîmé après la coupure : la secondaire au seqid plus grand est prise ; déjà vrai en lecture, vérifié pour l'écriture),
  `cryptsetupAcceptsChangedHeader` (`luksDump` et `open --test-passphrase --key-slot n` pour chaque emplacement),
  `unlockSlotSkipsOthers` (l'emplacement 2 s'ouvre sans calculer l'Argon2 de 1 Gio de l'emplacement 0),
  `luks1Refused`, `jsonStaysUnder12KiB`.
- [ ] **Step 3 : implémentation.** Emplacement écrit dans une zone libre de la zone des emplacements (32 Kio à 16 Mio,
  ≈ 64 emplacements de 252 Kio) ; puis JSON réécrit avec cJSON (emplacement, `digests.0.keyslots`), seqid + 1,
  sommes, en-tête **primaire puis secondaire**, vidage après chaque écriture ; l'ancienne zone n'est effacée
  qu'après. Clé maîtresse obtenue par `auth`, vérifiée par le digest, en mémoire sécurisée, jamais rendue.
  `fl_fsfile_bdev` : lectures et écritures par `fl_file_pread`/`pwrite` du volume hôte (verrou du coffre puis
  verrou de l'hôte, jamais l'inverse).
- [ ] **Step 4 :** `verify_vault_with_kernel.sh` (sudo, sur le PC) : `cryptsetup open`, `fsck.exfat -n` ou `e2fsck -fn`,
  montage, comparaison avec le manifeste écrit par `test_vault` ; note dans le registre.
- [ ] **Step 5 : commit** `feat(moteur): coffres LUKS2 dans un fichier et gestion des emplacements de clé`.

### Task 4 : poignées JNI et moteur Kotlin

**Files :**
- Modify : `native/jni/handles.h`, `native/jni/handles.c` (parent explicite par type ; nouveaux types `T_AGEW`, `T_AGER`),
  `native/jni/fluluks_jni.c`, `android/app/src/main/kotlin/com/mawena/fluluks/engine/Engine.kt`, `NativeEngine.kt`,
  `android/app/src/test/kotlin/com/mawena/fluluks/MemEngine.kt` (faux chiffrement réversible pour les tests JVM)
- Test : `native/tests/test_handles.c` (cas ajoutés), `android/app/src/androidTest/kotlin/com/mawena/fluluks/EngineTest.kt`
  (cas ajoutés)

**Interfaces :**
- Produces (C, `fj_*`) : `fj_agew_open_fd(fd, pass, n, log2n)`, `fj_agew_open_file(file, pass, n, log2n)`, `fj_agew_write`,
  `fj_agew_close(h, commit)` ; `fj_ager_open_fd(fd, pass, n)`, `fj_ager_open_file(file, size, pass, n)`, `fj_ager_size`,
  `fj_ager_pread`, `fj_ager_close` ; `fj_age_peek_fd/file` ; `fj_age_rewrap_fd/file` ; `fj_arc_open_*` et
  `fj_arcw_open_*` reçoivent `pass` et `mode` (`plain`, `aes`, `age`) ; `fj_open_disk_fd(fd, read_only)` ;
  `fj_open_disk_file(file, size)` (disque **enfant du fichier** : fermer le fichier, démonter ou fermer le disque hôte
  démonte d'abord de force le coffre) ; `fj_format` accepte `"table":"none"` ; `fj_luks_slots(disk)` (JSON
  `{"version", "slots":[…]}`), `fj_luks_slot_set`, `fj_luks_slot_remove` (refus `FL_EBUSY` si un volume du disque
  est monté) ; `fj_mount_slot(disk, part, slot, pass, rw)`. Toute phrase est effacée par le natif sur tous les chemins.
- Produces (Kotlin, `Engine`) : `ageWriterOpen(target: NativeOut, pass: ByteArray): Long`, `ageWrite`, `ageWriterClose`,
  `ageReaderOpen(src: NativeIn, pass: ByteArray): Long`, `ageSize`, `agePread`, `ageReaderClose`, `agePeek`,
  `ageRewrap` ; `arcOpen*`/`arcwOpen*` avec `pass: ByteArray?` et `mode` ; `openDiskFd(fd: Int, readOnly: Boolean)`,
  `openDiskOnFile(file: Long, size: Long)`, `luksSlots(disk: Long): ByteArray`,
  `luksSlotSet(disk, slot, auth: ByteArray, new: ByteArray, kdfMemKib: Int, kdfTime: Int): Int`,
  `luksSlotRemove(disk, slot, auth): Int`, `mountSlot(disk, part, slot, pass, writable): Long`.

- [ ] **Step 1 : tests en échec** (`test_handles`) : `closingHostFileClosesVaultDisk` (fichier hôte fermé → volume du
  coffre démonté et synchronisé, poignées invalides, rien ne fuit), `ejectHostDiskCascades`, `agerChildOfFile`,
  `staleAgeHandleIsEinval`, `slotChangeRefusedWhileMounted`, `passWipedOnEveryPath` (crochet : copie native effacée
  après succès comme après échec).
- [ ] **Step 2 : implémentation** ; `fj_debug_used` revient à son compte initial dans chaque test.
- [ ] **Step 3 : essais instrumentés en échec puis verts** (`EngineTest`, sur le Pixel, dossier privé) :
  `ageRoundTripOnPhone`, `zipAesRoundTripOnPhone`, `vaultCreateMountWriteOnPhone` (coffre de 64 Mio par
  `openDiskFd`), `slotChangeOnPhone`.
- [ ] **Step 4 : commit** `feat(moteur): poignées de chiffrement et de coffres pour Kotlin`.

### Task 5 : chiffrer et déchiffrer fichiers et dossiers (Kotlin)

**Files :**
- Create : `android/app/src/main/kotlin/com/mawena/fluluks/crypt/Crypt.kt`, `crypt/CryptFormats.kt` (reconnaissance :
  `age`, `zip-aes`, `zip-legacy`, `luks`, `none`), `crypt/Passwords.kt` (règles, messages)
- Modify : `files/Transfers.kt` (types `encrypt`, `decrypt`, `verify` dans le créneau unique), `files/Archives.kt`
  (extraction avec mot de passe, depuis un flux age), `bridge/HostApiImpl.kt`, `pigeons/fluluks_api.dart` (+ générés)
- Test : `android/app/src/test/kotlin/com/mawena/fluluks/CryptTest.kt`, `CryptFormatsTest.kt`, `PasswordsTest.kt`
  (`MemEngine`, sources en mémoire qui enregistrent chaque écriture)

**Interfaces :**
- Produces (Kotlin) : `object Crypt` : `fun startEncrypt(req: EncryptRequest, password: ByteArray)` avec
  `data class EncryptRequest(srcId, paths, dstId, dstDir, format: "age"|"zip", together: Boolean, name: String?, deleteOriginal: Boolean)` ;
  `fun startDecrypt(srcId, path, dstId, dstDir, password: ByteArray, extractFolder: Boolean)` ;
  `fun startVerify(srcId, path, password: ByteArray)` ; `suspend fun changePassword(srcId, path, old: ByteArray, new: ByteArray)` ;
  `fun info(srcId, path): EncryptedInfo` (`format`, `folder` pour un `.zip.age`, `workFactor`).
- Produces (Pigeon) : `class EncryptedInfoMsg { String format; bool folder; }` ; `EncryptedInfoMsg encryptedInfo(String sourceId, String path)` ;
  `void startEncrypt(String srcId, List<String> paths, String dstId, String dstDir, String format, bool together, String? name, bool deleteOriginal, Uint8List password)` ;
  `void startDecrypt(String srcId, String path, String dstId, String dstDir, bool extractFolder, Uint8List password)` ;
  `void startVerify(String srcId, String path, Uint8List password)` ;
  `@async void changeFilePassword(String srcId, String path, Uint8List oldPassword, Uint8List newPassword)` ;
  `startExtract` reçoit `Uint8List? password` ; `TransferMsg.kind` ∈ … `encrypt`, `decrypt`, `verify` ;
  erreur `badPassword` (« Mot de passe incorrect ») distincte des autres.

- [ ] **Step 1 : tests en échec** : `encryptFileToAgeSameFolder` (`rapport.pdf` → `rapport.pdf.age`, nom unique),
  `encryptFolderToZipAge`, `encryptSelectionTogether` (« Archive.zip.age » ou le nom choisi), `encryptToZipAes`,
  `encryptToRemoteDestination` (tube de la phase 10), `noPlaintextOutsideDestination` (seules écritures : le partiel
  puis le nom final sur la destination), `partialRemovedOnFailureAndCancel`, `originalKeptUntilVerified`,
  `originalKeptIfChangedDuringEncrypt` (taille ou date changée : message, original gardé),
  `originalKeptIfVerifyFails`, `deleteOriginalIsPermanentNotTrash`, `openVaultFileNeverEncrypted` (T9 branche le
  test), `decryptAgeFile` (`x.pdf.age` → `x.pdf`), `decryptFolderExtractsWithoutZip` (aucun `.zip` écrit),
  `decryptKeepZipWhenAsked`, `wrongPasswordIsBadPassword`, `verifyReportsIntactOrDamaged`,
  `changePasswordReplacesAtomically` (partiel puis renommage ; ancien fichier gardé si échec),
  `passwordsZeroedOnAllPaths`, `lowMemoryRefusesScrypt` (message de `VolumeManager.MSG_LOW_MEMORY`),
  `autoLockWaitsForCrypt` (`Transfers.isBusy`) ; `PasswordsTest` : `minLength8`, `nulRefused` ; `CryptFormatsTest` :
  reconnaissance par les premiers octets (`age-encryption.org/v1`, `PK` + drapeau de chiffrement, `LUKS\xba\xbe`),
  pas seulement l'extension.
- [ ] **Step 2 : implémentation.** Chiffrement : lecture de la source en flux, empreinte SHA-256 du clair (ou du ZIP en
  clair) calculée au passage, écriture par `NativeOut` vers le partiel ; vérification : relecture complète du
  chiffré (déchiffrement sans écriture) et comparaison d'empreinte, ZIP relu (entrées et tailles) ; renommage ;
  seulement ensuite, si demandé, suppression **définitive** de l'original (sans corbeille). Déchiffrement : `NativeIn`
  de la phase 9 (descripteur, fichier du moteur, mandataire distant) ; dossier : `fl_arc_open_age` puis l'extraction
  de la phase 9 (chemins hostiles, place, deux passages).
- [ ] **Step 3 : commit** `feat(chiffrement): chiffrer, déchiffrer, vérifier et changer le mot de passe d'un fichier`.

### Task 6 : ouvrir un fichier chiffré sans copie en clair

**Files :**
- Create : `android/app/src/main/kotlin/com/mawena/fluluks/crypt/DecryptedSource.kt`
- Modify : `files/Sources.kt` (sources déchiffrées enregistrées), `provider/StreamProvider.kt`, `bridge/HostApiImpl.kt`,
  `pigeons/fluluks_api.dart` (+ générés)
- Test : `android/app/src/test/kotlin/com/mawena/fluluks/DecryptedSourceTest.kt` ;
  `android/app/src/androidTest/kotlin/com/mawena/fluluks/CryptInstrumentedTest.kt`

**Interfaces :**
- Produces : `class DecryptedSource(id = "dec:<jeton>", origin: FileSource, path, reader: Long) : FileSource` en lecture seule
  (`readOnlyReason` « fichier déchiffré en mémoire »), un seul fichier `/<nom sans .age>` ; `openReadAt` par
  `agePread` ; `contentUri` par `StreamProvider` ; Pigeon `@async SourceMsg openDecrypted(String srcId, String path, Uint8List password)`,
  `void closeDecrypted(String sourceId)` ; `SourceMsg.kind` = `decrypted`.

- [ ] **Step 1 : tests en échec** : `decryptedViewReadsAtAnyOffset`, `decryptedViewIsReadOnly` (toute écriture refusée
  avec le message), `decryptedViewNeverWritesToDisk` (aucune écriture sur aucune source ni dans le cache),
  `decryptedViewDroppedWithSource` (source d'origine fermée, verrouillée ou débranchée → source déchiffrée retirée,
  liens du fournisseur expirés), `closeDecryptedWipes` (lecteur natif fermé), `atMostFourDecryptedViews` (la
  cinquième ferme la plus ancienne), `folderAgeNotOpenable` (un `.zip.age` se déchiffre ou s'extrait, il ne s'ouvre
  pas en vue).
- [ ] **Step 2 : implémentation** ; les lecteurs de Portail (images, texte en lecture seule, musique, vidéo) et
  « Ouvrir avec » reçoivent `sourceId = dec:…` sans autre changement ; « Partager » demande confirmation (« Le
  destinataire recevra le fichier en clair »).
- [ ] **Step 3 : essais instrumentés** (`CryptInstrumentedTest`) : `decryptedVideoSeeks` (vidéo de 20 Mio chiffrée, lue
  par le fournisseur de flux à 50 % et 90 %), `decryptedImageViewer`.
- [ ] **Step 4 : commit** `feat(chiffrement): ouvrir un fichier chiffré sans copie en clair`.

### Task 7 : saisie des mots de passe — force, générateur, copie sensible *(proposition du planificateur)*

**Files :**
- Create : `lib/src/widgets/password_field.dart`, `lib/src/crypto/password_strength.dart`,
  `lib/src/crypto/password_generator.dart`, `assets/words/eff_large.txt`, `assets/words/LICENCE-EFF.txt`
- Modify : `pubspec.yaml` (asset), `lib/src/licenses.dart`, `lib/src/strings_fr.dart`, `bridge/HostApiImpl.kt`
  (`copySecret`), `pigeons/fluluks_api.dart` (+ générés), `lib/src/widgets/unlock_dialog.dart` et
  `lib/src/screens/format_screen.dart` (même champ)
- Test : `test/password_field_test.dart`, `test/password_strength_test.dart`, `test/password_generator_test.dart`

**Interfaces :**
- Produces : `enum Strength { veryWeak, weak, fair, good, excellent }` ; `Strength estimate(String pw)` et
  `double entropyBits(String pw)` (classes de caractères, longueur, pénalités : répétitions, suites `abcd`/`1234`,
  mots du clavier `azerty`/`qwerty`, mots de la liste EFF comptés comme des mots) ; `String generatePassword({int length = 20})`,
  `String generatePassphrase({int words = 6})` (`Random.secure()`) ; `PasswordField(controller, {confirm, showStrength, allowGenerate, onGenerated})` ;
  Pigeon `void copySecret(Uint8List text)` (`ClipDescription.EXTRA_IS_SENSITIVE`, effacé après 60 s s'il n'a pas changé).

- [ ] **Step 1 : tests en échec** : `strengthLevels` (`motdepasse` très faible, `Tr0ub4dour&3` moyen, 6 mots EFF
  bon ou excellent, 20 caractères générés excellent), `sequencesPenalized`, `generatorUsesSecureRandomAndAllClasses`,
  `passphraseHasSixDistinctListWords`, `wordListIs7776Unique`, `confirmMismatchBlocks`, `under8Blocks`,
  `generateFillsBothFields`, `copySecretSendsBytes`, `strengthAnnouncedToTalkBack` (« Force : bon »),
  `fieldClearedOnDispose`.
- [ ] **Step 2 : implémentation** ; avertissement permanent sous le champ de création : « Sans ce mot de passe, rien ne
  pourra être récupéré » ; la phase 5 (déverrouillage) garde son comportement (pas de force ni de générateur).
- [ ] **Step 3 : commit** `feat(chiffrement): force des mots de passe, générateur et copie protégée`.

### Task 8 : écrans de chiffrement et de déchiffrement

**Files :**
- Create : `lib/src/screens/encrypt_sheet.dart`, `lib/src/widgets/decrypt_dialog.dart`
- Modify : `lib/src/screens/explorer_screen.dart` (⋮ d'un élément et barre de sélection : **Chiffrer…** ; ⋮ d'un fichier
  chiffré : **Déchiffrer…**, **Vérifier**, **Changer le mot de passe** ; toucher un fichier chiffré), `lib/src/file_types.dart`
  (type « chiffré », icône cadenas), `lib/src/widgets/transfer_bar.dart` (libellés `encrypt`, `decrypt`, `verify`),
  `lib/src/state/app_state.dart`, `lib/src/strings_fr.dart`, `test/fake_host_api.dart`
- Test : `test/encrypt_screens_test.dart`

- [ ] **Step 1 : tests en échec** : `encryptSheetDefaults` (Chiffrement fort, même dossier, original gardé),
  `zipFormatShowsNamesWarning`, `deleteOriginalExplainsFlash`, `severalSelectedOffersTogether`,
  `destinationPickerIncludesRemote`, `tapAgeFileAsksPasswordThenOffersOpenDecryptVerify`,
  `tapZipAgeOffersExtract`, `tapProtectedZipAsksPassword` (extraction de la phase 9 avec mot de passe),
  `badPasswordStaysInDialog` (champ vidé, message, nouvel essai), `resultOffersShare` (« Partager le fichier
  chiffré » ; rappel « Le destinataire déchiffre avec age » ou « avec 7-Zip »), `passwordBytesZeroedAfterCall`,
  `lockIconOnEncryptedFiles`.
- [ ] **Step 2 : implémentation** ; écrans ajoutés au banc `test/screen_harness.dart` (`encrypt`, `decrypt`) :
  48 dp, contraste, 200 %, TalkBack, pas de texte sous la barre de navigation.
- [ ] **Step 3 : commit** `feat(chiffrement): Chiffrer et Déchiffrer dans l'explorateur`.

### Task 9 : coffres (Kotlin)

**Files :**
- Create : `android/app/src/main/kotlin/com/mawena/fluluks/vaults/Vaults.kt`, `vaults/VaultStore.kt` (`vaults.json` :
  id, nom, clé stable de la source, chemin, taille, système de fichiers, rôles des emplacements, biométrie ; aucun
  secret ; écriture par temporaire puis renommage)
- Modify : `volumes/VolumeManager.kt` (entrées de disque de type coffre, non publiées dans `disks` ; `openVault`,
  éjection et débranchement en cascade), `volumes/DiskState.kt` (`VolumeState.vaultId`), `files/Sources.kt`
  (`kind` = `vault`), `files/LocalSource.kt`, `files/DiskSource.kt` et `files/Transfers.kt` (fichier d'un coffre ouvert
  protégé), `provider/FluLuksDocumentsProvider.kt` (racine « Coffre <nom> », même règle), `bridge/HostApiImpl.kt`,
  `pigeons/fluluks_api.dart` (+ générés)
- Test : `android/app/src/test/kotlin/com/mawena/fluluks/VaultsTest.kt`, `VaultStoreTest.kt`, `VolumeManagerTest.kt`
  (cas ajoutés)

**Interfaces :**
- Produces (Kotlin) : `object Vaults` : `suspend fun create(srcId, dir, name, sizeBytes, fs, password: ByteArray): VaultInfo`
  (dans le créneau des opérations, type `vault`, annulable), `suspend fun open(vaultId | srcId + path, password: ByteArray, writable): VolumeState`,
  `suspend fun openWithSlot(vaultId, slot, secret: ByteArray, writable)`, `fun close(vaultId)`, `fun forget(vaultId)`,
  `fun list(): List<VaultInfo>`, `fun isOpenFile(sourceId, path): Boolean` ;
  `VaultInfo(id, name, sourceKey, sourceLabel, path, sizeBytes, fs, state: closed|open|unavailable, volumeId, hasRecovery, biometric, luksVersion)`.
- Produces (Pigeon) : `class VaultMsg { … mêmes champs … }` ; `List<VaultMsg> listVaults()` ;
  `@async VaultMsg createVault(String srcId, String dir, String name, int sizeBytes, String fs, Uint8List password)` ;
  `@async VolumeMsg openVault(String? vaultId, String? srcId, String? path, Uint8List password, bool writable)` ;
  `void closeVault(String vaultId)`, `void forgetVault(String vaultId)` ; `VolumeMsg.vaultId` ; FlutterApi
  `onStateChanged` porte aussi la liste des coffres.

- [ ] **Step 1 : tests en échec** : `createWritesZerosThenFormats` (partiel `.nom.luks.portail-partiel`, progression
  0 à 80 % pour la place, puis formatage ; renommé seulement à la fin), `createCancelRemovesPartial`,
  `createChecksSizeBounds` (32 Mio, place libre moins la réserve, 4 Gio − 1 sur FAT32), `createRefusedOnRemote`,
  `createRefusedInsideVault`, `openOnPhoneUsesFd`, `openOnDiskUsesEngineFile`, `vaultVolumeIsEncryptedAndListed`
  (source `vault`, racine du provider), `vaultNotInDisksList`, `ejectHostClosesVaultFirst` (le coffre synchronisé et
  démonté avant le volume porteur ; échec du coffre → éjection de l'hôte refusée avec le message),
  `detachHostForceClosesVault` (avertissement « Coffre fermé : son disque a été débranché »),
  `sdCardRemovedClosesVault`, `autoLockClosesVault`, `openVaultFileProtected` (supprimer, renommer, déplacer, copier,
  chiffrer, écraser par le provider : refus « Fermez d'abord le coffre »), `maxVolumesCountsVaults`,
  `storeSurvivesDamage` (fichier abîmé → liste vide, rien de perdu sur les coffres eux-mêmes), `notALuksFileRefused`
  (« Ce fichier n'est pas un coffre »), `linuxLuks1ContainerOpens` (image LUKS1 + ext4 du jeu de tests),
  `dirtyExfatOpensReadOnlyWithAdvice` (raison de la phase 7 complétée : « vérifiez-le sous Linux :
  cryptsetup open, puis fsck.exfat »).
- [ ] **Step 2 : implémentation.** Un coffre est un `DiskEntry` de plus dans `VolumeManager` (pas de `UsbDevice`),
  ouvert par `openDiskFd` (`ParcelFileDescriptor.open(...).detachFd()` sur le fichier vérifié par `LocalSource`) ou
  `openDiskOnFile` (fichier du volume porteur ouvert en écriture, gardé jusqu'à la fermeture) ; scan, puis
  `mount(part 0)` ; `VolumeService` le garde ouvert comme un disque. Les opérations « éjecter », « débrancher »,
  « verrouiller » d'un volume ferment d'abord les coffres qu'il porte.
- [ ] **Step 3 : commit** `feat(coffres): créer et ouvrir des coffres comme des stockages`.

### Task 10 : coffres (écrans)

**Files :**
- Create : `lib/src/screens/vault_create_screen.dart`, `lib/src/widgets/vaults_card.dart`, `lib/src/widgets/vault_tile.dart`
- Modify : `lib/src/screens/home_screen.dart` (section « Coffres »), `lib/src/screens/explorer_screen.dart` (« Nouveau
  coffre… » dans le menu de création ; toucher un `.luks` ou un fichier LUKS : « Ouvrir le coffre »),
  `lib/src/widgets/unlock_dialog.dart` (variante coffre), `lib/src/widgets/source_tile.dart` (icône coffre),
  `lib/src/state/app_state.dart`, `lib/src/strings_fr.dart`, `test/fake_host_api.dart`
- Test : `test/vault_screens_test.dart`

- [ ] **Step 1 : tests en échec** : `createFormDefaults` (exFAT, 1 Gio, dossier courant), `sizeChoicesRespectFreeSpace`,
  `fat32HostCapsSize`, `ext4ExplainsLinuxOwnership`, `noRecoveryWarningAlwaysShown`, `createShowsProgressAndCancel`,
  `homeListsVaultsWithState` (fermé, ouvert, indisponible avec le nom du stockage absent), `openThenBrowse`
  (l'explorateur s'ouvre à la racine du coffre), `closeFromTile`, `forgetKeepsFile` (« Retirer de la liste » ne
  supprime rien), `tapLuksFileOffersOpen`, `readOnlyCheckboxHonoursSetting`.
- [ ] **Step 2 : implémentation** ; tuile : nom, taille, système de fichiers, emplacement ; menu ⋮ : Ouvrir/Fermer,
  Parcourir, Ouvrir dans Fichiers, Changer le mot de passe (T11), Clé de secours (T11), Déverrouillage biométrique
  (T12), Afficher l'emplacement, Retirer de la liste. Écrans ajoutés au banc d'accessibilité (`vaultCreate`,
  `homeVaults`).
- [ ] **Step 3 : commit** `feat(coffres): écrans de création, section Coffres de l'accueil`.

### Task 11 : mot de passe et clé de secours d'un coffre *(proposition du planificateur)*

**Files :** Modify `vaults/Vaults.kt`, `bridge/HostApiImpl.kt`, `pigeons/fluluks_api.dart` (+ générés),
`lib/src/widgets/vault_tile.dart`, `lib/src/strings_fr.dart` ; Create `lib/src/screens/vault_keys_screen.dart` ;
Test `VaultsTest.kt` (cas ajoutés), `test/vault_keys_test.dart`

**Interfaces :** Pigeon `@async void changeVaultPassword(String vaultId, Uint8List oldPassword, Uint8List newPassword)`,
`@async Uint8List addRecoveryKey(String vaultId, Uint8List password)` (clé rendue une seule fois),
`@async void removeRecoveryKey(String vaultId, Uint8List password)` ; la clé de secours se saisit dans le dialogue
d'ouverture (lien « Utiliser la clé de secours »).

- [ ] **Step 1 : tests en échec** : `changePasswordRequiresClosedVault` (« Fermez d'abord le coffre »),
  `changePasswordKeepsSlot0`, `recoveryKeyFormat` (24 caractères Crockford, groupes de 4, sans I L O U),
  `recoveryKeyOpensVault` (tirets et casse ignorés), `recoveryKeyShownOnce` (écran avec Copier et « J'ai noté la
  clé », jamais réaffichée), `removeRecoveryKeepsPassword`, `luks1VaultOffersNothing` (« Coffre LUKS1 : changez la
  phrase sous Linux »), `foreignSlotsKept` (emplacements inconnus d'un coffre créé sous Linux jamais touchés).
- [ ] **Step 2 : implémentation** (`luksSlotSet` / `luksSlotRemove` de T4, coffre fermé, progression pendant Argon2).
- [ ] **Step 3 : commit** `feat(coffres): changer le mot de passe, clé de secours`.

### Task 12 : déverrouillage biométrique d'un coffre *(proposition du planificateur)*

**Files :**
- Create : `android/app/src/main/kotlin/com/mawena/fluluks/vaults/BiometricBox.kt`
- Modify : `AndroidManifest.xml` (`android.permission.USE_BIOMETRIC`), `vaults/Vaults.kt`, `MainActivity.kt`,
  `bridge/HostApiImpl.kt`, `pigeons/fluluks_api.dart` (+ générés), `lib/src/widgets/vault_tile.dart`,
  `lib/src/widgets/unlock_dialog.dart`, `lib/src/strings_fr.dart`
- Test : `BiometricBoxLogicTest.kt` (logique pure), `android/app/src/androidTest/kotlin/com/mawena/fluluks/BiometricBoxInstrumentedTest.kt`,
  `test/vault_biometric_test.dart`

**Interfaces :** `class BiometricBox(alias = "portail-coffre-<id>")` : clé AES-256-GCM du Keystore,
`setUserAuthenticationRequired(true)`, biométrie forte seule (`setUserAuthenticationParameters(0, AUTH_BIOMETRIC_STRONG)`
dès l'API 30), `setInvalidatedByBiometricEnrollment(true)` ; secret scellé dans `vaults/<id>.bio` ;
Pigeon `@async void enableBiometric(String vaultId, Uint8List password)`, `@async VolumeMsg openVaultBiometric(String vaultId, bool writable)`,
`@async void disableBiometric(String vaultId, Uint8List? password)` ; `bool biometricAvailable()`.

- [ ] **Step 1 : tests en échec** : `hiddenWithoutStrongBiometric` (API < 28 ou aucune biométrie forte : option
  absente, avec la raison), `enableAddsSlot2AndSealsSecret`, `secretNeverEqualsPassword`,
  `enrollmentChangeDisablesBiometric` (`KeyPermanentlyInvalidatedException` → biométrie désactivée, message, ouverture
  par mot de passe proposée), `cancelFallsBackToPassword`, `disableWipesKeyAndSlot`, `forgetVaultWipesKey` ;
  instrumenté : `biometricSecretSealedWithAuthKey` (clé matérielle, `isUserAuthenticationRequired`).
- [ ] **Step 2 : implémentation** par `android.hardware.biometrics.BiometricPrompt` et un `CryptoObject` (Cipher) ;
  titre « Ouvrir le coffre <nom> » ; ouverture par `mountSlot(…, 2, …)` (Argon2 de 64 Mio seulement). Le Pixel
  4 XL n'a pas de lecteur d'empreinte : son déverrouillage par le visage est une biométrie forte.
- [ ] **Step 3 : commit** `feat(coffres): déverrouillage biométrique facultatif`.

### Task 13 : confidentialité — fermeture à l'écran éteint, masquage dans les captures *(proposition du planificateur)*

**Files :** Modify `service/VolumeService.kt` (récepteur `ACTION_SCREEN_OFF`), `vaults/Vaults.kt`, `files/UiPrefs.kt`,
`MainActivity.kt` (`FLAG_SECURE` selon l'état), `lib/src/screens/settings_screen.dart` (Réglages → Confidentialité),
`pigeons/fluluks_api.dart` (+ générés), `lib/src/strings_fr.dart` ; Test `VaultPrivacyTest.kt`,
`test/settings_screen_test.dart` (cas ajoutés)

- [ ] **Step 1 : tests en échec** : `screenOffClosesVaultsWhenEnabled`, `screenOffWaitsForRunningOperation` (fermés à
  la fin de l'opération en cours), `screenOffLeavesDisksAlone` (seuls les coffres), `secureFlagWhileVaultOrDecryptedView`
  (posé tant qu'un coffre est parcouru ou qu'une vue déchiffrée est affichée, si le réglage est actif),
  `bothSettingsOffByDefault`.
- [ ] **Step 2 : implémentation** ; le délai du verrouillage automatique de la phase 7 s'applique déjà aux coffres :
  son libellé devient « Verrouiller les disques chiffrés et les coffres après ».
- [ ] **Step 3 : commit** `feat(coffres): fermeture à l'écran éteint et masquage dans les captures`.

### Task 14 : documentation

- [ ] README : section « Chiffrement et coffres (nouveau en 1.7.0) » (formats, un mot de passe par élément, aucune
  récupération sans lui, ouverture sans copie en clair, coffres, clé de secours, biométrie) ; « Lire sur un
  ordinateur » : `age -d fichier.age > fichier`, `age -d dossier.zip.age | bsdtar -xf -`, `7z x fichier.zip`,
  `udisksctl loop-setup -f Coffre.luks` ou `sudo cryptsetup open Coffre.luks coffre && sudo mount /dev/mapper/coffre /mnt` ;
  limites (noms visibles en ZIP AES, taille d'un coffre fixe, pas de coffre sur un dossier distant ni dans un coffre,
  7z et RAR chiffrés refusés, coffre exFAT mal fermé en lecture seule jusqu'à `fsck.exfat`, effacement sur mémoire
  flash) ; « Archives protégées par mot de passe » retiré des limites pour le ZIP ; licences (scrypt, liste EFF) ;
  tableau de la feuille de route ; commandes des juges dans « Tests ».
- [ ] Aide : trois questions, « Chiffrer un fichier ou un dossier », « Créer et ouvrir un coffre », « J'ai oublié le mot
  de passe » ; strings testées.
- [ ] Fiche d'essai `docs/superpowers/research/<AAAA-MM-JJ>-essais-phase14/essais.md` (T16), copiée sur `develop` et sur le
  téléphone (`Download/portail-tests/phase14/`).
- [ ] Commit `docs: chiffrement et coffres`.

### Task 15 : essais instrumentés sur le Pixel

**Files :** Create `android/app/src/androidTest/kotlin/com/mawena/fluluks/VaultInstrumentedTest.kt` ; Modify
`CryptInstrumentedTest.kt`, `Fixtures.kt`

- [ ] **Step 1** : essais sur le téléphone, dans le dossier privé de l'application et sur une image de disque de
  `/data/local/tmp/fluluks/` : chiffrer puis déchiffrer un dossier de 200 fichiers (age et ZIP AES), original
  supprimé après vérification, vue déchiffrée d'une vidéo par le fournisseur, coffre de 128 Mio créé puis ouvert, 50
  fichiers écrits, fermé, rouvert, mot de passe changé, clé de secours ; coffre dans un fichier d'un volume ext4
  d'image, éjection de l'image (coffre fermé avant) ; mesure des durées (scrypt, Argon2, débit dans le coffre).
- [ ] **Step 2** : passage par `am instrument` (jamais `connectedAndroidTest`), journal
  `docs/superpowers/research/<date>-essais-phase14/instrumentes-<horodatage>.txt` ; les fichiers produits sont relus
  sur le PC par `agejuge.py`, `zipaesjuge.py` et `verify_vault_with_kernel.sh` (`adb pull`).
- [ ] **Step 3 : commit** `test(chiffrement): essais instrumentés sur le Pixel`.

### Task 16 *(utilisateur)* : essais sur le Pixel

- [ ] Construire et installer l'APK sur le Pixel **avant** de demander l'essai (`adb install -r`, APK de
  l'utilisateur sauvegardée).
- [ ] Fiche remplie : chiffrer une photo et un dossier du téléphone (age), puis un dossier du disque LUKS vers le disque ;
  ouvrir la photo chiffrée dans la visionneuse et une vidéo chiffrée (avance rapide) ; déchiffrer ; mauvais mot de
  passe ; ZIP AES ouvert sur le PC par 7-Zip ou `7z`, fichier `.age` par `age -d` ; coffre créé sur le téléphone et sur
  le disque USB, parcouru dans Portail et dans Fichiers, vidéo lue dans VLC depuis le coffre ; coffre ouvert sous
  Ubuntu par `cryptsetup` ; verrouillage automatique et éjection du disque avec un coffre ouvert ; mot de passe
  changé, clé de secours ; déverrouillage par le visage ; générateur de phrase de passe.

### Fin de phase

- [ ] Relecture finale de toute la branche.
- [ ] Fusion `feature/chiffrement` → `develop`, puis `release/1.7.0` (pubspec `1.7.0+14`), `main`, étiquette signée
  `v1.7.0`. Pas de push sans l'accord de l'utilisateur.

---

## Décisions du planificateur

Prises en l'absence de l'utilisateur, qui les a validées d'avance ; chacune se change à faible coût si l'essai la
contredit.

1. **age pour les fichiers et dossiers, par défaut** : format ouvert et spécifié, authentifié par morceaux (troncature,
   permutation et modification détectées), dérivation scrypt, lisible par l'outil libre `age` sur tous les systèmes ;
   un dossier chiffré en `.zip.age` cache aussi les noms. Le 7z AES n'est ni lu ni écrit par libarchive ; un format
   propre (Argon2id + AES-GCM) ne serait lisible que par Portail. *Coût si faux : un second écrivain dans `fl_age_*`
   (format propre), l'interface ne change pas.*
2. **ZIP AES-256 en option « pour l'échange »**, avec l'avertissement (noms visibles, PBKDF2-SHA1 à 1 000 tours fixé par
   le format) : c'est ce que 7-Zip et WinRAR ouvrent sous Windows sans rien installer. Le même branchement de libarchive
   sur mbedTLS rend lisibles les ZIP protégés reçus d'ailleurs. *Coût si faux : retirer l'option (une ligne de
   l'écran), la lecture des ZIP protégés reste.*
3. **scrypt de référence de Tarsnap, copié sans modification** plutôt qu'écrit à la main (règle du § 4.1 : pas de
   cryptographie maison) ; log2 N = 18 comme `age` par défaut. *Coût si faux : 17 ou 19 est une constante ; une
   version optimisée de scrypt remplacerait un fichier.*
4. **Dossier chiffré = ZIP dans age** (et pas TAR) : réutilise l'écrivain, le lecteur et les protections de la phase 9,
   compresse, s'ouvre sous Windows après `age -d`. *Coût si faux : un TAR via libarchive, même tube.*
5. **Coffre = fichier LUKS2 sans table + exFAT (ext4 au choix)** : tout le moteur existe déjà (LUKS2, formatage,
   systèmes de fichiers) et Linux l'ouvre avec `cryptsetup` ; exFAT par défaut car il ne reste « sale » qu'entre une
   écriture et sa synchronisation (3 s), alors qu'un ext4 tué en écriture attend le rejeu de son journal sous Linux ;
   ext4 pour qui veut les droits et propriétaires Linux. VeraCrypt n'est pas pris en charge. *Coût si faux : le choix
   par défaut est une constante ; un autre conteneur serait une nouvelle tâche.*
6. **Taille fixe, place écrite à la création** (zéros) : un fichier creux pourrait manquer de place plus tard et
   abîmer le système de fichiers du coffre. Pas d'agrandissement en phase 14 (il viendrait avec le redimensionnement
   de la phase 16). *Coût si faux : création plus rapide par fichier creux, une option.*
7. **Pas de coffre sur un dossier distant ni dans un coffre** : accès aléatoire trop lent et fragile à distance ;
   l'imbrication ajoute un niveau de verrous et de cascade sans usage clair. *Coût si faux : lever le refus après des
   essais de débit.*
8. **Aucun mot de passe mémorisé, aucune option « Mémoriser »** : c'est la règle des disques (§ 2) ; la commodité vient
   de la biométrie facultative, qui scelle un **secret distinct** dans un emplacement LUKS2 à part, jamais le mot de
   passe. *Coût si faux : une case « Mémoriser » scellée par `SecretBox`, comme les serveurs (§ 18).*
9. **Suppression de l'original définitive, sans écrasement** : la corbeille garderait le clair ; un écrasement ne
   garantit rien sur une mémoire flash (répartition de l'usure) et l'use. La mise en garde le dit. *Coût si faux :
   une passe d'écrasement facultative avant la suppression.*
10. **Ouvrir sans copie en clair** par une source déchiffrée en mémoire et le fournisseur de flux de la phase 10 (accès
    aléatoire par morceau de 64 Kio) : les lecteurs existants servent tels quels. Quatre vues au plus.
11. **Propositions retenues** (demande du 2026-10-23 « features pertinentes ») : générateur et force (T7), vérifier et
    changer le mot de passe d'un fichier (T5), mot de passe et clé de secours d'un coffre (T11), biométrie (T12),
    écran éteint et captures (T13), partage d'un fichier chiffré avec la façon de le lire (T8), chiffrement vers un
    dossier distant (T5). **Non retenues** : fichier-clé (keyfile) pour un coffre (gestion d'un fichier sensible de plus
    pour peu de gain face à la clé de secours) ; changer la phrase des **disques** LUKS (le code de T3 le permettrait,
    à demander pour une phase suivante) ; agrandir un coffre ; armure ASCII de age.
12. **Liste EFF en anglais** pour les phrases de passe (7 776 mots, licence claire, ≈ 12,9 bits par mot) faute d'une
    liste française de licence aussi nette. *Coût si faux : remplacer un fichier de données.*
13. **Biométrie par l'API d'Android**, sans `androidx.biometric` (la `MainActivity` de Flutter n'est pas une
    `FragmentActivity`, et l'API 28+ suffit sur le Pixel) ; désactivée par défaut, invalidée par l'ajout d'une
    biométrie. *Coût si faux : `androidx.biometric` 1.1.0 (Apache-2.0) et une `FlutterFragmentActivity`.*
14. **Réglages de confidentialité désactivés par défaut** (l'utilisateur fait des captures, la musique d'un coffre doit
    continuer écran éteint), comme le masquage du terminal en phase 13.
