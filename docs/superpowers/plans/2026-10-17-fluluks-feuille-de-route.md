# FluLuks — Feuille de route

## Suivi

Légende : `[x]` terminé · `[ ]` à faire. Une case est cochée dès que l'étape est terminée et vérifiée,
dans le même commit que le travail.

### Conception
- [x] Document de conception validé ([spec](../specs/2026-10-17-fluluks-design.md))
- [x] Dépôt git et branches git flow (`main`, `develop`)

### Phase 1 — Moteur natif en lecture, sur PC Linux ([plan](2026-10-17-phase1-moteur-natif-lecture.md))
- [x] Plan de la phase 1 écrit
- [x] T1 — Squelette natif et dépendances
- [x] T2 — Images de test (fixtures) + vérification par le noyau Linux *(vérifié par le noyau le 2026-10-17 : 5 images « NOYAU OK »)*
- [x] T3 — Périphériques bloc (fichier, tranche)
- [x] T4 — Tables de partitions GPT / MBR
- [x] T5 — Détection du contenu des partitions
- [x] T6 — Briques cryptographiques
- [x] T7 — LUKS1
- [x] T8 — LUKS2
- [x] T9 — ext4 en lecture + API fichiers unifiée
- [x] T10 — Outil `fluluks-cli` + lecture du vrai disque sur PC *(vrai disque lu le 2026-10-17 : `/dev/sdb1` LUKS2 aes-xts-plain64 déverrouillé, racine ext4 listée ; déverrouillage + montage + listage en 5,1 s sur le vrai disque (build Release, PC), mesure sur téléphone en phase 2)*
- [x] Fusion `feature/moteur-natif-lecture` → `develop`

### Phase 2 — Android en lecture : jalon « mon disque dans VLC » ([plan](2026-10-17-phase2-android-lecture.md))
- [x] Plan de la phase 2 écrit
- [x] *(utilisateur)* Libérer au moins 10 Gio sur `/` (7,5 Gio libres le 2026-10-17 ; ≈ 60 Gio libres ensuite)
- [x] T1 — Environnement Android (SDK 36, NDK 28.2, licences acceptées par l'utilisateur) *(flutter doctor affiche « license status unknown » : incompatibilité entre Flutter 3.41 et le nouvel Android CLI ; la compilation est vérifiée en T5)*
- [x] T2 — Moteur natif portable Android (`fl_memzero`, `extern "C"`) + tests natifs sur le téléphone *(Pixel 4 XL : tous les tests natifs passent ; téléphone : déverrouillage paramètres exacts 4,3 s)*
- [x] T3 — Périphérique bloc USB (Bulk-Only Transport + SCSI)
- [x] T4 — Cache de blocs avec lecture anticipée
- [x] T5 — Projet Flutter et intégration native Gradle
- [x] T6 — Couche JNI à poignées et moteur Kotlin
- [x] T7 — Détection USB, gestionnaire de volumes, service au premier plan
- [x] T8 — DocumentsProvider en lecture seule
- [x] T9 — Interface Flutter (Disques, Déverrouillage) et pont Pigeon
- [x] T10 — Essais sur le Pixel : clé sacrifiée, puis vrai disque en lecture *(2026-10-18, build release
      sur le Pixel 4 XL : essais faits **directement sur le vrai disque** en lecture seule, car la clé
      sacrifiée, préparée avec `prepare_test_key.sh` et aux options ext4 identiques à celles du vrai disque,
      est en USB-A. Tous les points de la grille sont réussis : partition LUKS2 détectée et NTFS affichée
      « Non pris en charge (NTFS) », déverrouillage, racine dans Fichiers, vidéo dans VLC avec avance à 50 % et
      90 %, éjection, mauvaise phrase de passe. Débit mesuré par `FluLuksPerf` : **≈ 65 Mo/s** sur la vidéo
      VLC (46,6 Mo en 717 ms de lecture) et ≈ 31 Mo/s sur un fichier ouvert depuis Fichiers, pour une cible
      ≥ 20 Mo/s. Aucun plantage ni erreur dans les journaux. Le disque est alimenté par le téléphone, sans
      hub. Durée de déverrouillage non chronométrée dans l'application ; 4,3 s mesurés en T2 avec les
      paramètres exacts.)*
- [x] Fusion `feature/android-lecture` → `develop`
- [x] Version `v0.1.0` (branche `release/0.1.0` → `main`)
- Reports de la phase 1 (revue finale) et où ils sont traités :
  - `explicit_bzero` → `fl_memzero`, gardes `extern "C"`, garde `make_fixtures.sh`, cmake 3.20 du README → T2
  - Mesure du déverrouillage sur le téléphone → T2 (tests natifs) et T10 (application)
  - Mémoire disponible avant Argon2 → T7 ; rappel de progression / annulation → phase 6
  - LUKS2 dont seul l'en-tête secondaire est valide → T6 (`scan`, `primary_damaged`)
  - Disques à secteurs logiques de 4096 → T3 (`test_usb_4k`) ; LUKS1 sur disque 4Kn → phase 6
  - JNI en `byte[]`, phrase de passe en octets, `abiFilters`, natif en -O2 → T5/T6
  - Liens symboliques : listés mais non ouverts par le provider → T8
  - Noms de 255 octets (lwext4), extents non écrits, `MAX_DIR_STEPS`, tests manquants (GPT hostile,
    surrogates, boucle EBR, sha512, corruptions LUKS1, label LUKS2) → phase 3 (avant toute écriture)
- Reports de la phase 2 (revues des tâches et revue finale), à reprendre dans les plans suivants :
  - **Avant l'écriture (phase 3)** : fenêtre de démontage non forcé qui rend `FL_EINVAL` au lieu de
    `FL_EBUSY` (`tab_put` doit refuser un enfant si le parent est en fermeture) ; tests manquants :
    `EBUSY` après attente, `closeDisk` pendant `mount`, `close_children` en attente ; `fj_list`
    accumule tout le dossier en natif avant le plafond JSON de 16 Mio.
  - **USB** : `EPROTO` toujours traduit en `FL_ENODEV` ; `clear_halt` sauté si le reset BOT échoue ;
    tests manquants (tag faux, statut > 2, CSW court, timeout transitoire récupéré) ; endpoint 0
    accepté ; CD virtuel en LUN 0 et `GET MAX LUN` → phase 6 ; 04/03 affiché comme « erreur
    d'entrée/sortie » (code d'erreur dédié à créer).
  - **Cache** : allocation ≤ 320 Kio et double copie, balayage linéaire des emplacements, retours
    `pthread_*_init` ignorés, lecture anticipée réduite sur une requête en partie déjà en cache.
  - **Android** : l'exécuteur unique fait attendre l'effacement des clés pendant un Argon2 (≈ 4 s) ;
    marquer le disque détaché immédiatement ; messages français codés en dur dans Kotlin ; repli
    API 34+ du service au premier plan (Ruling P8) ; fenêtre de quelques ms entre `stopSelf` et
    `onDestroy` ; erreur de `runSafely` non publiée si le disque est inconnu ; course de redémarrage
    non testée automatiquement ; « Aucune partition reconnue » sans test JVM ; pas d'unité « To ».
  - **Sécurité, risque résiduel** : copies de la phrase de passe hors de notre contrôle (`String` du
    champ de saisie, clavier, tampons d'encodage de Pigeon en Dart et en Kotlin).
  - **Outils** : `kotlinOptions` déprécié ; `disable-abi-filtering` est une propriété interne de
    Flutter (à revérifier à chaque mise à jour) ; Pigeon bloqué en 27.3.0 par Flutter 3.41.4
    (Ruling P10) ; `$OUT` en lien symbolique dans `run_native_tests.sh` ; shellcheck absent du PC.
  - **Essais** : refaire la grille avec une clé USB-C sacrifiée (dont le débranchement en cours de
    lecture) et chronométrer le déverrouillage dans l'application.

### Phase 3 — ext4 en écriture ([plan](2026-10-18-phase3-ext4-ecriture.md))
- [x] Essai préalable de l'écriture lwext4 face à `e2fsck`
      ([rapport](../research/2026-10-18-essai-lwext4-ecriture/RAPPORT.md))
- [x] Plan de la phase 3 écrit
- [x] T1 — Dettes de lecture dans lwext4 (noms de 255 octets, trous de dossier, entrées hostiles)
- [x] T2 — Tests manquants des phases 1 et 2, `FL_EBUSY` en fermeture, écriture LUKS2
- [x] T3 — Banc de non-corruption (images d'écriture, `e2fsck`, `debugfs`, pannes)
- [x] T4 — Écriture ext4 (API complète, propriétaire hérité, horodatages)
- [x] T5 — Journal sûr (sommes v3, séquence, barrières) et tests de coupure
- [x] T6 — Règles de lecture seule du § 4.2 et montage sûr
- [x] T7 — crc32c rapide *(chemin arm64 à passer sur le Pixel : `run_native_tests.sh`)*
- [x] T8 — Batterie de non-corruption complète *(vérification par le noyau : `sudo tools/fixtures/verify_rw_with_kernel.sh build/native/batterie`, à lancer par l’utilisateur)*
- [x] T9 — Écriture USB (WRITE, SYNCHRONIZE CACHE, protection en écriture) *(compilation `build/android-arm64` faite ; tests sur le Pixel à passer)*
- [x] T10 — JNI et VolumeManager en écriture (synchronisation, éjection, débranchement) *(tests JVM et natifs PC verts, `test_batterie_lent` : 35 min en Debug + ASan ; à passer sur le Pixel, injoignable depuis le conteneur : `connectedDebugAndroidTest` puis `tools/android/check_image.sh <serial>`)*
- [x] T11 — DocumentsProvider en écriture *(tests JVM verts, dont `WriteOpsTest` sur un système de fichiers en mémoire ; `ProviderWriteTest` compilé, à passer sur le Pixel puis `tools/android/check_image.sh <serial> provider-plein.img`)*
- [x] T12 — Interface Flutter (lecture seule, éjection, avertissements)
- Relecture et corrections de T4 à T12 (faites hors de la boucle de relecture, dans une autre session) :
  - [x] Vérification sur PC du travail récupéré *(2026-10-18 : suite native rapide 20/20 sous ASan/UBSan,
        0 erreur ; `flutter analyze` propre ; `flutter test` 21/21 ; tests JVM verts)*
  - [x] Relectures rétroactives de T4 à T12 *(T12 approuvée ; T4, T5, T6, T7, T8, T9, T10-11 à corriger :
        5 défauts critiques, tous sans effet sur le vrai disque tant qu'aucune écriture n'y est faite)*
  - [x] Correctif T9 : avertissement USB « support changé » ou « reset » pendant l'écriture *(critique)* + T7 : crc32c dans les tests du téléphone *(UNIT ATTENTION après l'ouverture : 0x28 → disque mort, autre → `FL_EIO` sans réessai et flush en échec ; `test_crc32c` et son débit ajoutés à `run_native_tests.sh` ; PC 20/20, arm64 compilé)*
  - [x] Correctif T10-T11 : éjection en échec, provider *(en parallèle, branche `fix/p3-ejection`, fusionnée ; JVM 46/46, natifs PC 20/20)*
  - [x] Correctif journal (T5, T6) : journaux à somme v1 refusés en écriture, blocs échappés, octets non
        initialisés, juge et barrières *(critique)* *(correctif lwext4 0007 : somme d'étiquette sur le
        bloc échappé — sans lui, `e2fsck` rejetait le bloc au rejeu —, blocs du journal à zéro ; v1 et
        ro_compat refusés ; juge aligné sur jbd2 ; ordre des barrières vérifié ; PC 20/20)*
  - [ ] Correctif écriture et politique (T4, T6) : erreur d'E/S pendant une écriture, `rmdir` sur disque
        hostile, sommes fausses avalées, lecture seule collante jusqu'au démontage *(critique)*
  - [x] Correctif T8 : batterie (cadence 200 pas, profondeur 40, copies supprimées en échec) *(jalon
        tous les 200 pas partout, variante longue comprise ; chaque batterie creuse jusqu'à la profondeur
        40, jugée aussitôt et affirmée ; en échec l'image est gardée en `<nom>.echec.img`, et les
        sorties d'un passage précédent sont supprimées au départ ; `test_manifeste` ajouté (manifest.py
        contre tm_write_manifest, via `debugfs rdump`, sans root) ; PC 20/20, `test_batterie_lent`
        vert en 83 min)*
  - [ ] Passage sur le Pixel : tests natifs (`run_native_tests.sh`), `connectedDebugAndroidTest`,
        `check_image.sh`
  - [ ] Relecture finale de toute la branche
- [ ] *(utilisateur)* Clé sacrifiée branchable en USB-C (clé USB-C ou adaptateur OTG USB-A → USB-C)
- [ ] *(utilisateur)* Sauvegarde complète du vrai disque avant toute écriture dessus
- [ ] T13 — Essais sur le Pixel : clé sacrifiée, puis vrai disque
- [ ] Fusion `feature/ext4-ecriture` → `develop`
- [ ] Version `v0.2.0`

### Phase 4 — FAT16 / FAT32 / exFAT (lecture puis écriture)
- [ ] Plan de la phase 4 écrit
- [ ] Version `v0.3.0`

### Phase 5 — NTFS (lecture puis écriture)
- [ ] Plan de la phase 5 écrit
- [ ] Version `v0.4.0`

### Phase 6 — Finitions et robustesse
- [ ] Plan de la phase 6 écrit
- [ ] *(explorateur intégré complet, copies avec progression, réglages, verrouillage automatique,
      débranchement inattendu, performances)*
- [ ] Version `v1.0.0`

---

## Principes

- Une phase = un plan détaillé, écrit juste avant de la commencer, pour profiter de ce que la phase
  précédente a appris.
- Une phase = une branche `feature/…` créée depuis `develop`, avec un commit par tâche, puis fusionnée
  dans `develop` avec `--no-ff`. Chaque phase livrée à l'utilisateur passe par
  `release/x.y.z` → `main` + étiquette `vx.y.z`.
- Écart par rapport au § 6 de la spec *(décision du planificateur)* : la réparation du SDK Android et la
  création du projet Flutter passent en phase 2, au moment où elles servent. La phase 1 n'a besoin que
  du PC Linux.
