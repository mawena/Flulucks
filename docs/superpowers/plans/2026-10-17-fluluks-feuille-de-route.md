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
- [ ] *(utilisateur)* Libérer au moins 10 Gio sur `/` (7,5 Gio libres le 2026-10-17)
- [x] T1 — Environnement Android (SDK 36, NDK 28.2, licences acceptées par l'utilisateur) *(flutter doctor affiche « license status unknown » : incompatibilité entre Flutter 3.41 et le nouvel Android CLI ; la compilation est vérifiée en T5)*
- [x] T2 — Moteur natif portable Android (`fl_memzero`, `extern "C"`) + tests natifs sur le téléphone *(Pixel 4 XL : tous les tests natifs passent ; téléphone : déverrouillage paramètres exacts 4,3 s)*
- [x] T3 — Périphérique bloc USB (Bulk-Only Transport + SCSI)
- [x] T4 — Cache de blocs avec lecture anticipée
- [x] T5 — Projet Flutter et intégration native Gradle
- [x] T6 — Couche JNI à poignées et moteur Kotlin
- [ ] T7 — Détection USB, gestionnaire de volumes, service au premier plan
- [ ] T8 — DocumentsProvider en lecture seule
- [ ] T9 — Interface Flutter (Disques, Déverrouillage) et pont Pigeon
- [ ] T10 — Essais sur le Pixel : clé sacrifiée, puis vrai disque en lecture
- [ ] Fusion `feature/android-lecture` → `develop`
- [ ] Version `v0.1.0` (branche `release/0.1.0` → `main`)
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
