# FluLuks

FluLuks est une application Android qui ouvre les **disques externes chiffrés avec LUKS** et branchés en
USB-C, **sans root**. Le contenu du disque apparaît dans l'application **Fichiers** d'Android : on ouvre
une vidéo dans VLC, un PDF ou une photo dans les applications habituelles.

Elle est née d'un besoin simple : utiliser sur un téléphone non rooté (un Pixel 4 XL) le disque
LUKS2/ext4 qu'on utilise sous Ubuntu. Aucune application existante ne le permettait.

> **État actuel : version 0.2.1, lecture et écriture sur ext4.** L'écriture a été vérifiée sur PC
> (batteries de non-corruption jugées par `e2fsck`, `debugfs` et le noyau Linux, coupures simulées) et
> sur le téléphone avec des images, mais **pas encore sur un vrai disque USB** : faites une sauvegarde
> avant d'écrire sur un disque qui compte, ou activez « Ouvrir les disques en lecture seule ». Voir les
> [limites](#limites-actuelles).

## Ce que fait la version 0.2.1

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
- **Lecture seule quand il le faut**, avec la raison affichée : interrupteur « Ouvrir les disques en
  lecture seule », disque protégé en écriture, journal à rejouer, système de fichiers marqué en erreur,
  somme de contrôle fausse, option ext4 non prise en charge en écriture, ou erreur d'écriture en cours
  de session.
- **Expose chaque volume ouvert** dans l'application Fichiers, avec accès aléatoire : l'avance rapide
  marche dans VLC.
- **Éjecte proprement** depuis l'application ou depuis la notification : écriture des données en
  attente, démontage, `SYNCHRONIZE CACHE`, effacement de la clé, puis libération de l'USB. Un fichier
  encore ouvert par une autre application est fermé de force après la synchronisation.
- **Interface en français.**

Mesures sur un Pixel 4 XL avec le vrai disque (LUKS2, Argon2id à 1 Gio de mémoire) :
- déverrouillage en 4,3 s environ ;
- lecture d'une vidéo dans VLC à environ 65 Mo/s.

### Limites actuelles

- **Écriture non essayée sur un vrai disque USB.** Elle est vérifiée sur PC et, sur le téléphone, avec
  des images et un faux disque USB simulé ; l'essai sur un vrai disque (tâche 13 de la phase 3) reste à
  faire. Sauvegardez avant d'écrire sur un disque important.
- **Écriture refusée** (le volume s'ouvre en lecture seule, avec la raison) sur un ext4 sans journal,
  sans extents (ancien ext3 converti), à journal externe, avec un journal à sommes v1 ou v2, ou avec
  une option inconnue de FluLuks. Le journal doit avoir été rejoué (disque bien démonté sous Ubuntu).
- **FAT16/32, exFAT et NTFS** sont détectés mais pas encore pris en charge (phases 4 et 5).
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

## Installation

L'application n'est pas publiée sur le Play Store. Elle s'installe à la main depuis les sources.

Il faut Android 8.0 (API 26) ou plus récent. L'application a été testée sur un Pixel 4 XL sous Android 13.

```sh
git clone --recurse-submodules <url-du-dépôt> FluLuks
cd FluLuks
flutter pub get
flutter build apk --release
adb install -r build/app/outputs/flutter-apk/app-release.apk
```

La construction demande les outils listés plus bas, dans [Développement](#développement).

## Utilisation

1. Branchez le disque sur le port USB-C du téléphone. Android propose d'ouvrir FluLuks : acceptez, puis
   autorisez l'accès au périphérique USB.
2. Dans l'écran **Disques**, touchez **Déverrouiller** sur la partition chiffrée et saisissez la phrase
   de passe. Une partition en clair s'ouvre avec **Ouvrir**.
3. Touchez **Ouvrir dans Fichiers**. Le disque apparaît aussi dans le sélecteur de fichiers de toutes les
   applications.
4. Pour finir, touchez **Éjecter**, dans l'application ou dans la notification « Disque ouvert », avant
   de débrancher le disque. Si vous avez écrit, attendez la fin de « Écriture des données en
   attente… ».

Pour n'ouvrir les disques qu'en lecture, activez **Ouvrir les disques en lecture seule** dans l'écran
**Disques** avant de déverrouiller.

## Comment ça marche

Sans root, Android interdit dm-crypt, `mount` et les pilotes de systèmes de fichiers du noyau. FluLuks
refait donc toute la chaîne dans l'espace utilisateur :

```
Interface Flutter (Dart)       écrans Disques et Déverrouillage, liés par Pigeon
Couche Kotlin                  UsbManager, service au premier plan, DocumentsProvider
Moteur natif C11 (JNI)         USB (Bulk-Only Transport + SCSI) → cache de blocs → GPT/MBR
                               → LUKS1/LUKS2 → ext4 (lwext4) → API fichiers unifiée
```

Le moteur natif ne dépend pas de Flutter. Le DocumentsProvider fonctionne même quand l'interface est
fermée, quand une autre application interroge le disque.

Bibliothèques utilisées :
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls) : AES et SHA ;
- l'implémentation de référence d'[Argon2](https://github.com/P-H-C/phc-winner-argon2) ;
- [cJSON](https://github.com/DaveGamble/cJSON) : métadonnées LUKS2 ;
- [lwext4](https://github.com/gkostka/lwext4) : ext4. Il est corrigé par les patchs de
  `native/third_party/patches/lwext4/` ; le sous-module reste sur le commit amont.

## Développement

### Prérequis

- Flutter 3.41.4 (Dart 3.11.1) et JDK 17 ou plus ;
- SDK Android dans `~/Android` : plateforme 36, NDK 28.2.13676358 et CMake 3.22.1 ;
- pour le moteur natif et ses tests sur PC (Linux) : cmake ≥ 3.20, ninja, gcc ou clang, python3 avec
  `cryptography`, cryptsetup, e2fsprogs ≥ 1.47, dosfstools et ntfs-3g.

### Tests

```sh
# moteur natif sur PC, avec ASan/UBSan (les images de test sont générées sans root)
cmake -S native -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFL_TESTS=ON -DFL_SANITIZE=ON
cmake --build build/native
ctest --test-dir build/native --output-on-failure -LE lent    # environ 10 min
ctest --test-dir build/native --output-on-failure -L lent     # batterie longue, environ 1 h 30
sudo tools/fixtures/verify_rw_with_kernel.sh build/native/batterie   # images écrites, jugées par le noyau

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
| `native/` | Moteur C11, couche JNI, tests natifs et correctifs lwext4 |
| `android/app/src/main/kotlin/` | USB, gestionnaire de volumes, service, DocumentsProvider, pont Pigeon |
| `lib/`, `pigeons/` | Interface Flutter et définition du pont |
| `tools/` | Génération des images de test, scripts téléphone et clé USB de test |
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
| 3 | ext4 en écriture (essai sur un vrai disque à faire) | **v0.2.1** |
| 4 | FAT16 / FAT32 / exFAT | v0.3.0 |
| 5 | NTFS | v0.4.0 |
| 6 | Finitions : explorateur intégré, copies avec progression, réglages, robustesse | v1.0.0 |

Suivi détaillé, étape par étape : [docs/superpowers/plans/2026-10-17-fluluks-feuille-de-route.md](docs/superpowers/plans/2026-10-17-fluluks-feuille-de-route.md).
Conception : [docs/superpowers/specs/2026-10-17-fluluks-design.md](docs/superpowers/specs/2026-10-17-fluluks-design.md).

## Licence

Aucune licence n'est encore choisie. Une partie de lwext4 est sous GPL, de même que libntfs-3g, prévue
pour la phase 5. L'application dans son ensemble devra donc être distribuée sous GPL si elle est publiée.
