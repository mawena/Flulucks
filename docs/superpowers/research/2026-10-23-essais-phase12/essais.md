# Essais de la phase 12 (lecteur vidéo) sur le Pixel 4 XL

APK de débogage de la branche `feature/lecteur-video`, installée avant l'essai. Le numéro affiché reste celui de la
version précédente jusqu'à la v1.5.0, et le débogage rend l'application plus lente qu'une version finale. Cochez
chaque ligne faite et notez ce qui ne va pas sous la ligne.

**Préparation** sur le PC, dans un même dossier du téléphone ou d'une clé : une vidéo `film.mkv` (ou `.mp4`) qui a
**deux pistes audio** si possible (un film en VF et VO, par exemple), et `film.fr.srt` à côté. À défaut, le script
`tools/fixtures/make_video_phase12.sh <dossier>` fabrique `video-phase12.mkv` (deux pistes audio, français et
anglais, et un sous-titre anglais intégré) ; renommez-le `film.mkv` et ajoutez un `film.fr.srt`.

## 1. Orientation

- [x] **Rotation automatique activée** (volet des réglages rapides d'Android) : téléphone tenu droit, toucher la
      vidéo. Le lecteur s'ouvre **en portrait**, la vidéo centrée avec des bandes noires.
- [x] Tourner le téléphone en paysage : le lecteur passe en paysage ; revenir en portrait : il revient.
- [x] **Rotation automatique coupée** : fermer le lecteur, couper la rotation automatique, rouvrir la vidéo en tenant
      le téléphone en paysage. Le lecteur reste **en portrait** et ne tourne pas (Android peut proposer son petit
      bouton de rotation dans la barre du bas : c'est le comportement normal d'Android).
- [x] Toucher l'image pour montrer les commandes : la barre du haut montre le nom du fichier et trois boutons
      (Pistes, Tourner, cadenas). Toucher **Tourner** : le lecteur passe en paysage, « Paysage verrouillé »
      s'affiche et le cadenas se ferme. Toucher encore **Tourner** : retour en portrait, « Portrait verrouillé ».
- [x] Rotation automatique **activée**, toucher le **cadenas** ouvert : « Orientation verrouillée » ; tourner le
      téléphone ne change plus rien. Toucher le cadenas fermé : « Rotation automatique », et le lecteur suit de
      nouveau le téléphone.
- [x] Après **Tourner** en paysage, tourner physiquement le téléphone en paysage de l'autre côté : l'image se
      retourne (les deux côtés du paysage restent permis) mais ne repasse pas en portrait.

## 2. Pistes

- [x] Toucher **Pistes** : un menu propose **Audio** et **Sous-titres**.
- [x] **Audio** : les pistes sont nommées en français (par exemple « Français · Stéréo », « Anglais · 5.1 ») ; celle
      qui joue est cochée. En choisir une autre : le son change aussitôt, sans retour au début.
- [x] **Sous-titres** : « Désactivés », puis « Français · film.fr.srt » et les sous-titres intégrés à la vidéo. Choisir
      **Désactivés** : les sous-titres disparaissent ; revenir à `film.fr.srt` : ils reviennent, accents justes.
- [x] Une vidéo sans son (ou sans sous-titre) : le menu le dit (« Aucune piste audio ») ou ne propose que
      « Désactivés », sans plantage.

## 3. Glisser pour avancer ou reculer

- [x] Glisser le doigt vers la droite sur l'image : au centre s'affichent la position visée (« 12:34 / 1:30:00 ») et
      l'écart (« +0:25 »), qui suivent le doigt. Au lever du doigt, la lecture saute à cette position.
- [x] Glisser vers la gauche : l'écart devient négatif (« −0:25 ») et la lecture recule.
- [x] Glisser jusqu'au bord : la position ne dépasse ni le début ni la fin.
- [x] Commandes affichées : faire glisser le **curseur de la barre de progression** fonctionne comme avant, sans que
      la bulle du glissement apparaisse ; toucher Lecture/Pause, ou les boutons de recul et d'avance, fonctionne comme
      avant.
- [x] Un glissement vertical ne fait rien.
- [x] Noter si la vitesse convient (une largeur d'écran ≈ 1 min en portrait, ≈ 2 min en paysage).

## 4. Toucher deux fois

- [x] Commandes cachées, toucher deux fois vite la **moitié droite** : « +10 s » s'affiche à droite et la lecture
      avance de 10 s ; les commandes restent cachées.
- [x] Toucher deux fois la **moitié gauche** : « −10 s » à gauche, la lecture recule.
- [x] Toucher deux fois à droite puis continuer à toucher vite : « +20 s », « +30 s »… ; chaque toucher avance de
      10 s de plus. S'arrêter une seconde, puis toucher une fois : les commandes apparaissent (la série est finie).
- [x] Commandes affichées, toucher deux fois une zone vide : la lecture saute et les commandes restent affichées.
- [x] Un toucher simple montre ou cache toujours les commandes.
- [x] Noter si le délai entre deux touchers (700 ms) convient.

## 5. Divers

- [x] Vidéo d'un **dossier distant** (FTP ou SFTP) : glisser et toucher deux fois fonctionnent ; un glissement ne
      provoque qu'un seul chargement, au lever du doigt.
- [x] Vidéo d'un **disque chiffré** : pistes et gestes fonctionnent ; éjecter le disque pendant la lecture ferme le
      lecteur avec « stockage fermé », comme avant.
- [x] Mettre en pause, éteindre puis rallumer l'écran : le lecteur garde son orientation et son cadenas.
- [x] TalkBack (facultatif) : les boutons s'annoncent « Pistes », « Tourner l'écran », « Verrouiller
      l'orientation » ou « Déverrouiller l'orientation ».
