# Portail

Portail (anciennement FluLuks) est un **gestionnaire de fichiers** Android qui ouvre aussi les **disques
externes chiffrés avec LUKS** branchés en USB-C, **sans root**, ainsi que les clés et disques en clair
formatés en ext4, FAT, exFAT ou NTFS. Il parcourt le téléphone, la carte SD et les disques USB, et copie
ou déplace d'un stockage à l'autre. Le contenu des disques apparaît aussi dans l'application **Fichiers**
d'Android : on ouvre une vidéo dans VLC, un PDF ou une photo dans les applications habituelles.

Elle est née d'un besoin simple : utiliser sur un téléphone non rooté (un Pixel 4 XL) le disque
LUKS2/ext4 qu'on utilise sous Ubuntu. Aucune application existante ne le permettait.

> **État actuel : version 1.0.0 ; gestionnaire de fichiers, lecture et écriture sur ext4, FAT, exFAT et
> NTFS.** L'écriture a été vérifiée sur PC (batteries de non-corruption jugées par des outils indépendants
> et le noyau Linux, coupures simulées), sur le téléphone avec des images, puis essayée sur une vraie clé
> et un vrai disque pour chaque système de fichiers (ext4 compris : `e2fsck` propre ensuite sous Ubuntu).
> Une sauvegarde reste recommandée avant d'écrire sur un disque qui compte. Voir les
> [limites](#limites-actuelles).

## Ce que fait la version 1.0.0

### Robustesse (nouveau en 1.0.0)

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
  supprimer, sur le téléphone comme sur un disque.
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
  rencontre le signale à la fin (« Les liens symboliques et fichiers spéciaux ne sont pas copiés »).

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

1. Au premier lancement, touchez **Autoriser** sur la carte « Accès aux fichiers du téléphone », puis
   activez Portail dans la page Android qui s'ouvre. L'accueil liste alors le téléphone et la carte SD :
   touchez-en un pour le parcourir.
2. Branchez le disque sur le port USB-C du téléphone. Android propose d'ouvrir Portail : acceptez, puis
   autorisez l'accès au périphérique USB.
3. Dans **Disques USB**, touchez **Déverrouiller** sur la partition chiffrée et saisissez la phrase de
   passe. Une partition en clair s'ouvre avec **Ouvrir**.
4. Touchez **Parcourir** pour l'explorer dans Portail, ou **Ouvrir dans Fichiers**. Le disque apparaît
   aussi dans le sélecteur de fichiers de toutes les applications.
5. Pour copier ou déplacer : appui long sur un fichier pour le sélectionner, **Copier** ou **Couper**,
   puis, dans le dossier de destination (sur n'importe quel stockage), **Coller ici**.
6. Pour finir, touchez **Éjecter**, dans l'application ou dans la notification « Disque ouvert », avant
   de débrancher le disque. Si vous avez écrit, attendez la fin de « Écriture des données en
   attente… ».

Pour n'ouvrir les disques qu'en lecture, activez **Ouvrir les disques en lecture seule** dans les
**Réglages** avant de déverrouiller. Le **verrouillage automatique** des disques chiffrés se règle au
même endroit.

## Comment ça marche

Sans root, Android interdit dm-crypt, `mount` et les pilotes de systèmes de fichiers du noyau. Portail
refait donc toute la chaîne dans l'espace utilisateur :

```
Interface Flutter (Dart)       accueil, explorateur, recherche, réglages, liés par Pigeon
Couche Kotlin                  UsbManager, service au premier plan, DocumentsProvider,
                               sources de fichiers (téléphone, carte SD, disques), copies, vignettes
Moteur natif C11 (JNI)         USB (Bulk-Only Transport + SCSI) → cache de blocs → GPT/MBR
                               → LUKS1/LUKS2 → ext4 (lwext4) · FAT/exFAT (FatFs) · NTFS (libntfs-3g)
                               → API fichiers unifiée
```

Le moteur natif ne dépend pas de Flutter. Le DocumentsProvider fonctionne même quand l'interface est
fermée, quand une autre application interroge le disque.

Bibliothèques utilisées :
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls) : AES et SHA ;
- l'implémentation de référence d'[Argon2](https://github.com/P-H-C/phc-winner-argon2) ;
- [cJSON](https://github.com/DaveGamble/cJSON) : métadonnées LUKS2 ;
- [lwext4](https://github.com/gkostka/lwext4) : ext4. Il est corrigé par les patchs de
  `native/third_party/patches/lwext4/` ; le sous-module reste sur le commit amont.
- [FatFs](https://elm-chan.org/fsw/ff/) R0.16 : FAT12/16/32 et exFAT. Il est copié tel quel dans
  `native/third_party/fatfs/` et corrigé par `native/third_party/patches/fatfs/` (voir
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

# moteur natif sur le téléphone (écrit seulement dans /data/local/tmp/fluluks/)
tools/android/run_native_tests.sh <numéro-adb>

# Kotlin et Flutter
(cd android && ./gradlew testDebugUnitTest)
(cd android && ./gradlew connectedDebugAndroidTest -Pandroid.injected.androidTest.leaveApksInstalledAfterRun=true)
tools/android/check_image.sh <numéro-adb> engine-ecriture.img   # e2fsck des images écrites sur le téléphone
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
| `assets/logo/` | Logo source (SVG) |
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
| 8 | Formatage des disques USB : FAT32, exFAT, ext4, NTFS, en clair ou chiffré LUKS2 | v1.1.0 |
| 9 | Compression et décompression (ZIP, 7z, RAR, TAR), corbeille, analyse de l'espace, propriétés et SHA-256, renommage par lot, filtres | v1.2.0 |
| 10 | Visionneuse d'images, lecteurs de musique et de vidéo, éditeur de texte, dossiers distants FTP puis SFTP | v1.3.0 |
| 11 | Vérification et réparation des disques : e2fsck, ntfsfix, fsck.fat, fsck.exfat, en-têtes LUKS | v1.4.0 |

Suivi détaillé, étape par étape : [docs/superpowers/plans/2026-10-17-fluluks-feuille-de-route.md](docs/superpowers/plans/2026-10-17-fluluks-feuille-de-route.md).
Conception : [docs/superpowers/specs/2026-10-17-fluluks-design.md](docs/superpowers/specs/2026-10-17-fluluks-design.md).

## Licence

Aucune licence n'est encore choisie pour le code de Portail. Mais l'application est liée à libntfs-3g
(GPL version 2 ou ultérieure, depuis la phase 5) et à une partie de lwext4 sous GPL : distribuée, elle doit
l'être sous GPL, avec ses sources. FatFs est sous une licence de type BSD à une clause, compatible avec la
GPL. Les licences des bibliothèques sont dans `native/third_party/` (`ntfs-3g/COPYING`,
`fatfs/LICENSE.txt`…) ; leurs correctifs sont décrits dans
[native/third_party/PATCHES.md](native/third_party/PATCHES.md).
