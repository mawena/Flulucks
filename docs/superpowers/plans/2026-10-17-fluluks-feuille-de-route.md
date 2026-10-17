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
- [x] T2 — Images de test (fixtures) + vérification par le noyau Linux *(vérification noyau sudo : à lancer par l'utilisateur en fin de phase)*
- [x] T3 — Périphériques bloc (fichier, tranche)
- [x] T4 — Tables de partitions GPT / MBR
- [x] T5 — Détection du contenu des partitions
- [x] T6 — Briques cryptographiques
- [x] T7 — LUKS1
- [x] T8 — LUKS2
- [ ] T9 — ext4 en lecture + API fichiers unifiée
- [ ] T10 — Outil `fluluks-cli` + lecture du vrai disque sur PC
- [ ] Fusion `feature/moteur-natif-lecture` → `develop`

### Phase 2 — Android en lecture : jalon « mon disque dans VLC »
- [ ] Plan de la phase 2 écrit
- [ ] *(tâches détaillées à l'écriture du plan : SDK/NDK et projet Flutter, JNI, USB Bulk-Only/SCSI,
      cache de blocs, service au premier plan, DocumentsProvider en lecture, écrans Disques et
      Déverrouillage, essai sur clé sacrifiée puis sur le vrai disque en lecture)*
- [ ] Version `v0.1.0` (branche `release/0.1.0` → `main`)

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
