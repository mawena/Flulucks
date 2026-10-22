# Essais de la phase 7 sur le Pixel 4 XL

APK de la branche `feature/robustesse` installée le 2026-10-22 (le numéro affiché reste 0.5.0 jusqu'à la
v1.0.0). Cochez chaque ligne faite ; notez ce qui ne va pas sous la ligne.

## 1. Vrai disque, ext4 en écriture

- [X] **Sauvegarde du vrai disque faite.** Ne rien faire de la suite de cette section sans elle.
- [X] Déverrouiller le disque **en écriture** (interrupteur « lecture seule » désactivé dans les Réglages).
- [X] Copier un fichier d'environ 1 Gio du téléphone vers le disque. Noter le débit affiché à la fin
      (« Terminé : 1 fichier(s), … Mo/s ») : ………
- [X] Le recopier du disque vers le téléphone. Débit : ………
- [X] Renommer un fichier sur le disque, puis le supprimer.
- [X] Éjecter, attendre la fin de « Écriture des données en attente… », débrancher.
- [X] Sous Ubuntu : déverrouiller le disque, puis `sudo e2fsck -fn /dev/mapper/<nom>` : **aucune erreur**.
- [X] Sous Ubuntu : le fichier copié s'ouvre et a la bonne taille ; il vous appartient (pas besoin de sudo).

## 2. Verrouillage automatique

- [X] Réglages → Verrouillage automatique → **5 min**.
- [X] Ouvrir un disque chiffré, ne plus y toucher : au bout de 5 à 6 min, notification « Disque verrouillé
      après 5 min sans activité », et le disque redemande la phrase de passe.
- [X] Rouvrir le disque, lancer une vidéo dans VLC et la laisser jouer plus de 6 min : **pas** de verrouillage.
- [X] Remettre le réglage sur « Jamais ».

## 3. Débranchement pendant une copie — **clé USB sacrifiée seulement**

- [X] Brancher une clé sans importance (FAT32 ou exFAT), l'ouvrir dans Portail.
- [X] Lancer la copie d'un gros fichier vers la clé, puis **débrancher la clé** pendant la copie.
- [X] Notification : conseil adapté (vérifier la clé avec chkdsk ou fsck, ouverte en lecture seule d'ici là).
- [X] L'accueil affiche « Stockage fermé : opération interrompue ».
- [X] En rebranchant la clé : pas de fichier incomplet sous son vrai nom (au plus un `.nom.portail-partiel`).

## 4. Divers

- [X] Explorateur : Téléphone → Android → data affiche « Dossier protégé par Android ».
- [X] Copier une photo d'un disque vers Téléphone → DCIM → Camera : la copie réussit et la photo apparaît
      dans Google Photos.
- [X] Le nouveau logo orange s'affiche sur l'écran d'accueil et dans la notification « Disque ouvert ».
