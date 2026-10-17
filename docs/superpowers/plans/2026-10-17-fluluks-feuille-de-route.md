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
- [x] T10 — Outil `fluluks-cli` + lecture du vrai disque sur PC *(vrai disque lu le 2026-10-17 : `/dev/sdb1` LUKS2 aes-xts-plain64 déverrouillé, racine ext4 listée ; déverrouillage 4,4 s en build Release sur PC, mesure sur téléphone en phase 2)*
- [x] Fusion `feature/moteur-natif-lecture` → `develop`

### Phase 2 — Android en lecture : jalon « mon disque dans VLC »
- [ ] Plan de la phase 2 écrit
- [ ] *(tâches détaillées à l'écriture du plan : SDK/NDK et projet Flutter, JNI, USB Bulk-Only/SCSI,
      cache de blocs, service au premier plan, DocumentsProvider en lecture, écrans Disques et
      Déverrouillage, essai sur clé sacrifiée puis sur le vrai disque en lecture)*
- [ ] Version `v0.1.0` (branche `release/0.1.0` → `main`)
- Reports de la phase 1 à intégrer au plan de la phase 2 (revue finale) :
  - [ ] **En premier :** remplacer `explicit_bzero` par un `fl_memzero` portable (Bionic ne l'a qu'à partir
        de l'API 28, la spec vise minSdk 26) et corriger la contrainte globale du plan.
  - [ ] Gardes `extern "C"` dans les en-têtes publics (JNI).
  - [ ] `make_fixtures.sh` : la garde anti-`rm -rf` bloque la régénération après un échec (marqueur posé
        au début) ; aligner la version cmake du README (3.20).
  - [ ] Mesurer le déverrouillage sur le téléphone en build natif Release (4,4 s sur PC ; 53 s en Debug+ASan).
  - [ ] Vérifier la mémoire disponible avant un Argon2 gourmand ; rappel de progression / annulation.
  - [ ] `fl_probe` : signaler LUKS2 quand seul l'en-tête secondaire est valide (aujourd'hui seul le CLI
        contourne).
  - [ ] Disques à secteurs logiques de 4096 (certains boîtiers USB) : message clair ou lecture-modification-écriture.
  - [ ] Noms de 255 octets non ouvrables (bug lwext4) ; extents non écrits et lectures à cheval trou/données
        à tester ; `MAX_DIR_STEPS` sous verrou global trop grand ; liens symboliques (politique du provider).
  - [ ] JNI : noms de fichiers en `byte[]` (pas `NewStringUTF`, emoji), phrase de passe en `byte[]` ;
        `abiFilters` arm64-v8a (+ x86_64 émulateur) ; natif compilé en -O2.
  - [ ] Tests manquants : GPT hostile, 4K, surrogates, boucle EBR, sha512, corruptions LUKS1, label LUKS2.

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
