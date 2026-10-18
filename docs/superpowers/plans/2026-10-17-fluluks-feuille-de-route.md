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

### Phase 3 — ext4 en écriture
- [ ] Plan de la phase 3 écrit
- [ ] *(tâches détaillées à l'écriture du plan : écriture lwext4 + journal, règles de sécurité § 4.2,
      propriétaire/droits, écriture via DocumentsProvider, batterie `e2fsck`, tests de coupure)*
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
