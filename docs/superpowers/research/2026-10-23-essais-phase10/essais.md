# Essais de la phase 10 (lecteurs intégrés et dossiers distants) sur le Pixel 4 XL

APK de débogage de la branche `feature/lecteurs-distants`, installée avant l'essai. Le numéro affiché reste 1.2.0
jusqu'à la v1.3.0, et le débogage la rend plus lente qu'une version finale. Cochez chaque ligne faite et notez ce qui
ne va pas sous la ligne.

**Réglage** : Réglages → **Ouvrir dans Portail** doit être activé (c'est le cas par défaut). Désactivé, les fichiers
s'ouvrent dans une autre application comme avant.

**Disques** : pour tout ce qui écrit sur un disque USB, utilisez une **clé de test** ou un disque dont vous avez
une sauvegarde, jamais le vrai disque sans sauvegarde.

## 1. Photos

- [x] Toucher une photo du téléphone : la visionneuse s'ouvre en plein écran. Glisser à gauche et à droite passe
      aux autres photos du dossier ; pincer et toucher deux fois zooment.
- [x] Une photo HEIC (prise par un iPhone, ou convertie sous Ubuntu) s'affiche.
- [x] Une photo d'un disque chiffré ouvert s'affiche ; après l'éjection du disque, rien n'en reste dans `Download`
      ni dans la galerie.

## 2. Musique

- [x] Sur un disque chiffré, toucher un morceau d'un dossier qui en contient plusieurs : la lecture démarre, et la
      barre réduite apparaît en bas, avec le titre et lecture/pause.
- [x] Toucher la barre : l'écran **Lecture en cours** montre la position, la durée et le curseur. Essayer
      **Précédent**, **Suivant**, **Reculer de 10 s**, **Avancer de 10 s** et le curseur.
- [x] Éteindre l'écran : la musique continue. La notification permet lecture/pause et suivant, y compris sur
      l'écran verrouillé.
- [x] Débrancher les écouteurs (ou couper le Bluetooth) : la lecture se met en pause.
- [x] Pendant la lecture, **éjecter** le disque : la musique s'arrête, « Lecture arrêtée : stockage fermé »
      s'affiche et l'éjection se termine normalement.
- [x] Recommencer en **arrachant** le disque pendant la lecture : même message, et l'application ne se bloque pas.

## 3. Vidéo

Préparer sur le PC, dans un même dossier : `film.mp4` (ou `.mkv`) et `film.fr.srt` avec des accents. Si possible,
enregistrez le `.srt` en Windows-1252 (« ANSI » dans le Bloc-notes de Windows).

- [x] Toucher la vidéo : le lecteur s'ouvre en plein écran et en paysage, et suit la rotation du téléphone.
- [x] Les sous-titres s'affichent avec les accents justes. Le bouton des sous-titres permet de les couper.
- [x] **Reculer de 10 s** et **Avancer de 10 s**, puis le curseur, fonctionnent. L'écran ne s'éteint pas pendant
      la lecture.
- [x] Retour : on revient à l'explorateur. Noter si une deuxième entrée « Portail » apparaît dans les
      applications récentes.

## 4. Texte

- [x] Sur une clé ext4 de test, ouvrir un `.txt` (ou un `.md`) : l'éditeur l'affiche. Le modifier, puis
      **Enregistrer**.
- [x] Brancher la clé sous Ubuntu : le texte modifié est bien là, les accents sont justes et aucun fichier
      « .portail-partiel » ne reste.
- [x] Un fichier aux fins de ligne Windows (fait sous Windows) garde ses fins de ligne après une modification
      (`file essai.txt` sous Ubuntu : « with CRLF line terminators »).

## 5. Serveur FTP

Sur le PC (même réseau Wi-Fi que le téléphone) : `python3 -m pip install --user pyftpdlib`, puis dans un dossier
d'essai `python3 -m pyftpdlib -w -p 2121 -u essai -P essai`. Adresse du PC : `hostname -I`.

- [x] Accueil → **Dossiers distants** → **Ajouter un serveur** : choisir **FTP**. L'avertissement « circulent en
      clair » s'affiche. Saisir l'adresse du PC, le port 2121, l'utilisateur `essai` et le mot de passe `essai`,
      **sans** cocher « Mémoriser », puis **Enregistrer**.
- [x] Toucher le serveur : le mot de passe est demandé, puis le dossier s'ouvre. Les noms accentués sont justes.
- [x] Copier une photo du téléphone vers le serveur, puis un fichier du serveur vers le téléphone. Les deux
      s'ouvrent et ont la même taille qu'à l'origine.
- [x] Ouvrir une vidéo du serveur et l'avancer au curseur : la lecture reprend plus loin.
- [x] Extraire une archive du serveur vers le téléphone, et compresser un dossier du serveur sur le serveur.
- [x] Supprimer un fichier du serveur : la boîte dit « Pas de corbeille sur un dossier distant ». Le fichier
      disparaît, et aucun dossier `.Portail-corbeille` n'est créé sur le PC.
- [x] **Déconnecter** (bouton de la tuile), puis fermer et rouvrir l'application : le mot de passe est redemandé.

## 6. Serveur SFTP

Sur le PC : `sudo apt install openssh-server` si ce n'est pas déjà fait.

- [x] **Ajouter un serveur** en **SFTP**, avec l'adresse du PC, le port 22 et votre utilisateur Ubuntu. Toucher le
      serveur : l'empreinte de la clé du serveur est montrée. La comparer à
      `ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub`, puis **Faire confiance**.
- [x] Connexion par mot de passe : le dossier de départ s'ouvre. Parcourir, copier dans les deux sens, et ouvrir une
      vidéo et l'avancer.
- [x] Clé privée : sous Ubuntu, `ssh-keygen -t ed25519 -f ~/portail_essai` (avec une phrase), puis ajouter
      `~/portail_essai.pub` à `~/.ssh/authorized_keys`. Copier `portail_essai` dans `Download` du téléphone.
      Dans la fiche du serveur, **Importer une clé**, puis choisir le fichier : le type et l'empreinte s'affichent.
      Supprimer ensuite le fichier de `Download`.
- [x] Se connecter : la phrase de la clé est demandée (dans la boîte « Mot de passe »), puis le dossier s'ouvre.
- [x] Clé du serveur changée : dans la fiche, **Oublier l'empreinte** redemande la confiance à la connexion
      suivante. *Facultatif* : après avoir régénéré les clés du serveur (`sudo ssh-keygen -A` après avoir déplacé les
      anciennes), la connexion est refusée avec « La clé du serveur a changé » tant que l'empreinte n'est pas
      oubliée.
- [x] Un lien symbolique sur le serveur s'affiche, mais n'est pas suivi.

## 7. Version finale (construction release)

À faire sur le PC, la construction release n'étant pas permise à l'agent :
`flutter build apk --release`, puis installer l'APK et refaire une connexion SFTP **par clé Ed25519**. Une erreur
« ClassNotFound » ou une connexion impossible signale une règle R8 manquante pour Bouncy Castle.

- [x] Construction release réussie.
- [x] Connexion SFTP par clé Ed25519 avec l'APK release.
