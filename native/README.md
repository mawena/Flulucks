# FluLuks — moteur natif (phase 1)

Bibliothèque C11 portable (`libfluluks`) et outil `fluluks-cli` : lecture seule de disques LUKS1/LUKS2
contenant de l'ext4, sans root pour les images.

## Prérequis

- cmake ≥ 3.20, ninja, gcc ou clang, git
- python3 avec le paquet `cryptography` (génération des fixtures)
- pour les fixtures : cryptsetup, e2fsprogs, dosfstools, ntfs-3g, gdisk/fdisk

## Récupérer les sources

```sh
git clone --recurse-submodules <url-du-dépôt>
# ou, dans un clone existant :
git submodule update --init --recursive
```

## Compiler et tester

```sh
cmake -S native -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFL_TESTS=ON -DFL_SANITIZE=ON
cmake --build build/native
ctest --test-dir build/native --output-on-failure -LE lent   # suite rapide
ctest --test-dir build/native --output-on-failure -L lent    # tests lents (gros fichiers, ~1 min)
```

Les images de test (≈ 670 Mio) sont générées dans `build/native/fixtures/` par le test
`generate_fixtures` ; elles ne sont jamais commitées.

## fluluks-cli

```sh
build/native/fluluks-cli parts <image|périphérique>
build/native/fluluks-cli ls    <image|périphérique> <n°> <chemin> [--pass-file F]
build/native/fluluks-cli cat   <image|périphérique> <n°> <chemin> [--pass-file F]
```

`--pass-file F` : le contenu du fichier est la phrase de passe en octets bruts, comme
`cryptsetup --key-file` (un `\n` final fait partie de la phrase). Sans cette option, la phrase est
demandée sur le terminal, sans écho. Les codes de sortie sont 0 (succès), 1 (erreur), 2 (usage).

## Correctifs lwext4

Le sous-module `native/third_party/lwext4` reste sur le commit amont. Les correctifs du dépôt
(`native/third_party/patches/lwext4/*.patch`) sont appliqués à la configuration CMake sur une copie
dans le dossier de build. Détails : `native/third_party/PATCHES.md`.

## Vérification par le noyau (sudo)

Avant tout, fermer / démonter le volume dans Ubuntu (sinon son journal peut ne pas être à jour et le
contenu lu être incomplet ; `fluluks-cli` avertit alors sur stderr).

1. Comparer les fixtures avec ce que monte le noyau (lecture seule) :

   ```sh
   sudo tools/fixtures/verify_with_kernel.sh build/native/fixtures
   ```

2. Lecture seule d'un vrai disque (ne rien écrire ; adapter le périphérique) :

   ```sh
   sudo build/native/fluluks-cli parts /dev/sdX
   sudo build/native/fluluks-cli ls /dev/sdX 1 / --pass-file <fichier>
   ```

## Prérequis Android

Sur un clone neuf, lancer `flutter pub get` puis `flutter build apk --debug` une fois avant d'utiliser `./gradlew` : Flutter génère `gradlew`, le jar du wrapper et `local.properties`.

`disable-abi-filtering=true` (dans `android/gradle.properties`) empêche le plugin Flutter d'écraser les `abiFilters` arm64-v8a et x86_64 de `app/build.gradle.kts`.
