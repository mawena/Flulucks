# Essais de la phase 8 (formatage) sur le PC et le Pixel 4 XL

APK de la branche `feature/formatage` (le numéro affiché reste 1.0.0 jusqu'à la v1.1.0). Cochez chaque
ligne faite ; notez ce qui ne va pas sous la ligne.

**Formater efface tout le disque.** Sur le téléphone, n'utilisez qu'une **clé USB sacrifiée**, jamais le
vrai disque. Avant chaque formatage, vérifiez le nom et la taille affichés dans la confirmation.

## 1. PC : images montées par le noyau

- [x] Depuis la racine du dépôt, lancer : `sudo tools/fixtures/essais_phase8_pc.sh`
      Le script formate 10 disques image (sans root), les fait monter et écrire par le noyau, repasse
      l'outil de vérification de chaque système de fichiers, puis supprime les images. Il ne touche à aucun
      vrai disque. Compte rendu : `docs/superpowers/research/2026-10-22-essais-phase8/pc-<date>.txt`.
- [x] Attendu à la fin : `10 image(s), 0 échec(s)` puis `## fin : tout est vert`.

## 2. Pixel : clé USB sacrifiée

Pour chaque format ci-dessous : accueil → carte de la clé → menu **⋮** → **Formater…**, choisir les options,
taper le nom de la clé pour confirmer, attendre la fin (« Disque formaté »), puis ouvrir la clé, y copier
un fichier, éjecter et la brancher sur Ubuntu.

- [ ] Le menu **Formater…** est grisé, avec « Éjectez d'abord les volumes de ce disque », tant qu'un volume
      de la clé est ouvert.
- [ ] **exFAT** (choix par défaut), étiquette `Essai` : la clé s'ouvre dans Portail, la copie réussit ;
      sous Ubuntu, la clé s'ouvre et le fichier est lisible.
- [ ] **FAT32**, étiquette `ESSAI` : même chose.
- [ ] **ext4 chiffré** (LUKS2), étiquette `Données`, une phrase de passe : le formatage prend quelques
      secondes de plus (dérivation de la clé) ; la clé se déverrouille dans Portail avec la phrase ; sous
      Ubuntu, elle demande la phrase, s'ouvre, et le fichier vous appartient (pas besoin de sudo).
- [ ] **NTFS**, étiquette `Essai` : même chose sous Ubuntu, et sous Windows si possible.
- [ ] Phrase de passe vide, ou confirmation différente : refusées dans l'écran, rien n'est formaté.
- [ ] Débrancher la clé **pendant** un formatage : message « Disque débranché pendant le formatage :
      recommencez le formatage », pas de plantage ; en la rebranchant, elle peut être reformatée.
