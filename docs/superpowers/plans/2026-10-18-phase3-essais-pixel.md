# Phase 3 — essais sur le Pixel et version 0.2.0 (mode d'emploi)

Pas à pas de la tâche 13 du plan de la phase 3 (`2026-10-18-phase3-ext4-ecriture.md`). Toutes les
commandes partent de la racine du dépôt, sur le PC Ubuntu, téléphone branché en USB (débogage USB
actif). Numéro de série adb : `192.168.0.104:5555` (Wi‑Fi).

```bash
git checkout feature/ext4-ecriture && git pull
git submodule update --init --recursive
export ANDROID_SERIAL=192.168.0.104:5555
adb devices                      # le téléphone doit apparaître en « device »
```

## 1. Tests automatiques sur le téléphone (avant toute clé)

Aucun de ces tests n'écrit hors de `/data/local/tmp/fluluks/` et du stockage privé de l'application.

```bash
# 1a. Tests natifs arm64 (dont crc32c matériel et son débit) — 10 à 15 min (Argon2 du vrai disque)
tools/android/run_native_tests.sh $ANDROID_SERIAL

# 1b. Tests instrumentés (moteur, gestionnaire de volumes, fournisseur Fichiers en lecture et écriture)
flutter build apk --debug
# (l'option garde l'application installée après les tests : sinon Gradle la désinstalle, et les
#  images de 1c disparaissent avec son cache)
(cd android && ./gradlew :app:connectedDebugAndroidTest -Pandroid.injected.androidTest.leaveApksInstalledAfterRun=true)

# 1c. Images écrites par ces tests, vérifiées par e2fsck sur le PC (doivent finir par « clean », code 0)
tools/android/check_image.sh $ANDROID_SERIAL engine-ecriture.img
tools/android/check_image.sh $ANDROID_SERIAL provider-plein.img
tools/android/check_image.sh $ANDROID_SERIAL provider-ecriture.img
```

Sur le PC (root), vérification par le noyau des images de la batterie de non-corruption :

```bash
cmake -S native -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFL_TESTS=ON -DFL_SANITIZE=ON
cmake --build build/native && ctest --test-dir build/native -R batterie
sudo tools/fixtures/verify_rw_with_kernel.sh build/native/batterie    # « NOYAU OK » partout
```

Envoyer le récapitulatif de 1a, le résultat de 1b (nombre de tests, échecs) et les sorties de 1c.
S'arrêter ici si quelque chose échoue.

## 2. Installer le build release, avec la capture des mesures

```bash
flutter build apk --release
adb install -r build/app/outputs/flutter-apk/app-release.apk
adb logcat -c && adb logcat -s FluLuksPerf FluLuksProvider VolumeManager VolumeService > essais-phase3.log &
```

(`FluLuksPerf` donne octets lus et écrits et durées par fichier ; arrêter la capture avec `kill %1`.)

## 3. Grille sur la clé sacrifiée

Clé préparée par `sudo tools/device/prepare_test_key.sh /dev/sdX [--video film.mp4]` (EFFACE la clé),
branchée en USB‑C (ou par adaptateur OTG). Noter pour chaque point : réussi ou non, durée, anomalie.

1. Déverrouiller (interrupteur « lecture seule » inactif) : sous la partition, « Ouvert en lecture et
   écriture — visible dans l'application Fichiers ».
2. Depuis « Fichiers », copier une vidéo d'au moins 500 Mo du téléphone vers la clé. Noter la durée,
   puis la lire dans VLC (avance rapide à 50 % et 90 %).
3. Créer un dossier, renommer, déplacer, copier, puis supprimer un fichier et un dossier.
4. Enregistrer une photo depuis une autre application (« Enregistrer sous » ou partage) sur la clé.
5. Éjecter depuis la notification (« Écriture des données en attente… » puis disparition), débrancher,
   puis sur Ubuntu :
   ```bash
   sudo cryptsetup open --readonly /dev/sdX1 essai && sudo e2fsck -fn /dev/mapper/essai   # propre
   sudo mount -o ro /dev/mapper/essai /mnt && ls -ln /mnt    # propriétaire et dates corrects
   sudo umount /mnt && sudo cryptsetup close essai
   ```
6. Rebrancher sur le téléphone, lancer une copie, et débrancher la clé **pendant** la copie : pas de
   plantage, notification « Disque débranché pendant une écriture… ». Rebrancher sur le téléphone →
   lecture seule avec « journal à rejouer : branchez le disque sous Ubuntu ». Puis sur Ubuntu :
   ```bash
   sudo cryptsetup open /dev/sdX1 essai && sudo mount /dev/mapper/essai /mnt   # le noyau rejoue le journal
   sudo umount /mnt && sudo e2fsck -fn /dev/mapper/essai && sudo cryptsetup close essai   # propre
   ```
7. Activer l'interrupteur « Ouvrir les disques en lecture seule », déverrouiller : « Lecture seule :
   ouvert en lecture seule à votre demande », et « Fichiers » ne propose plus de créer.

## 4. Vrai disque — seulement si toute la grille est réussie ET la sauvegarde complète faite

Créer un dossier `FluLuks-essai`, y copier quelques fichiers, en renommer un, en supprimer un, éjecter.
Sur Ubuntu : `cryptsetup open --readonly`, `e2fsck -fn` propre, dossier visible avec le bon
propriétaire (`ls -ln`).

## 5. Retour, puis version 0.2.0

Envoyer : résultats de 1, la grille notée (durées, débit de la copie de 500 Mo), le fichier
`essais-phase3.log`, et toute anomalie. Je consigne le tout dans la feuille de route, puis je fais la
clôture git flow (je pousse tout) :

1. `feature/ext4-ecriture` fusionnée dans `develop` (`--no-ff`) ;
2. `release/0.2.0` depuis `develop`, `version: 0.2.0+2` dans `pubspec.yaml` ;
3. fusion dans `main` (`--no-ff`), étiquette `v0.2.0`, refusion dans `develop`, branches supprimées.

Ensuite, de ton côté, pour récupérer et installer la version publiée :

```bash
git fetch --tags && git checkout v0.2.0
flutter build apk --release && adb install -r build/app/outputs/flutter-apk/app-release.apk
```

Le build release est signé avec la clé de débogage (comme la 0.1.0) : il s'installe par-dessus le build
de débogage sans désinstaller ni perdre la préférence « lecture seule ».
