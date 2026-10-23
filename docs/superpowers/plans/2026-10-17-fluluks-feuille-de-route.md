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
- [x] T8 — Batterie de non-corruption complète *(vérifiée par le noyau le 2026-10-18 : `sudo tools/fixtures/verify_rw_with_kernel.sh build/native/batterie`, 11 images « NOYAU OK » — `1k`, `luks2`, `reel`, `secteurs-4k`, `seed`, `uninit` produites avec le correctif 0010, les 5 `longue-*` avec le code d'avant ; variante longue repassée avec le code publié en 0.2.0 : verte en 107 min, images `longue-*` à refaire juger par le noyau)*
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
  - [x] Correctif écriture et politique (T4, T6) : erreur d'E/S pendant une écriture, `rmdir` sur disque
        hostile, sommes fausses avalées, lecture seule collante jusqu'au démontage *(critique)* *(volume
        gelé dès la lecture seule en cours de session ; correctifs lwext4 0008 — racine d'extents
        vérifiée, erreurs de libération remontées : une troncature validait un inode sans ses blocs — et
        0009 — annulation sans relecture : plantage ; `rmdir` borné ; PC 20/20)*
  - [x] Correctif T8 : batterie (cadence 200 pas, profondeur 40, copies supprimées en échec) *(jalon
        tous les 200 pas partout, variante longue comprise ; chaque batterie creuse jusqu'à la profondeur
        40, jugée aussitôt et affirmée ; en échec l'image est gardée en `<nom>.echec.img`, et les
        sorties d'un passage précédent sont supprimées au départ ; `test_manifeste` ajouté (manifest.py
        contre tm_write_manifest, via `debugfs rdump`, sans root) ; PC 20/20, `test_batterie_lent`
        vert en 83 min)*
  - [x] Passage sur le Pixel : tests natifs (`run_native_tests.sh`), `connectedDebugAndroidTest`,
        `check_image.sh` *(2026-10-18, Pixel 4 XL : tests natifs arm64 tous verts, crc32c matériel
        « armv8-crc » à 9 218 Mo/s contre 300 Mo/s octet par octet, déverrouillage aux paramètres exacts
        4,45 s ; tests instrumentés 50/50 ; `e2fsck -fn` propre sur `engine-ecriture.img`,
        `provider-plein.img` et `provider-ecriture.img`. Corrigé en route : éjection refusée sans fin
        quand une autre application garde un fichier ouvert → démontage forcé après un `sync` réussi
        (Ruling P12) ; Gradle désinstallait l'application après les tests, et ses images avec →
        `-Pandroid.injected.androidTest.leaveApksInstalledAfterRun=true`, et `check_image.sh` exige
        le magic ext4)*
  - [x] Correctif : vrai disque refusé en écriture (« sommes de contrôle invalides ») au branchement sur
        le Pixel *(2026-10-18 : la politique d'écriture lisait comme vides les extents pleins de
        32 768 blocs (`ee_len` 0x8000) du journal de 1 Gio du disque de 421 Gio → journal « introuvable »,
        compté comme somme fausse ; toutes les vraies sommes étaient bonnes, rien n'a été écrit. Image
        de test `ext4-rw-journal-gros.img` (journal de même forme) ; `fluluks-cli diagnostic` (lecture
        seule, détail des contrôles et parcours de l'arborescence) et détail dans logcat
        (`FluLuksPolicy`) en cas de refus ; vrai disque monté en écriture sur le Pixel après correction)*
  - [x] Correctif lwext4 0010 : fichiers creux *(trouvé en vérifiant le correctif précédent :
        supprimer un gros fichier creux validait une transaction de journal par tranche de 16 Mio — un
        million pour 16 Tio, des heures sur un disque USB —, et écrire loin au-delà de la fin d'un fichier
        non vide visait un bloc hors du disque (volume gelé en « corruption »). Test
        `test_politique_suppression_gros_creux` : 10 vidages de cache au lieu d'environ 130 000)*
  - [x] Relecture finale de toute la branche *(depuis T12 ; corrigés : fichier à blocs indirects vidé
        puis volume gelé en « w » — refus à l'ouverture —, dernier bloc logique, `fsync` réussi après gel,
        éjection après gel, accès hors verrou, nœud d'index vide (0008) ; découpe d'extents à 2 niveaux
        sur disque plein laissée en limite connue ; PC 20/20, JVM 48/48)*
- [x] *(utilisateur)* Clé sacrifiée branchable en USB-C (clé USB-C ou adaptateur OTG USB-A → USB-C)
- [x] *(utilisateur)* Sauvegarde complète du vrai disque avant toute écriture dessus
- [x] T13 — Essais sur le Pixel : clé sacrifiée, puis vrai disque *(faits le 2026-10-22 avec les essais de la phase 7 ; non faits pour la 0.2.0 : pas de clé
      sacrifiée ni de sauvegarde du vrai disque pour l'instant ; version publiée à la demande de
      l'utilisateur, écriture par défaut, avec la mention « écriture non essayée sur un vrai disque » dans le
      README. Le vrai disque s'ouvre bien en écriture sur le Pixel depuis le correctif du journal de 1 Gio,
      aucune écriture n'y a été faite.)*
- [x] Fusion `feature/ext4-ecriture` → `develop`
- [x] Version `v0.2.0` *(2026-10-18, branche `release/0.2.0` → `main` ; écriture non encore essayée sur un vrai
      disque, voir T13)*
- Correctif `hotfix/0.2.1` — écriture lente :
  - [x] Écriture différée par fichier (`wbuf.c`) *(chaque `pwrite` devenait une transaction de journal
        complète, ≈ 3,6 vidages de cache de ~20 ms sur un disque dur ; les écritures contiguës d'un fichier
        sont regroupées par 4 Mio : 16 Mio écrits par blocs de 4 Kio passent à 0,6 vidage par Mio. Un seul
        tampon par volume, vidé avant toute autre opération ; erreur d'un vidage collante sur son fichier,
        rendue par l'écriture, la troncature, la fermeture et `sync` suivantes ; écriture directe près du
        disque plein, pour que `FL_ENOSPC` revienne à l'écriture fautive. Test `test_ext4_tampon`, jugé par
        `e2fsck` et `debugfs` : 9 cas, dont l'erreur différée, la panne et le disque plein ; PC 21/21 sous
        ASan/UBSan, arm64 compilé)*
  - [x] Version `v0.2.1` *(2026-10-18, branche `hotfix/0.2.1` → `main` et `develop`)*
  - [x] Essai sur le Pixel *(2026-10-19, par l'utilisateur : copie nettement plus rapide qu'en 0.2.0)*

### Phase 4 — FAT16 / FAT32 / exFAT (lecture puis écriture) ([plan](2026-10-19-phase4-fat-exfat.md))
- [x] Plan de la phase 4 écrit *(2026-10-19 ; FatFs R0.16, FAT12 inclus)*
- [x] T1 — Images FAT/exFAT et juges indépendants (`fsck.fat`, `fsck.exfat`, `fatjuge.py`) *(mtools ne sait pas
      écrire un émoji : il n'est que dans l'arbre exFAT ; test_fat_banc 6/6)*
- [x] T2 — FatFs dans le moteur : montage, statfs, lecture seule si sale *(la JNI et fluluks-cli acceptent
      FAT dès T2, au lieu de T9 ; arm64 compilé)*
- [x] T3 — Lecture : stat, readdir, pread, noeuds partagés, recherche rapide *(test_fat 19/19 sur PC et sur le
      Pixel ; sans la table de clusters, 12 590 lectures au lieu de 5 000 au plus pour 500 pread)*
- [x] T4 — Écriture des fichiers : zéros des trous, limite 4 Gio (`FL_EFBIG`), drapeau sale *(FSInfo et
      PercentInUse justes grâce à un f_getfree avant la première écriture ; test_fat_ecriture 10/10, plus de 4 Gio
      en exFAT en 20 s ; suite rapide 24/24)*
- [x] T5 — Dossiers et noms : renommage de casse, sous lui-même, noms refusés *(« \\ » refusé sur tout le
      chemin : FatFs le prend pour un séparateur ; test_fat_ecriture 17/17)*
- [x] T6 — Débit : regroupement des écritures contiguës *(32 Mio par pwrite de 128 Kio, écritures dans la zone
      de données : exFAT 8 196 → 13, FAT32 → 130 ; après une panne d'écriture le volume reste lisible)*
- [x] T7 — Coupures et pannes *(coupure à chaque écriture : 54 points en FAT32, 15 en exFAT ; jamais un volume
      incohérent marqué propre — sans le drapeau sale, le test échoue —, fichiers sûrs intacts après fsck)*
- [x] T8 — Batterie aléatoire, images hostiles, vérification par le noyau *(300 images abîmées + 9 cas
      fabriqués : 2 défauts trouvés — f_lseek(CREATE_LINKMAP) sans fin sur une chaîne en boucle, corrigé par
      `patches/fatfs/0001` ; décalage indéfini sur un exFAT hostile, borné par la colle ; batterie 3 × 400 et
      3 × 5 000 opérations ; script noyau `verify_fat_with_kernel.sh` à lancer par l'utilisateur)*
- [x] T9 — JNI, Kotlin, Flutter *(EFBIG, « Nom non autorisé sur ce disque », renommage de casse ; JVM 55/55,
      Flutter 21/21, APK construit ; tests natifs sur le Pixel, test_fat_hostile compris : tous OK)*
- [x] *(utilisateur)* T10 — Essais sur le Pixel avec une clé FAT32 puis exFAT sauvegardée *(2026-10-19, par
      l'utilisateur : validés)*
- [x] T11 — Documentation *(README : NTFS, lecture seule, limites, GPL effective ; PATCHES.md : section libntfs-3g)*
- [x] Fusion `feature/fat-exfat` → `develop` *(après relecture finale : démontage qui rendait toujours FL_OK,
      corrigé ; suite rapide 27/27)*
- [x] Version `v0.3.0` *(2026-10-19, branche `release/0.3.0` → `main` et `develop`)*

### Phase 5 — NTFS (lecture puis écriture) ([plan](2026-10-19-phase5-ntfs.md))
- [x] Plan de la phase 5 écrit *(2026-10-19 ; libntfs-3g 2022.10.3, juge indépendant `ntfsjuge.py`)*
- [x] T1 — libntfs-3g dans l'arbre, images NTFS et juge indépendant *(correctif 0001 : accès non alignés de
      compress.c vus par UBSan ; test_ntfs_banc 5/5, suite rapide 28/28)*
- [x] T2 — Colle NTFS : montage, statfs, politique de lecture seule *(veille, $LogFile, drapeaux de $Volume :
      lecture seule sans aucune écriture ; montage puis démontage en écriture : image identique)*
- [x] T3 — Lecture : stat, readdir, pread *(correctif 0004 : ntfs_readdir repris au milieu d'un octet du
      bitmap d'index lisait un bloc absent ; test_ntfs 20/20 sur PC et sur le Pixel)*
- [x] T4 — Écriture des fichiers et drapeau sale *(correctif 0003 : décalage d'un négatif dans les runlists ;
      inodes non écrits abandonnés après une panne ; test_ntfs_ecriture 9/9)*
- [x] T5 — Dossiers et noms *(noms Windows refusés, recherche sans la casse, renommage de casse par un nom
      temporaire, renommage entre deux liens durs sans effet ; test_ntfs_ecriture 19/19)*
- [x] T6 — Débit : cache d'écriture différée *(32 Mio par pwrite de 128 Kio : 1 852 écritures et 1 042
      lectures → 19 écritures et 0 lecture ; petites lectures gardées dans la même table)*
- [x] T7 — Coupures et pannes *(88 points de coupure, 57 laissent un volume incohérent, tous marqués sales ;
      correctif 0005 : montage en lecture seule malgré un $MFTMirr différent ; drapeau écrit $MFT d'abord en le
      posant et en dernier en l'effaçant ; mutation : sans drapeau, le test échoue)*
- [x] T8 — Batterie aléatoire, images hostiles, vérification par le noyau *(300 images abîmées + 8 cas
      fabriqués, lus puis écrits sans plantage ; batterie 512 et 4096 octets par secteur, graines 1 à 6 × 1 000 pas,
      variante lente 5 000 pas ; correctif 0006 : fuite de runlist dans ntfs_delete ; vérification par le noyau :
      `sudo tools/fixtures/verify_ntfs_with_kernel.sh`, faite par l'utilisateur avec T10)*
- [x] T9 — JNI, Kotlin, Flutter *(test_handles_ntfs et test_handles_luks2_ntfs jugés par ntfsjuge.py et
      ntfsfix -n ; message FL_ENOTSUP « Fichier non modifiable ici… » ; APK de version, tests natifs verts sur le Pixel)*
- [x] *(utilisateur)* T10 — Essais sur le Pixel avec un disque NTFS sauvegardé *(2026-10-20 : vrai disque NTFS
      lu et écrit depuis le Pixel 4 XL ; vérification sous Windows et relecture par le noyau
      (`verify_ntfs_with_kernel.sh`) encore à faire)*
- [x] T11 — Documentation *(README : NTFS, lecture seule, limites, GPL effective ; PATCHES.md : section libntfs-3g)*
- [x] Fusion `feature/ntfs` → `develop` *(après relecture finale : cache de lecture périmé, démarrage rapide
      de Windows mal nommé, compression laissée active, volume plus grand que le disque ouvert en écriture,
      cas hostile « liste d'attributs » ; corrigés, suite rapide 34/34, Pixel 15/15)*
- [x] Version `v0.4.0`

### Phase 6 — Portail : nom, logo et gestionnaire de fichiers ([plan](2026-10-20-phase6-portail.md))
- [x] Plan de la phase 6 écrit *(spec § 11 : nom Portail, logo dossier-bouclier, gestionnaire de fichiers
      complet avec accès à tous les fichiers)*
- [x] T1 — Nom « Portail » et logo *(icône adaptative avec silhouette monochrome, icône de notification ; rendu vérifié sur le Pixel)*
- [x] T2 — Sources de fichiers (téléphone, carte SD, disques) et accès au téléphone *(verrou d'écriture partagé avec le provider ; provider 36/36 sur le Pixel)*
- [x] T3 — Lister, créer, renommer, supprimer, ouvrir avec, partager *(FileProvider pour le téléphone ; tests instrumentés à passer sur le Pixel)*
- [x] T4 — Copier et déplacer entre stockages *(noms assainis vers les disques et la carte SD ; essai 8 Mio à passer sur le Pixel)*
- [x] T5 — Recherche *(sans casse ni accents, 500 résultats et profondeur 64 au plus)*
- [x] T6 — Vignettes des photos et vidéos *(cache de 32 Mio en mémoire seulement, vidé à l'éjection ; essai réel à passer sur le Pixel)*
- [x] T7 — Tri, affichage et favoris *(favoris par clé stable, résolus vers le volume ouvert)*
- [x] T8 — Écran d'accueil et Réglages *(capture sur le Pixel lors des essais de T11)*
- [x] T9 — Explorateur *(liste et grille, vignettes, tri, sélection, presse-papiers, barre de copie ; captures lors des essais de T11)*
- [x] T10 — Écran de recherche *(400 ms après la dernière frappe, recherche précédente annulée)*
- [x] *(utilisateur)* T11 — Essais sur le Pixel *(2026-10-20 : tout est correct selon l'utilisateur)*
- [x] T12 — Récupération de `feature/portail-save` *(un test repris, branche supprimée)*
- [x] T13 — Affichage des dossiers en liste ou en grille, choisi dans les Réglages
- [x] T14 — Documentation *(README : gestionnaire de fichiers, accès à tous les fichiers, vignettes en mémoire, limites, phases 7 et 8)*
- [x] Fusion `feature/portail` → `develop` *(après relecture finale : `Android/data` affiché vide, chemins absolus
      dans les messages, source supprimée pendant un téléchargement, vignettes qui bloquaient le listage,
      carte SD retirée non suivie ; corrigés, JVM 114/114, Flutter 60/60, Pixel 59/59)*
- [x] Version `v0.5.0`

### Phase 7 — Robustesse et performances ([plan](2026-10-22-phase7-robustesse.md))
- [x] Plan de la phase 7 écrit *(spec § 13)*
- [x] T1 — Logo orange, plus représentatif : arche de portail, dossier et cadenas *(demande du 2026-10-22 ; AppIdentityTest 3/3 sur le Pixel)*
- [x] T2 — Système de fichiers et chiffrement connus pour chaque volume *(clé « fs » du statfs natif)*
- [x] T3 — Verrouillage automatique des disques chiffrés (Kotlin : contrôle toutes les 30 s, jamais pendant une copie)
- [x] T4 — Réglage du verrouillage dans les Réglages (Jamais par défaut, 5, 15, 30 min, 1 h)
- [x] T5 — Débranchement pendant une écriture : conseil selon le système de fichiers (ext4, FAT/exFAT, NTFS)
- [x] T6 — Copies : résultat visible sur l'accueil, montré une fois, avec le débit
- [x] T7 — Copies : nom temporaire pendant l'écriture (« .nom.portail-partiel »), noms longs rendus uniques
- [x] T9 — Performances du § 5 mesurées sur le Pixel, sur image *(listage de 1 000 fichiers : 25 ms disque, 276 ms
      téléphone ; lecture par le provider : 241 Mo/s ; copies 64 Mio : 118 Mo/s vers le disque, 96 Mo/s retour)*
- [x] Retour d'essai du 2026-10-22 : « Coller ici » disparaît une fois la copie lancée (comme après « Couper »)
- [x] *(utilisateur)* T10 — Essais sur le Pixel : ext4 en écriture sur le vrai disque *(tâche 13 de la phase 3 ;
      `e2fsck -fn` propre)*, verrouillage automatique, débranchement d'une clé pendant une copie, divers
- [x] T8 — Points mineurs de la relecture de la phase 6 *(nom assaini de 255 octets, barre de copie qui rejoue
      le dernier résultat, « Ouvrir avec » et « Partager » sur le fil principal, vignette gardée après
      l'éjection, favoris d'un disque chiffré en clair, clé USB vue comme carte SD, fichier partiel sous son
      vrai nom, détails de l'explorateur et de la recherche)*
- [x] T11 — Documentation : README (robustesse, mesures, limite levée)
- [x] Version `v1.0.0`

### Phase 8 — Formatage des disques USB ([plan](2026-10-22-phase8-formatage.md))
- [x] Plan de la phase 8 écrit *(demande du 2026-10-22 ; spec § 14)*
- [x] T1 — Table de partitions à une partition (MBR jusqu'à 2 Tio, GPT au-delà) *(jugée par sfdisk et sgdisk)*
- [x] T2 — FAT32 et exFAT *(f_mkfs de FatFs ; fsck.fat et fsck.exfat propres, étiquette dans le secteur de démarrage)*
- [x] T3 — ext4 avec journal *(ext4_mkfs de lwext4 corrigé par 0011 ; e2fsck propre avant et après écriture)*
- [x] T4 — NTFS *(mkntfs de ntfs-3g appelé comme bibliothèque, correctif 0007 ; ntfsfix et ntfsjuge propres avant et après écriture)*
- [x] T5 — LUKS2 et formatage complet d'un disque *(en-tête LUKS2 jugé par cryptsetup ; 8 combinaisons jugées par leur outil et blkid ; lwext4 corrigé par 0012 ; script noyau `verify_format_with_kernel.sh`)*
- [x] T6 — Formatage depuis Kotlin *(fj_format, VolumeManager.format hors de l'exécuteur, Pigeon formatDisk ; JVM 148, Flutter 72 ; après la relecture finale : JVM 152, Flutter 80, FormatInstrumentedTest et suite instrumentée 65/65 sur le Pixel)*
- [x] T7 — Écran de formatage *(menu « Formater… » de la carte du disque, exFAT par défaut, confirmation par le nom du disque ; Flutter 79)*
- [x] *(utilisateur)* T8 — Essais : montage par le noyau sur le PC, clé USB sacrifiée sur le Pixel
- [x] T9 — Documentation
- [x] Fusion `feature/formatage` → `develop`
- [x] Version `v1.1.0`

### Phase 9 — Compression, corbeille et outils du gestionnaire ([plan](2026-10-22-phase9-compression.md))
- [x] Plan de la phase 9 écrit *(demande du 2026-10-22 ; spec § 15)*
- [x] T1 — libarchive 3.8.10 et liblzma 5.8.4 dans le moteur
- [x] T2 — Lecture d'archives : ZIP, 7z, RAR4, RAR5, TAR, TAR.GZ, TAR.XZ, GZ, XZ
- [x] T3 — Écriture ZIP (noms UTF-8, ZIP64)
- [x] T4 — Poignées d'archive par JNI, sur un descripteur ou un fichier du moteur
- [x] T5 — Compresser et extraire d'un stockage à l'autre, avec progression et annulation
- [x] T6 — Compresser et extraire depuis l'explorateur
- [x] T7 — Corbeille sur le téléphone et sur les disques (récupérable 30 jours)
- [x] T8 — Analyse de l'espace (dossiers et plus gros fichiers)
- [x] T9 — Propriétés d'un fichier, avec empreinte SHA-256
- [x] T10 — Renommage par lot
- [x] T11 — Filtres rapides sur l'accueil (photos, vidéos, musique, documents, archives, récents)
- [x] Essais instrumentés sur le Pixel : archives 3/3, filtres 1/1, copies 5/5 *(2026-10-23)*
- [x] *(utilisateur)* T12 — Essais sur le Pixel ([fiche](../research/2026-10-22-essais-phase9/essais.md))
      *(2026-10-23 : toutes les lignes de la fiche réussies)*
- [x] T13 — Documentation
- [x] Relecture finale de toute la branche *(aucun point critique ; 5 importants corrigés : place réelle relue à
      l'extraction, premier passage borné, .gz/.xz seuls, purge de la corbeille, photos de la même seconde ;
      11 mineurs reportés)*
- [x] Fusion `feature/compression` → `develop`
- [x] Version `v1.2.0`

### Phase 10 — Lecteurs intégrés et dossiers distants ([plan](2026-10-23-phase10-lecteurs-distants.md))
- [x] Plan de la phase 10 écrit
      *(demande du 2026-10-20, J.1 ; déplacée en phase 9 puis en phase 10 le 2026-10-22 ; spec § 18)*
- [x] T1 — Lecture d'images et de textes, enregistrement sûr des textes
- [x] T2 — « Ouvrir dans Portail » et éditeur de texte (UTF-8, BOM et fins de ligne gardés)
- [x] T3 — Visionneuse d'images (glisser d'une photo à l'autre, zoom)
- [x] T4 — Lecture à accès aléatoire et fournisseur de flux
- [x] T5 — Serveurs enregistrés et sources distantes (secrets chiffrés par le Keystore)
- [x] T6 — Dossiers distants dans le gestionnaire
- [x] T7 — Dossiers distants par FTP
- [x] T8 — Dossiers distants par SFTP (empreinte du serveur, clé privée)
- [x] T9 — Lecteur vidéo intégré (avance rapide, sous-titres `.srt` à côté de la vidéo)
- [x] T10 — Lecteur de musique (lecture en arrière-plan, file d'attente du dossier)
- [x] T11 — Essais instrumentés sur le Pixel
- [x] *(utilisateur)* T12 — Essais sur le Pixel
- [x] T13 — Documentation
- [x] Relecture finale de toute la branche
- [x] Fusion `feature/lecteurs-distants` → `develop`
- [x] Version `v1.3.0`

### Phase 11 — Interface complète et professionnelle ([plan](2026-10-23-phase11-interface.md))
- [x] Plan de la phase 11 écrit *(demande du 2026-10-23 ; spec § 19 ; choix de conception à valider par l'utilisateur)*
- [x] T1 — Système de design : couleurs du logo, thèmes clair et sombre, choix du thème
- [x] T2 — Composants communs (états vides, erreurs, chargements, confirmations, feuilles d'actions)
- [x] T3 — Textes français (pluriels, unités, dates relatives)
- [x] T4 — Accueil revu et logo
- [x] T5 — Déverrouillage, formatage et saisies sensibles
- [x] T6 — Collection d'éléments et explorateur revu (fil d'Ariane, tout sélectionner, annuler la mise à la corbeille)
- [x] T7 — Liste ou grille dans les raccourcis et la recherche *(demande de l'utilisateur du 2026-10-23)*
- [x] T8 — Recherche dans tous les stockages
- [x] T9 — Réglages regroupés, « À propos », licences, aide et bienvenue
- [x] T10 — Barres du bas et écrans de la phase 10
- [x] T11 — Accessibilité, grands écrans et tests dorés
- [ ] T12 — Captures et essais instrumentés sur le Pixel 4 XL
- [ ] *(utilisateur)* T13 — Essais sur le Pixel
- [ ] T14 — Documentation
- [ ] Relecture finale de toute la branche
- [ ] Fusion `feature/interface` → `develop`
- [ ] Version `v1.4.0`

### Phase 12 — Lecteur vidéo : orientation, pistes et gestes ([plan](2026-10-23-phase12-lecteur-video.md))
- [x] Plan de la phase 12 écrit *(demande du 2026-10-23 ; spec § 20)*
- [x] T1 — Orientation : portrait par défaut, rotation selon le réglage d'Android, boutons « Tourner » et cadenas
- [x] T2 — Choix des pistes audio et des sous-titres, désactivables
- [x] T3 — Glisser pour avancer ou reculer, avec l'aperçu de la position visée
- [x] T4 — Toucher deux fois pour reculer ou avancer de 10 s, cumulable
- [x] T5 — Documentation
- [x] T6 — Essais instrumentés sur le Pixel
- [x] *(utilisateur)* T7 — Essais sur le Pixel ([fiche](../research/2026-10-23-essais-phase12/essais.md)) *(2026-10-23 : toute la fiche réussie selon l'utilisateur)*
- [ ] Relecture finale de toute la branche
- [ ] Fusion `feature/lecteur-video` → `develop`
- [ ] Version `v1.5.0`

### Phase 13 — Vérification et réparation des disques
- [ ] Plan de la phase 13 écrit *(demande du 2026-10-22 ; spec § 16 ; phase 11, puis phase 12, jusqu'au 2026-10-23)*
- [ ] Vérification en lecture seule avec rapport, pour chaque système de fichiers
- [ ] Réparation ext4 par e2fsck (e2fsprogs, sur l'accès USB du moteur)
- [ ] Réparation NTFS par ntfsfix (ntfs-3g)
- [ ] Réparation FAT32 et exFAT par fsck.fat et fsck.exfat
- [ ] LUKS : vérification des deux en-têtes, sauvegarde et restauration de l'en-tête
- [ ] Version `v1.6.0`

### Phase 14 — Gestion des partitions, comme GParted
- [ ] Plan de la phase 14 écrit *(demande du 2026-10-22 ; spec § 17 ; phase 12, puis phase 13, jusqu'au 2026-10-23)*
- [ ] Vue du disque : partitions et espace libre à l'échelle, système de fichiers, étiquette, taille, place occupée
- [ ] Taille choisie au formatage ; créer une partition dans un espace libre, en supprimer une, formater une seule partition
- [ ] Changer l'étiquette d'une partition ; nouvelle table MBR ou GPT
- [ ] Redimensionner (agrandir, réduire) : ext4 par resize2fs, NTFS par ntfsresize, système de fichiers dans LUKS2 ; FAT32 et exFAT à étudier
- [ ] Déplacer une partition, si le plan sait le rendre sûr
- [ ] Opérations préparées, résumées, puis appliquées après confirmation
- [ ] Version `v1.7.0`

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
