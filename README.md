# Portail

Portail (anciennement FluLuks) est un **gestionnaire de fichiers** Android qui ouvre aussi les **disques
externes chiffrés avec LUKS** branchés en USB-C, **sans root**, ainsi que les clés et disques en clair
formatés en ext4, FAT, exFAT ou NTFS. Il parcourt le téléphone, la carte SD et les disques USB, copie ou
déplace d'un stockage à l'autre, compresse et extrait les archives, et garde une corbeille. Il montre les photos,
lit la musique et les vidéos et modifie les textes dans ses propres lecteurs, et parcourt des dossiers distants FTP
et SFTP comme les autres stockages. Le contenu des
disques apparaît aussi dans l'application **Fichiers** d'Android : on ouvre une vidéo dans VLC, un PDF ou une
photo dans les applications habituelles.

Elle est née d'un besoin simple : utiliser sur un téléphone non rooté (un Pixel 4 XL) le disque
LUKS2/ext4 qu'on utilise sous Ubuntu. Aucune application existante ne le permettait.

> **État actuel : version 1.4.0 ; gestionnaire de fichiers (interface claire ou sombre et accessible, recherche dans
> tous les stockages, lecteurs intégrés de photos, musique, vidéos et textes, dossiers distants FTP et SFTP, archives
> ZIP, 7z, RAR et TAR, corbeille, analyse de l'espace, renommage par lot), lecture et écriture sur ext4, FAT, exFAT
> et NTFS, formatage des disques USB, en clair ou chiffrés.** L'écriture a été vérifiée sur PC (batteries de
> non-corruption jugées par des outils indépendants et le noyau Linux, coupures simulées), sur le téléphone avec des
> images, puis essayée sur une vraie clé et un vrai disque pour chaque système de fichiers (ext4 compris : `e2fsck`
> propre ensuite sous Ubuntu).
> Une sauvegarde reste recommandée avant d'écrire sur un disque qui compte. Voir les
> [limites](#limites-actuelles).

## Ce que fait la version 1.4.0

### Interface (nouveau en 1.4.0)

- **Thèmes clair et sombre**, aux couleurs terracotta du logo : Réglages → Apparence → **Thème** (Système, par
  défaut, Clair ou Sombre). L'écran de démarrage est terracotta, sans éclair blanc en mode sombre.
- **Accessibilité** : le texte suit la taille choisie dans Android jusqu'à 200 % sans déborder ; les écrans se
  lisent avec **TalkBack**, en français (tuiles de la grille, jauges de place, puces d'état, lecteur de musique) ;
  boutons et éléments touchables de 48 dp au moins ; contraste AA (4,5:1 pour le texte), vérifié par les tests en
  clair comme en sombre. Les animations s'arrêtent quand Android les coupe.
- **Grands écrans et paysage** : l'accueil passe sur deux colonnes dès 840 dp de large (le Pixel 4 XL en
  paysage), la grille ajoute des colonnes selon la largeur, et les formulaires restent dans une largeur lisible.
- **Liste ou grille partout** où cela a du sens : explorateur, raccourcis de l'accueil, recherche et recherche
  globale suivent le même choix (Réglages, ou le bouton de la barre de titre), vignettes comprises. La corbeille et
  l'analyse de l'espace restent en liste, pour l'emplacement d'origine, les dates et les barres de taille.
- **Recherche dans tous les stockages** : la barre en haut de l'accueil fouille le téléphone, les cartes SD et les
  disques ouverts, et groupe les résultats par stockage ; la puce **Partout** y mène depuis la recherche de
  l'explorateur. Les dossiers distants n'y sont pas inclus, pour rester rapide.
- **Explorateur** : fil d'Ariane, menu **⋮** sur chaque élément (en liste), **Tout sélectionner**, et **Annuler**
  juste après une mise à la corbeille. Les états vides, les chargements et les erreurs (avec **Réessayer**) ont la
  même présentation partout.
- **Déverrouillage** : bouton pour afficher ou masquer la phrase de passe, et case **Ouvrir en lecture seule**,
  préremplie par le réglage.
- **Bienvenue** au premier lancement, montrée une seule fois : ce que fait Portail, la confidentialité de la phrase
  de passe, et l'accès aux fichiers, inutile pour les disques USB.
- **Aide** (menu **⋮** de l'accueil, ou Réglages) : huit questions courantes, de l'ouverture d'un disque chiffré au
  formatage.
- **À propos** : version, appareil, et **Licences des bibliothèques** embarquées (lwext4, ntfs-3g, FatFs,
  libarchive, XZ Utils, Argon2, cJSON, Mbed TLS, Media3, Apache Commons Net, JSch, Bouncy Castle).
- **Retours haptiques** : légère vibration à la sélection, au déverrouillage, à la fin d'une copie et à une
  suppression définitive ; Réglages → Confort → **Retours haptiques** pour les couper.
- **Réglages regroupés** en sections : Apparence, Fichiers, Disques, Confort et Portail.

<p align="center">
  <img src="docs/captures/accueil-clair.png" width="240" alt="Accueil en thème clair : recherche, raccourcis, stockages et disque USB ouvert">
  <img src="docs/captures/explorateur-grille-sombre.png" width="240" alt="Explorateur en grille, thème sombre, avec vignettes">
  <img src="docs/captures/deverrouillage-sombre.png" width="240" alt="Dialogue de déverrouillage, thème sombre, avec l'œil et la case Ouvrir en lecture seule">
</p>
<p align="center"><sub>Accueil en clair, explorateur en grille et déverrouillage en sombre, sur le Pixel 4 XL.</sub></p>

### Lecteurs intégrés et dossiers distants (1.3.0)

- **Ouvrir dans Portail** (réglage actif par défaut) : toucher un fichier l'ouvre dans le lecteur de Portail qui lui
  convient ; les autres types, ou tous si le réglage est coupé, s'ouvrent dans une autre application comme avant.
  Rien de ce que lisent les lecteurs n'est écrit sur le stockage du téléphone : un fichier d'un disque chiffré reste
  dans le disque.
- **Photos** (JPEG, PNG, GIF, WebP, BMP, HEIC et HEIF) : plein écran ; on glisse d'une photo du dossier à l'autre,
  on zoome en pinçant ou en touchant deux fois. 64 Mio au plus par image.
- **Musique** (MP3, M4A, AAC, OGG, Opus, FLAC, WAV) : la file d'attente est faite des morceaux du dossier, dans
  l'ordre affiché. La lecture continue écran éteint, avec la notification d'Android, et se met en pause quand on
  débranche les écouteurs. Une barre réduite reste en bas de tous les écrans ; l'écran **Lecture en cours** donne la
  position, le curseur, précédent, suivant, et le recul et l'avance de 10 s. Si le stockage disparaît (éjection,
  disque arraché, serveur déconnecté), la lecture s'arrête : « Lecture arrêtée : stockage fermé ».
- **Vidéo** (MP4, M4V, MKV, WebM, MOV, 3GP, AVI) : plein écran, **en portrait par défaut** ; le lecteur suit la
  rotation du téléphone seulement quand la rotation automatique d'Android est activée. En haut, **Pistes** choisit
  la piste audio et les sous-titres (ou les coupe), **Tourner** passe du portrait au paysage et inversement, et le
  **cadenas** fige l'orientation (le rouvrir rend la rotation automatique). Comme dans VLC, **glisser**
  horizontalement avance ou recule (la position visée s'affiche pendant le geste, la lecture y saute au lever du
  doigt), et **toucher deux fois** la moitié gauche ou droite recule ou avance de 10 s, 10 s de plus à chaque
  toucher rapproché. Les boutons reculent et avancent aussi de 10 s ; l'écran reste allumé pendant la lecture. Les
  sous-titres `film.srt` et `film.<langue>.srt` posés à côté de la vidéo sont proposés (UTF-8 ou Windows-1252 ; le
  premier est actif). *(Orientation, pistes et gestes : 1.5.0.)*
- **Textes** (TXT, Markdown, JSON, XML, CSV, journaux, fichiers de configuration, code…) : éditeur pour les fichiers
  UTF-8 de 2 Mio au plus. Le BOM et les fins de ligne Windows sont gardés. L'enregistrement écrit un fichier
  temporaire qui remplace ensuite l'original, jamais d'original à moitié écrit ; si le fichier a changé depuis son
  ouverture, Portail demande avant d'écraser. **Modifier comme texte** ouvre un fichier d'une autre extension.
- **Dossiers distants** (accueil, section **Dossiers distants**) : un serveur **FTP** ou **SFTP** enregistré (nom,
  adresse, port, utilisateur, dossier de départ) se parcourt comme les autres stockages : ouvrir avec les lecteurs
  (une vidéo distante s'avance sans être téléchargée), partager, copier et déplacer dans les deux sens, renommer,
  créer, supprimer, compresser et extraire.
  - Le mot de passe n'est mémorisé que si « Mémoriser » est coché, chiffré par une clé du Keystore d'Android ;
    sinon il est demandé à chaque connexion et oublié à la déconnexion.
  - **SFTP** : l'empreinte de la clé du serveur est montrée à la première connexion, à comparer à
    `ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub`, puis vérifiée à chaque connexion ; une clé changée est
    refusée (**Oublier l'empreinte** dans la fiche si le serveur a été réinstallé). Connexion par mot de passe ou
    par une **clé privée OpenSSH** importée dans la fiche (Ed25519, ECDSA ou RSA), scellée comme le mot de passe ;
    une clé chiffrée demande sa phrase à la connexion, sauf si elle est mémorisée.
  - **FTP** : mode passif ; UTF-8 si le serveur l'annonce, sinon Latin-1 ; connexion anonyme si l'utilisateur est
    vide.

### Archives, corbeille et outils (1.2.0)

- **Extraire** une archive ZIP, 7z, RAR (RAR4 et RAR5), TAR, TAR.GZ ou TAR.XZ, ou un fichier `.gz` ou `.xz`
  seul : touchez-la, puis **Extraire ici** (dans un nouveau dossier au nom de l'archive ; un `.gz` ou un `.xz`
  seul donne simplement le fichier, à côté de l'archive) ou **Extraire vers…** (ouvrez ensuite le dossier voulu,
  sur n'importe quel stockage, et touchez **Extraire ici** dans la barre).
  Progression et **Annuler**, comme une copie. Les noms des ZIP de Windows sans accents Unicode (page de code
  437) sont lus correctement.
- **Compresser** une sélection en ZIP : menu **⋮** de la sélection, **Compresser**, nom proposé (celui de
  l'élément seul, sinon « Archive »). Les photos, vidéos, musiques et archives déjà compressées sont stockées
  telles quelles, le reste est compressé ; les noms accentués restent lisibles sous Windows et Linux ; au-delà de
  4 Gio, le ZIP passe en ZIP64.
- **Archives hostiles** : rien n'est jamais écrit hors du dossier de destination. Les chemins en `../` ou
  absolus, les liens et les fichiers spéciaux sont ignorés et comptés (« 3 élément(s) de l'archive
  ignoré(s) »). Avant d'écrire, la taille annoncée (au moins 4 Kio par fichier et par dossier) doit tenir dans
  la place libre, moins une réserve de 256 Mio (ou 5 % du stockage) ; pendant l'extraction, la place réelle est
  relue avant chaque fichier et tout s'arrête avant d'entamer la réserve (bombe de décompression, taille
  mensongère, milliers de petits fichiers sur une clé à gros clusters), et le fichier en cours est supprimé.
- **Corbeille** : **Supprimer** met les éléments dans la corbeille du stockage (dossier caché
  `.Portail-corbeille`, au format freedesktop d'Ubuntu), pendant 30 jours ; **Supprimer définitivement**
  reste proposé. La corbeille s'ouvre depuis la carte du stockage, la tuile d'un volume ouvert ou le menu de
  l'explorateur : **Restaurer** (les dossiers d'origine sont recréés), supprimer définitivement, **Vider**.
  Sur un disque chiffré, elle reste dans le volume chiffré.
- **Analyse de l'espace** : menu de l'explorateur, à la racine. Dossiers et fichiers du plus gros au plus
  petit, avec leur pourcentage ; on descend dans les dossiers sans relire le stockage ; les 100 plus gros
  fichiers.
- **Propriétés** (menu **⋮** de la sélection) : taille (totale pour un dossier), date, système de fichiers ;
  droits et propriétaire sur ext4 ; **empreinte SHA-256** calculée à la demande, à comparer à `sha256sum`.
- **Renommage par lot** (**Renommer** sur plusieurs éléments) : numérotation (« Vacances (01).jpg »),
  remplacement d'un texte, ou **date de prise de vue** des photos (« 2024-05-01 12.34.56.jpg »). Aperçu des
  noms ; les photos de la même seconde (rafale) sont numérotées (« 2024-05-01 12.34.56 (2).jpg ») ; le lot est
  refusé en entier si un nom est pris par un autre élément ou invalide.
- **Raccourcis de l'accueil** : Photos, Vidéos, Musique, Documents, Archives et Récents (30 jours), sur le
  téléphone et les cartes SD, d'après l'index d'Android.

### Formatage des disques USB (1.1.0)

- **Formate un disque entier** depuis le menu **⋮** de sa carte (**Formater…**), en **exFAT** (choix par
  défaut : Windows, macOS, Linux, téléviseurs), **FAT32** (ancien matériel), **ext4** (Ubuntu) ou **NTFS**
  (Windows), avec une étiquette.
- **Chiffrement LUKS2** en option, comme `cryptsetup luksFormat` : aes-xts-plain64, clé de 512 bits,
  Argon2id (1 Gio de mémoire, 256 Mio si le téléphone en manque). Le disque se déverrouille ensuite dans
  Portail comme sous Ubuntu, avec la même phrase de passe.
- **Une seule partition** sur tout le disque, alignée sur 1 Mio : table MBR jusqu'à 2 Tio, GPT au-delà.
  Les anciennes signatures (GPT de secours, en-tête LUKS, superblocs) sont effacées, pour qu'aucun outil
  ne voie un reste de l'ancien contenu.
- **Confirmation en tapant le nom du disque** : rien n'est écrit avant. Tout est vérifié d'abord (étiquette,
  phrase de passe, taille du disque) : un refus n'efface rien.
- En ext4, la racine appartient à l'utilisateur 1000, celui d'Ubuntu : les fichiers sont à vous sans sudo.
- Chaque format a été relu par son outil de référence (`fsck.fat`, `fsck.exfat`, `e2fsck`, `ntfsfix`,
  `cryptsetup`) et monté par le noyau Linux, puis essayé sur une clé USB depuis le Pixel.

### Robustesse (1.0.0)

- **Verrouillage automatique** des disques chiffrés après 5, 15, 30 minutes ou 1 heure sans activité
  (Réglages ; « Jamais » par défaut). Une copie en cours ou un fichier lu par une autre application (une
  vidéo dans VLC, par exemple) compte comme une activité. Une notification l'annonce.
- **Disque débranché pendant une écriture** : la notification donne le conseil adapté au système de
  fichiers (ext4 : le brancher sous Ubuntu, qui rejoue le journal ; FAT ou exFAT : le faire vérifier, il
  s'ouvre en lecture seule d'ici là ; NTFS : le brancher sous Windows), et l'accueil affiche « Stockage
  fermé : opération interrompue ».
- **Copies plus sûres** : un fichier en cours de copie porte un nom temporaire (« .nom.portail-partiel »)
  jusqu'à la fin ; une copie interrompue ne laisse jamais un fichier incomplet sous son vrai nom.
- **Résultat des copies** sur l'accueil, avec le débit (« Terminé : 3 fichier(s), 118 Mo/s »).
- **Nouveau logo orange** : une arche de portail qui encadre un dossier, avec un cadenas.

### Gestionnaire de fichiers

- **Un accueil pour tous les stockages** : le téléphone, la carte SD, les disques USB (à déverrouiller,
  ouvrir ou éjecter) et les favoris.
- **Explorateur** : naviguer, ouvrir avec une autre application, partager, créer un dossier, renommer et
  supprimer (à la corbeille), sur le téléphone comme sur un disque.
- **Copier et déplacer entre tous les stockages** (téléphone ↔ carte SD ↔ disque, ou d'un disque à
  l'autre), avec la progression dans une barre et un bouton pour annuler. Les noms interdits sur la
  destination (`:` ou `?` vers FAT, exFAT ou NTFS, par exemple) sont remplacés.
- **Recherche** par nom dans le dossier ouvert et ses sous-dossiers, sans tenir compte des majuscules
  ni des accents ; 500 résultats au plus.
- **Vignettes** des photos et des vidéos, **affichage en liste ou en grille** (choisi dans les Réglages ou
  dans le menu de l'explorateur), **tri** par nom, taille ou date, fichiers cachés affichés ou non.
- **Favoris** : un dossier d'un disque reste en favori d'une session à l'autre, et s'ouvre dès que le
  disque est ouvert.
- **Réglages** : lecture seule des disques, affichage en liste ou en grille, fichiers cachés, accès aux
  fichiers du téléphone.

### Disques USB

- **Détecte** un disque USB de stockage de masse branché en USB-C, et demande la permission d'accès.
- **Lit les tables de partitions** GPT et MBR (partitions étendues comprises), ainsi qu'un disque sans
  table.
- **Déverrouille LUKS1 et LUKS2** avec une phrase de passe :
  - dérivations de clé PBKDF2, Argon2i et Argon2id ;
  - chiffrements aes-xts-plain64, aes-cbc-essiv:sha256 et aes-cbc-plain64.
- **Lit l'ext4** : journal, extents, dossiers indexés et `metadata_csum`.
- **Écrit sur l'ext4** (nouveau en 0.2.0) : créer, modifier, renommer, déplacer, copier et supprimer
  des fichiers et des dossiers, depuis l'application Fichiers ou n'importe quelle application qui
  enregistre sur le disque.
  - Le journal est utilisé comme par le noyau : après un débranchement en pleine écriture, Ubuntu
    rejoue le journal au montage suivant.
  - Les fichiers créés appartiennent au propriétaire du dossier parent (droits 0644, dossiers 0755)
    et sont datés à l'heure du téléphone : ils apparaissent normalement sous Ubuntu.
  - Les données sont synchronisées à la fermeture de chaque fichier et 3 s après la dernière écriture.
  - Les écritures successives d'un fichier sont regroupées par 4 Mio (0.2.1) : une copie n'attend plus
    le disque à chaque bloc.
- **Lit et écrit FAT12, FAT16, FAT32 et exFAT** (nouveau en 0.3.0), en clair ou dans LUKS : clés USB,
  cartes SD, disques formatés sous Windows ou macOS.
  - FAT n'a ni propriétaire ni droits ; les noms y sont insensibles à la casse, comme sous Windows.
  - Un fichier ne peut pas dépasser 4 Gio en FAT12/16/32 : la copie s'arrête avec « Fichier trop gros
    pour ce disque ». exFAT n'a pas cette limite.
  - Pendant les écritures, le volume est marqué « en cours d'utilisation » ; une clé arrachée sans
    « Éjecter » est reconnue à l'ouverture suivante.
- **Lit et écrit NTFS** (nouveau en 0.4.0), en clair ou dans LUKS : disques formatés sous Windows, avec
  libntfs-3g, la bibliothèque du pilote ntfs-3g de Linux.
  - Les fichiers système de NTFS (`$MFT`, `$Bitmap`…) sont cachés ; les noms y sont insensibles à la casse,
    comme sous Windows, et les noms interdits par Windows (`aux`, `nul`, `con.txt`…) sont refusés.
  - Le volume est marqué « à vérifier » pendant les écritures et ne l'est plus après « Éjecter » : un disque
    arraché en pleine écriture est vérifié par Windows au branchement suivant.
  - Un disque NTFS n'est ouvert en écriture que s'il a été bien arrêté par Windows (voir les
    [limites](#limites-actuelles)).
- **Formate un disque** (nouveau en 1.1.0) : voir [plus haut](#formatage-des-disques-usb-nouveau-en-110).
- **Lecture seule quand il le faut**, avec la raison affichée : interrupteur « Ouvrir les disques en
  lecture seule », disque protégé en écriture, journal à rejouer, système de fichiers marqué en erreur,
  somme de contrôle fausse, option ext4 non prise en charge en écriture, disque FAT ou exFAT mal
  éjecté, disque NTFS mal fermé ou laissé par Windows en veille prolongée, ou erreur d'écriture en cours
  de session.
- **Expose chaque volume ouvert** dans l'application Fichiers, avec accès aléatoire : l'avance rapide
  marche dans VLC.
- **Éjecte proprement** depuis l'application ou depuis la notification : écriture des données en
  attente, démontage, `SYNCHRONIZE CACHE`, effacement de la clé, puis libération de l'USB. Un fichier
  encore ouvert par une autre application est fermé de force après la synchronisation.
- **Interface en français.**

### Accès à tous les fichiers

Pour parcourir le téléphone et la carte SD, Portail demande la permission Android **« Accès à tous les
fichiers »** (`MANAGE_EXTERNAL_STORAGE`). C'est la seule qui permet à un gestionnaire de fichiers de voir
tous les dossiers, et non les seuls médias. Le Play Store la réserve à certaines applications ; Portail
s'installant à la main, elle est acceptable ici.

Sans elle, l'accueil affiche une carte « Accès aux fichiers du téléphone » : les disques USB restent
utilisables, mais le téléphone et la carte SD ne sont pas parcourus. Pour la retirer : *Paramètres
Android → Applications → Portail → Accès à tous les fichiers*.

Mesures sur un Pixel 4 XL avec le vrai disque (LUKS2, Argon2id à 1 Gio de mémoire) :
- déverrouillage en 4,3 s environ ;
- lecture d'une vidéo dans VLC à environ 65 Mo/s.

Mesures sur le même téléphone, sur une image (test `PerfInstrumentedTest`, sans la limite du port USB) :
- listage d'un dossier de 1 000 fichiers : 25 ms sur un disque, 276 ms sur le téléphone ;
- lecture par le fournisseur de documents (ce que voit VLC) : 241 Mo/s ;
- copie de 64 Mio : 118 Mo/s du téléphone vers le disque, 96 Mo/s du disque vers le téléphone.

### Limites actuelles

- **Dossiers protégés du téléphone** : Android interdit à toute application, même avec l'accès à tous les
  fichiers, de lire `Android/data` et `Android/obb`. Portail affiche « Dossier protégé par Android ».
- **Liens symboliques et fichiers spéciaux** : listés, mais ni suivis ni copiés ; une copie qui en
  rencontre le signale à la fin (« Les liens symboliques et fichiers spéciaux ne sont pas copiés »). C'est vrai
  aussi sur un serveur SFTP.
- **Dossiers distants** :
  - **pas de corbeille** : la suppression y est définitive, et la boîte de suppression le dit ;
  - **FTP n'est pas chiffré** : le mot de passe et les fichiers circulent en clair, préférez SFTP hors de chez vous ;
  - la place libre d'un serveur est inconnue : elle n'est pas vérifiée avant une copie ou une extraction ;
  - vignettes des images de 8 Mio au plus, pas de vignette de vidéo ;
  - au plus 3 transferts en même temps par serveur ; au-delà, une opération attend (30 s au plus) ;
  - pas d'FTPS (FTP sur TLS) ni d'authentification SSH par agent.

- **Écriture ext4** : vérifiée sur PC et sur le téléphone avec des images, puis essayée sur le vrai disque
  (LUKS2 et ext4 : copie de 1 Gio dans les deux sens, renommage, suppression, `e2fsck -fn` propre sous
  Ubuntu, fichiers appartenant à l'utilisateur). Sauvegardez avant d'écrire sur un disque important.
- **Écriture refusée** (le volume s'ouvre en lecture seule, avec la raison) sur un ext4 sans journal,
  sans extents (ancien ext3 converti), à journal externe, avec un journal à sommes v1 ou v2, ou avec
  une option inconnue de Portail. Le journal doit avoir été rejoué (disque bien démonté sous Ubuntu).
- **Écriture FAT et exFAT** : vérifiée sur PC (`fsck.fat`, `fsck.exfat`, un lecteur indépendant,
  coupures simulées, images hostiles), sur le téléphone avec des images, puis essayée sur une vraie clé.
- **FAT ou exFAT mal éjecté** : une clé débranchée sans « Éjecter » (ou retirée d'un ordinateur sans
  éjection) s'ouvre en lecture seule, jusqu'à sa vérification sur un ordinateur (chkdsk sous Windows,
  `fsck.fat` ou `fsck.exfat` sous Linux). Portail ne répare pas un système de fichiers FAT abîmé.
- **Écriture NTFS** : vérifiée sur PC (un lecteur NTFS indépendant écrit pour les tests, `ntfsfix -n`,
  coupures simulées, images hostiles), sur le téléphone avec des images, puis essayée sur un vrai disque.
  La relecture par le pilote ntfs3 du noyau Linux (script fourni) reste à faire. Sauvegardez avant d'écrire
  sur un disque Windows important.
- **NTFS en lecture seule** quand Windows n'a pas fermé le volume proprement :
  - Windows en veille prolongée ou en **démarrage rapide** (réglage par défaut de Windows 10 et 11) :
    arrêtez complètement Windows (Maj + Arrêter) avant d'y écrire ;
  - disque débranché de Windows sans éjection, journal `$LogFile` à rejouer, ou chkdsk demandé :
    rebranchez-le sous Windows, puis éjectez-le proprement ;
  - version de NTFS autre que 3.0 ou 3.1, secteurs NTFS plus petits que ceux du disque, ou volume plus grand
    que le disque (disque cloné sur un plus petit).
- **Limites NTFS** :
  - fichiers **compressés** par Windows : lus, mais pas modifiés (« Fichier non modifiable ici ») ;
    aucun fichier créé par Portail n'est compressé ;
  - fichiers **chiffrés** par Windows (EFS) : listés, mais ni lus ni modifiés ;
  - un nom de plus de 255 octets en UTF-8 n'est pas listé ;
  - les fichiers créés n'ont pas de droits Windows particuliers (« Tout le monde ») ;
  - le journal des modifications `$UsnJrnl` n'est pas tenu à jour (comme ntfs-3g) : un logiciel de
    sauvegarde Windows qui s'en sert peut rater des changements faits sur le téléphone ;
  - liens symboliques et jonctions : listés, mais pas suivis.
- **Formatage** :
  - tout le disque, en une seule partition : pas de partitionnement à la carte, pas de formatage d'une
    seule partition ;
  - FAT32 jusqu'à 2 Tio (au-delà, choisissez exFAT), et au moins 34 Mio ; 8 Mio au moins pour les autres
    formats, 16 Mio de plus s'il est chiffré ;
  - étiquette : 11 caractères sans accent en FAT32, 11 caractères sans `/ \ : * ? " < > |` en exFAT,
    16 octets en ext4, 32 caractères en NTFS ;
  - le formatage est rapide (les données ne sont pas réécrites) : l'ancien contenu reste lisible avec des
    outils de récupération, sauf sur un disque chiffré. Pour un disque qui a contenu des données en
    clair sensibles, effacez-le d'abord sur un ordinateur ;
  - gardez Portail ouvert pendant le formatage : Android peut arrêter une application en arrière-plan, et
    le disque serait à reformater ;
  - l'étiquette d'un disque NTFS n'est pas encore affichée sur sa carte.
- **Archives** :
  - archives protégées par mot de passe : non prises en charge (« Archive protégée par mot de passe ») ;
  - RAR en lecture seule (le format n'est pas libre en écriture) : Portail compresse en ZIP ;
  - archives en plusieurs volumes (`.part1.rar`, `.z01`…) non prises en charge ;
  - liens et fichiers spéciaux d'une archive non extraits ; les fichiers extraits portent la date de
    l'extraction, pas celle de l'archive.
- **Corbeille** : les suppressions faites par une autre application (Fichiers d'Android) sur un disque restent
  définitives. Un nom de plus de 245 octets est restauré à la racine du stockage.
- **Raccourcis de l'accueil** : les disques USB n'y figurent pas (Android ne les indexe pas ; la recherche de
  l'explorateur les couvre). La date de prise de vue n'est lue que dans les JPEG (une photo HEIC est datée par
  sa date de modification).
- **LVM, BitLocker, btrfs, XFS…** sont signalés comme « non pris en charge ».
- **Boîtiers USB** : seuls ceux qui proposent le protocole *Bulk-Only Transport* fonctionnent, ce qui est
  le cas de la plupart. Un boîtier en UAS seul est refusé avec un message clair.
- **Alimentation** : un disque dur 2,5" peut demander plus de courant que le téléphone n'en fournit. Il
  faut alors un hub USB-C alimenté.
- **Clé USB-A** : il faut un adaptateur OTG USB-A → USB-C.

## Sécurité

- **Aucun root, aucune modification du système.** Tout se fait dans l'application : USB, déchiffrement et
  système de fichiers.
- **La phrase de passe n'est jamais enregistrée.** Elle circule en octets, effacés juste après usage. Elle
  n'apparaît dans aucun journal ni aucune notification.
- **La clé maîtresse reste en mémoire native.** Elle est effacée à l'éjection, au verrouillage et au
  débranchement.
- **Le contenu du disque est traité comme hostile**, de même que ce qu'envoient les autres applications.
  Toutes les structures lues sur le disque sont vérifiées avant usage.
- **En écriture, le moindre doute fait passer le volume en lecture seule.** C'est le cas d'une option
  ext4 inconnue, d'un journal à rejouer ou d'une somme de contrôle fausse, et l'application affiche la
  raison. Après une erreur d'écriture en cours de session, plus rien n'est écrit : le disque garde la
  dernière transaction validée, que le journal sait récupérer.
- **Rien d'un disque chiffré n'est écrit sur le téléphone à votre insu.** Les vignettes sont gardées en
  mémoire seulement (32 Mio au plus), et vidées à l'éjection ; les résultats de recherche ne sont pas
  enregistrés. Seule une action explicite (copier, partager, ouvrir avec) sort un fichier du disque.

## Installation

L'application n'est pas publiée sur le Play Store. Elle s'installe à la main depuis les sources.

Il faut Android 8.0 (API 26) ou plus récent. L'application a été testée sur un Pixel 4 XL sous Android 13.

```sh
git clone --recurse-submodules <url-du-dépôt> Portail
cd Portail
flutter pub get
flutter build apk --release
adb install -r build/app/outputs/flutter-apk/app-release.apk
```

La construction demande les outils listés plus bas, dans [Développement](#développement).

## Utilisation

1. Au premier lancement, l'écran de bienvenue propose **Autoriser l'accès** : activez Portail dans la page
   Android qui s'ouvre, puis touchez **Commencer**. Sans cet accès, les disques USB restent utilisables, et la
   carte « Accès aux fichiers du téléphone » de l'accueil le redemande. L'accueil liste le téléphone et la carte
   SD : touchez-en un pour le parcourir.
2. Branchez le disque sur le port USB-C du téléphone. Android propose d'ouvrir Portail : acceptez, puis
   autorisez l'accès au périphérique USB.
3. Dans **Disques USB**, touchez **Déverrouiller** sur la partition chiffrée et saisissez la phrase de
   passe. Une partition en clair s'ouvre avec **Ouvrir**.
4. Touchez **Parcourir** pour l'explorer dans Portail, ou **Ouvrir dans Fichiers**. Le disque apparaît
   aussi dans le sélecteur de fichiers de toutes les applications.
5. Pour copier ou déplacer : appui long sur un fichier pour le sélectionner, **Copier** ou **Couper**,
   puis, dans le dossier de destination (sur n'importe quel stockage), **Coller ici**. Le menu **⋮** de la
   sélection propose aussi **Renommer**, **Compresser** et **Propriétés** ; **Supprimer** met à la corbeille.
   Pour extraire une archive, touchez-la.
6. Pour **formater** un disque (tout son contenu est effacé) : menu **⋮** de sa carte, **Formater…**,
   choisissez le format, l'étiquette et éventuellement le chiffrement, puis tapez le nom du disque pour
   confirmer. Éjectez d'abord ses volumes ouverts.
7. Pour finir, touchez **Éjecter**, dans l'application ou dans la notification « Disque ouvert », avant
   de débrancher le disque. Si vous avez écrit, attendez la fin de « Écriture des données en
   attente… ».

Pour n'ouvrir les disques qu'en lecture, activez **Ouvrir les disques en lecture seule** dans les
**Réglages** avant de déverrouiller. Le **verrouillage automatique** des disques chiffrés se règle au
même endroit.

## Comment ça marche

Sans root, Android interdit dm-crypt, `mount` et les pilotes de systèmes de fichiers du noyau. Portail
refait donc toute la chaîne dans l'espace utilisateur :

```
Interface Flutter (Dart)       accueil, explorateur, recherche, corbeille, réglages, liés par Pigeon
Couche Kotlin                  UsbManager, service au premier plan, DocumentsProvider,
                               sources de fichiers (téléphone, carte SD, disques), copies, archives,
                               corbeille, vignettes
Moteur natif C11 (JNI)         USB (Bulk-Only Transport + SCSI) → cache de blocs → GPT/MBR
                               → LUKS1/LUKS2 → ext4 (lwext4) · FAT/exFAT (FatFs) · NTFS (libntfs-3g)
                               → API fichiers unifiée ; archives (libarchive)
```

Le moteur natif ne dépend pas de Flutter. Le DocumentsProvider fonctionne même quand l'interface est
fermée, quand une autre application interroge le disque.

Bibliothèques utilisées :
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls) : AES et SHA ;
- l'implémentation de référence d'[Argon2](https://github.com/P-H-C/phc-winner-argon2) ;
- [cJSON](https://github.com/DaveGamble/cJSON) : métadonnées LUKS2 ;
- [lwext4](https://github.com/gkostka/lwext4) : ext4. Il est corrigé par les patchs de
  `native/third_party/patches/lwext4/` ; le sous-module reste sur le commit amont.
- [FatFs](https://elm-chan.org/fsw/ff/) R0.16 : FAT12/16/32 et exFAT, et leur formatage. Il est copié tel
  quel dans `native/third_party/fatfs/` et corrigé par `native/third_party/patches/fatfs/` (voir
  `native/third_party/PATCHES.md`).
- [ntfs-3g](https://github.com/tuxera/ntfs-3g) 2022.10.3 : libntfs-3g pour NTFS, et `mkntfs`, appelé
  comme une bibliothèque, pour le formatage. Corrigé par `native/third_party/patches/ntfs-3g/`.
- [libarchive](https://www.libarchive.org/) 3.8.10 : lecture des ZIP, 7z, RAR, TAR, GZ et XZ, écriture des
  ZIP ; avec liblzma de [XZ Utils](https://tukaani.org/xz/) 5.8.4 pour le format xz. Copiés dans
  `native/third_party/libarchive/` et `native/third_party/xz/`, sans correctif (voir
  `native/third_party/PATCHES.md`).

## Développement

### Prérequis

- Flutter 3.41.4 (Dart 3.11.1) et JDK 17 ou plus ;
- SDK Android dans `~/Android` : plateforme 36, NDK 28.2.13676358 et CMake 3.22.1 ;
- pour le moteur natif et ses tests sur PC (Linux) : cmake ≥ 3.20, ninja, gcc ou clang, python3 avec
  `cryptography`, cryptsetup, e2fsprogs ≥ 1.47, dosfstools, exfatprogs, mtools et ntfs-3g.

### Tests

```sh
# moteur natif sur PC, avec ASan/UBSan (les images de test sont générées sans root)
cmake -S native -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFL_TESTS=ON -DFL_SANITIZE=ON
cmake --build build/native
ctest --test-dir build/native --output-on-failure -LE lent    # environ 10 min
ctest --test-dir build/native --output-on-failure -L lent     # batteries longues, environ 1 h 40
sudo tools/fixtures/verify_rw_with_kernel.sh build/native/batterie   # images écrites, jugées par le noyau
FL_KEEP=1 ctest --test-dir build/native -R test_ntfs_batterie        # garde les images NTFS écrites…
sudo tools/fixtures/verify_ntfs_with_kernel.sh build/native/tmp/ntfs-batterie-<type>.img \
  build/native/tmp/ntfs-batterie-<type>.manifest                   # …et les fait relire par ntfs3
sudo tools/fixtures/essais_phase8_pc.sh   # formate 10 images et les fait monter et écrire par le noyau

# moteur natif sur le téléphone (écrit seulement dans /data/local/tmp/fluluks/)
tools/android/run_native_tests.sh <numéro-adb>

# Kotlin et Flutter
(cd android && ./gradlew testDebugUnitTest)
(cd android && ./gradlew connectedDebugAndroidTest -Pandroid.injected.androidTest.leaveApksInstalledAfterRun=true)
tools/android/check_image.sh <numéro-adb> engine-ecriture.img   # e2fsck des images écrites sur le téléphone
# les essais instrumentés des archives lisent trois archives de build/native/fixtures-archives/, à pousser avant :
# adb push build/native/fixtures-archives/{test_read_format_7zip_lzma2.7z,test_read_format_rar5_multiple_files.rar,hostile.zip} /data/local/tmp/fluluks/fixtures/
# ceux du lecteur vidéo lisent video-phase12.mkv (deux pistes audio, un sous-titre), fabriquée par ffmpeg puis poussée :
# tools/fixtures/make_video_phase12.sh build/native/fixtures-video
# adb push build/native/fixtures-video/video-phase12.mkv /data/local/tmp/fluluks/fixtures/
flutter test && flutter analyze
```

Diagnostic en lecture seule d'un disque refusé en écriture (sommes calculées et lues, parcours de
l'arborescence) : `sudo build/native/fluluks-cli diagnostic /dev/sdX <n°>`.

Plus de détails sur le moteur, l'outil `fluluks-cli` et la vérification par le noyau Linux :
[native/README.md](native/README.md).

### Organisation du dépôt

| Dossier | Contenu |
|---|---|
| `native/` | Moteur C11, couche JNI, tests natifs, lwext4, FatFs, libntfs-3g et leurs correctifs |
| `android/app/src/main/kotlin/` | USB, gestionnaire de volumes, service, DocumentsProvider, gestionnaire de fichiers (`files/`), pont Pigeon |
| `lib/`, `pigeons/` | Interface Flutter et définition du pont |
| `tools/` | Génération des images de test, scripts téléphone, clé USB de test, rendu du logo |
| `assets/logo/` | Logo source (SVG) et ses rendus PNG pour l'application |
| `assets/licenses/` | Licences des bibliothèques Java, montrées dans « À propos » |
| `docs/captures/` | Captures du README |
| `docs/superpowers/` | Conception, plans de chaque phase, feuille de route et essais |

### Branches

Le dépôt suit git flow :
- `main` porte les versions publiées, étiquetées `vX.Y.Z` ;
- `develop` porte l'intégration ;
- une branche `feature/…` est ouverte par phase ou par sujet ;
- `release/x.y.z` sert à préparer chaque version.

## Feuille de route

| Phase | Contenu | Version |
|---|---|---|
| 1 | Moteur natif en lecture sur PC | — |
| 2 | Android en lecture : « mon disque dans VLC » | **v0.1.0** |
| 3 | ext4 en écriture | **v0.2.1** |
| 4 | FAT16 / FAT32 / exFAT | **v0.3.0** |
| 5 | NTFS | **v0.4.0** |
| 6 | Portail : nom, logo, gestionnaire de fichiers (téléphone, carte SD, disques) | **v0.5.0** |
| 7 | Robustesse : verrouillage automatique, débranchement inattendu, performances, ext4 sur le vrai disque | **v1.0.0** |
| 8 | Formatage des disques USB : FAT32, exFAT, ext4, NTFS, en clair ou chiffré LUKS2 | **v1.1.0** |
| 9 | Compression et décompression (ZIP, 7z, RAR, TAR), corbeille, analyse de l'espace, propriétés et SHA-256, renommage par lot, filtres | **v1.2.0** |
| 10 | Visionneuse d'images, lecteurs de musique et de vidéo, éditeur de texte, dossiers distants FTP puis SFTP | **v1.3.0** |
| 11 | Interface complète et professionnelle : thèmes clair et sombre, accessibilité, grands écrans, liste ou grille partout, recherche dans tous les stockages, aide, à propos et licences | **v1.4.0** |
| 12 | Lecteur vidéo : portrait par défaut, rotation et verrou, pistes audio et sous-titres, gestes comme VLC | v1.5.0 |
| 13 | Vérification et réparation des disques : e2fsck, ntfsfix, fsck.fat, fsck.exfat, en-têtes LUKS | v1.6.0 |
| 14 | Gestion des partitions comme GParted : taille au formatage, créer, supprimer, redimensionner | v1.7.0 |

Suivi détaillé, étape par étape : [docs/superpowers/plans/2026-10-17-fluluks-feuille-de-route.md](docs/superpowers/plans/2026-10-17-fluluks-feuille-de-route.md).
Conception : [docs/superpowers/specs/2026-10-17-fluluks-design.md](docs/superpowers/specs/2026-10-17-fluluks-design.md).

## Licence

Aucune licence n'est encore choisie pour le code de Portail. Mais l'application est liée à libntfs-3g
(GPL version 2 ou ultérieure, depuis la phase 5) et à une partie de lwext4 sous GPL : distribuée, elle doit
l'être sous GPL, avec ses sources. FatFs est sous une licence de type BSD à une clause, compatible avec la
GPL, comme libarchive (BSD à deux clauses) et liblzma (0BSD). Depuis la phase 10, l'application embarque aussi
AndroidX Media3 1.11.1 et Apache Commons Net 3.13.0 (Apache-2.0), JSch 2.28.8 de mwiede (BSD) et Bouncy Castle
1.86 (licence MIT), toutes compatibles avec la GPL ; Apache FtpServer 1.2.1 et Apache MINA SSHD 2.20.0 (Apache-2.0)
ne servent qu'aux tests. Les icônes du lecteur vidéo (phase 12) viennent d'AOSP et de Media3 (Apache-2.0). Les
licences des bibliothèques natives sont dans
`native/third_party/` (`ntfs-3g/COPYING`, `fatfs/LICENSE.txt`, `libarchive/COPYING`, `xz/COPYING`…) ; leurs correctifs sont décrits dans
[native/third_party/PATCHES.md](native/third_party/PATCHES.md). Depuis la phase 11, toutes ces licences se lisent aussi
dans l'application : À propos → **Licences des bibliothèques**.
