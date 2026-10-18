# FluLuks — Phase 1 : moteur natif en lecture — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** une bibliothèque C portable (`libfluluks`) et un outil `fluluks-cli`, qui lisent sur PC Linux
une image disque ou un vrai disque (`/dev/sda`) : partitions GPT/MBR, LUKS1/LUKS2, puis fichiers ext4.

**Architecture :** des couches empilées derrière une seule interface « périphérique bloc »
(`fl_bdev`) : fichier → tranche de partition → déchiffrement LUKS → système de fichiers. La
cryptographie s'appuie sur mbedTLS et Argon2 (référence), le JSON LUKS2 sur cJSON et ext4 sur lwext4.
Tout est testé sur PC contre des images générées sans root, puis vérifiées une fois par le noyau Linux.

**Tech Stack :** C11, CMake ≥ 3.22 + Ninja, gcc/clang, ASan/UBSan, CTest ; mbedTLS v3.6.5,
phc-winner-argon2 20190702, cJSON v1.7.18, lwext4 `58bcf89` (master du 2022-09-22) ; Python 3 +
`cryptography` et e2fsprogs/cryptsetup/sgdisk/sfdisk pour les images de test.

**Spec :** [docs/superpowers/specs/2026-10-17-fluluks-design.md](../specs/2026-10-17-fluluks-design.md)

**Suivi :** cocher la tâche dans [la feuille de route](2026-10-17-fluluks-feuille-de-route.md) à la fin
de chaque tâche, dans le même commit.

## Global Constraints

- Branche de travail : `feature/moteur-natif-lecture`, créée depuis `develop`. Un commit par tâche, avec
  un message en français de type `feat:` / `test:` / `chore:`, **sans aucune ligne d'attribution à
  Claude**, sous l'identité git de l'utilisateur (`mawena <gamligocharles@gmail.com>`).
- Code natif en **C11**, portable Linux x86_64 **et** Android arm64 : pas d'API spécifique à glibc dans
  `native/src` (l'effacement passe par `fl_memzero`, qui n'utilise `explicit_bzero` que sur glibc ou Android ≥ 28), et pas de `printf` dans la
  bibliothèque (seul l'outil CLI écrit sur la sortie).
- Nos sources compilent avec `-Wall -Wextra -Werror`. Les dépendances tierces compilent sans `-Werror`.
- Les tests tournent en Debug avec `-fsanitize=address,undefined`. Aucun rapport de sanitizer n'est
  toléré.
- **Jamais** de phrase de passe ni de clé dans une sortie, un journal ou un message d'erreur. Les clés
  maîtresses sont allouées par `fl_secure_alloc` et effacées par `fl_secure_free`.
- Chiffrements à gérer (spec § 4.1) : `aes-xts-plain64` (indispensable), `aes-cbc-essiv:sha256`,
  `aes-cbc-plain64`. Tout autre chiffrement → `FL_ENOTSUP`.
- KDF : PBKDF2 (sha1, sha256, sha512), Argon2i, Argon2id.
- Paramètres du disque réel, qui servent de référence (spec § 3) : LUKS2, aes-xts-plain64, clé de
  512 bits, secteurs de 512 octets, argon2id t=7 m=1 048 576 Kio p=4, AF 4000/sha256, digest
  pbkdf2-sha256. ext4 : `has_journal ext_attr resize_inode dir_index filetype extent 64bit flex_bg
  sparse_super large_file huge_file dir_nlink extra_isize metadata_csum`, blocs de 4096, inodes de 256.
- Toute entrée disque est **hostile** : aucun décalage, aucune taille ni aucun compteur lu sur le
  disque n'est utilisé sans vérification de bornes.
- Phase 1 = **lecture seule** : un fichier image ou un périphérique est ouvert en `O_RDONLY`.

## Review Focus

Les cas que la spec implique et qu'une personne rencontrera, mais qu'aucun test « normal » n'exerce :

1. **En-têtes tronqués ou corrompus** (image LUKS réduite à 4 Kio, JSON LUKS2 abîmé, GPT invalide) →
   `FL_ECORRUPT` ou repli sur la copie de secours, jamais de plantage ni de lecture hors limites.
   *Tests : T4 `test_gpt_hors_bornes`, `test_gpt_secours` ; T8 `test_luks2_tronque`,
   `test_luks2_entete_secondaire`.*
2. **Partition déclarée au-delà de la fin du disque** → la partition est ignorée et signalée, les autres
   restent lisibles. *Test : T4 `test_gpt_hors_bornes`.*
3. **Phrase de passe non ASCII** (accents, emoji, en UTF-8) → elle déverrouille comme sous Ubuntu.
   *Test : T8 `test_luks2_deuxieme_slot_utf8`.*
4. **Taille du disque qui n'est pas un multiple du secteur de chiffrement** (segment `dynamic`) → le
   dernier secteur partiel est ignoré, sans erreur. *Test : T8 `test_luks2_taille_dynamique_impaire`.*
5. **Fichiers de plus de 4 Gio** (vidéos) → lecture correcte à des décalages > 2³². *Test : T9
   `test_ext4_lecture_au_dela_de_4gio`.*

---

## Arborescence créée par cette phase

```
native/
  CMakeLists.txt                  # bibliothèque fluluks, outil, tests, cible fixtures
  cmake/third_party.cmake         # compilation des dépendances (sans -Werror)
  include/fluluks/
    error.h  blockdev.h  partition.h  probe.h  luks.h  fs.h  secure.h
  src/
    error.c  secure.c
    blockdev/  blockdev.c  file_bdev.c  slice_bdev.c
    part/      gpt.c  mbr.c  partition.c  crc32.c  crc32.h
    probe/     probe.c
    crypto/    fl_crypto.h  fl_crypto.c  sector_cipher.c
    luks/      luks.c  luks_internal.h  luks1.c  luks2.c  af.c  crypt_bdev.c  b64.c
    fs/        fs.c  fs_internal.h  ext4_glue.c
  tools/fluluks_cli.c
  tests/
    support/tt.h                  # mini-framework d'assertions
    test_error.c test_blockdev.c test_partition.c test_probe.c test_crypto.c
    test_luks1.c test_luks2.c test_ext4.c test_cli.sh
  third_party/  mbedtls/ argon2/ cjson/ lwext4/     # sous-modules git épinglés
tools/fixtures/
  make_tree.py  luks_encrypt.py  craft.py  make_fixtures.sh  verify_with_kernel.sh
```

Commandes de référence, utilisées par toutes les tâches :

```bash
cmake -S native -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFL_TESTS=ON -DFL_SANITIZE=ON
cmake --build build/native
ctest --test-dir build/native --output-on-failure -LE lent      # rapide
ctest --test-dir build/native --output-on-failure                # tout, y compris « lent »
```

---

### Task 1 : Squelette natif et dépendances

**Files :**
- Create : `native/CMakeLists.txt`, `native/cmake/third_party.cmake`, `native/include/fluluks/error.h`,
  `native/src/error.c`, `native/include/fluluks/secure.h`, `native/src/secure.c`,
  `native/tests/support/tt.h`, `native/tests/test_error.c`
- Modify : `.gitignore` (ajouter `build/`)
- Sous-modules : `native/third_party/{mbedtls,argon2,cjson,lwext4}`

**Interfaces :**
- Produces :
  ```c
  /* error.h */
  typedef enum {
    FL_OK = 0, FL_EIO = -1, FL_EINVAL = -2, FL_ENOMEM = -3, FL_ENOTSUP = -4, FL_EBADPASS = -5,
    FL_ECORRUPT = -6, FL_EROFS = -7, FL_ENOENT = -8, FL_EEXIST = -9, FL_ENOTDIR = -10,
    FL_EISDIR = -11, FL_ENOTEMPTY = -12, FL_ENOSPC = -13
  } fl_err;
  const char *fl_strerror(int err);          /* jamais NULL ; code inconnu → "erreur inconnue" */
  /* secure.h */
  void *fl_secure_alloc(size_t n);           /* zéroïsé, mlock best-effort ; NULL si échec */
  void  fl_secure_free(void *p, size_t n);   /* explicit_bzero, munlock, free ; NULL accepté */
  ```
  `tt.h` : `TT_ASSERT(cond)`, `TT_ASSERT_EQ(a, b)` (entiers 64 bits, affiche les deux valeurs),
  `TT_ASSERT_STR_EQ(a, b)`, `TT_ASSERT_MEM_EQ(a, b, n)`, `TT_RUN(fn)` et `TT_MAIN_END()` (code de sortie
  ≠ 0 si un échec), `tt_fixture(const char *nom)` qui renvoie `$FL_FIXTURES/<nom>` (tampon statique).
- CMake : option `FL_TESTS`, option `FL_SANITIZE`, cible bibliothèque statique `fluluks`, fonction
  `fl_add_test(nom [LABELS l…])` qui compile `tests/<nom>.c`, la lie à `fluluks`, l'enregistre dans
  CTest avec `ENVIRONMENT FL_FIXTURES=${CMAKE_BINARY_DIR}/fixtures` et `FIXTURES_REQUIRED fx`.

- [ ] **Step 1 : créer la branche** — `git checkout develop && git checkout -b feature/moteur-natif-lecture`

- [ ] **Step 2 : ajouter les sous-modules aux versions épinglées**

```bash
git submodule add https://github.com/Mbed-TLS/mbedtls native/third_party/mbedtls
git -C native/third_party/mbedtls checkout v3.6.5 && git -C native/third_party/mbedtls submodule update --init --recursive
git submodule add https://github.com/P-H-C/phc-winner-argon2 native/third_party/argon2 && git -C native/third_party/argon2 checkout 20190702
git submodule add https://github.com/DaveGamble/cJSON native/third_party/cjson && git -C native/third_party/cjson checkout v1.7.18
git submodule add https://github.com/gkostka/lwext4 native/third_party/lwext4 && git -C native/third_party/lwext4 checkout 58bcf89
```

- [ ] **Step 3 : écrire `third_party.cmake`.** Il crée des cibles statiques :
  - `fl_mbedcrypto` via `add_subdirectory` de mbedTLS avec `ENABLE_PROGRAMS=OFF`, `ENABLE_TESTING=OFF`,
    `GEN_FILES=OFF` ; utiliser sa cible `mbedcrypto`. Vérifier que `MBEDTLS_CIPHER_MODE_XTS` et
    `MBEDTLS_PKCS5_C` sont actifs dans la configuration par défaut, sinon fournir un
    `MBEDTLS_USER_CONFIG_FILE`.
  - `fl_argon2` : `src/{argon2.c,core.c,encoding.c,thread.c,ref.c,blake2/blake2b.c}`, liée à `Threads`.
    On prend `ref.c` partout, car `opt.c` exige SSE.
  - `fl_cjson` : `cJSON.c`.
  - `fl_lwext4` : `src/*.c` avec `CONFIG_USE_DEFAULT_CFG=1`, `CONFIG_EXT4_BLOCKDEVS_COUNT=8`,
    `CONFIG_EXT4_MOUNTPOINTS_COUNT=8`, `CONFIG_DEBUG_PRINTF=0`, `CONFIG_DEBUG_ASSERT=0`.

- [ ] **Step 4 : écrire le test qui échoue** `tests/test_error.c` :

```c
TT_ASSERT_STR_EQ(fl_strerror(FL_EBADPASS), "phrase de passe incorrecte");
TT_ASSERT_STR_EQ(fl_strerror(12345), "erreur inconnue");
unsigned char *k = fl_secure_alloc(64); TT_ASSERT(k != NULL);
for (int i = 0; i < 64; i++) TT_ASSERT_EQ(k[i], 0);
fl_secure_free(k, 64); fl_secure_free(NULL, 0);
```

- [ ] **Step 5 : vérifier l'échec** : `cmake --build build/native` → erreur de lien (`fl_strerror`
  indéfini).

- [ ] **Step 6 : implémenter `error.c` et `secure.c`.** Les messages exacts de `fl_strerror` :
  FL_OK « succès », FL_EIO « erreur d'entrée/sortie », FL_EINVAL « argument invalide », FL_ENOMEM
  « mémoire insuffisante », FL_ENOTSUP « format non pris en charge », FL_EBADPASS « phrase de passe
  incorrecte », FL_ECORRUPT « données corrompues ou illisibles », FL_EROFS « volume en lecture seule »,
  FL_ENOENT « fichier ou dossier introuvable », FL_EEXIST « existe déjà », FL_ENOTDIR « n'est pas un
  dossier », FL_EISDIR « est un dossier », FL_ENOTEMPTY « dossier non vide », FL_ENOSPC « plus d'espace
  disponible ».

- [ ] **Step 7 : vérifier** : la configuration, la compilation (dépendances comprises) puis
  `ctest -R test_error` passent. À ce stade, la cible `fixtures` est un `add_test` vide
  `generate_fixtures` (`FIXTURES_SETUP fx`) ; T2 la remplit.

- [ ] **Step 8 : commit**, en cochant T1 dans la feuille de route —
  `git commit -m "chore: squelette du moteur natif et dépendances épinglées"`

---

### Task 2 : Images de test (fixtures) et vérification par le noyau

Elles se génèrent **sans root**. Les tests les lisent dans `build/native/fixtures/` (ignoré par git).

**Files :**
- Create : `tools/fixtures/make_tree.py`, `tools/fixtures/luks_encrypt.py`, `tools/fixtures/craft.py`,
  `tools/fixtures/make_fixtures.sh`, `tools/fixtures/verify_with_kernel.sh`
- Modify : `native/CMakeLists.txt` (le test `generate_fixtures` lance
  `make_fixtures.sh ${CMAKE_BINARY_DIR}/fixtures`)

**Interfaces :**
- Produces : les fichiers ci-dessous, dans `$FL_FIXTURES`. Les noms et valeurs sont **exactement**
  ceux-là, car les tâches suivantes les utilisent.

| Fichier | Contenu |
|---|---|
| `tree/` | `hello.txt` = `"Bonjour FluLuks\n"` · `vide.txt` (0 octet) · `accentué é ☀.txt` = `"utf8\n"` · `sous-dossier/données.bin` = 1 Mio, octet i = `(i*31+7) & 0xFF` · `gros.bin` = 5 Mio, même motif · `beaucoup/f000.txt` … `f299.txt` (contenu = leur nom + `\n`) · `clairsemé.bin` : fichier creux de 5 368 709 120 octets dont les 11 derniers octets valent `FIN-FLULUKS` |
| `pass.txt` | `motdepasse-FluLuks` (sans saut de ligne) |
| `pass2.txt` | `mot de passe été ☀` en UTF-8 (sans saut de ligne) |
| `ext4-plain.img` | 64 Mio, `mkfs.ext4 -q -F -T default -b 4096 -I 256 -O ^orphan_file,^metadata_csum_seed -L FLUTEST -U 0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0 -d tree/` ; `dumpe2fs -h` doit afficher exactement les options du disque réel |
| `gpt-luks2.img` | GPT, partition 1 nommée `FluData`, de LBA 2048 jusqu'à la fin moins 1 Mio. Elle contient LUKS2 aes-xts-plain64 512 bits, secteurs de 512, argon2id `--pbkdf-force-iterations 4 --pbkdf-memory 65536 --pbkdf-parallel 4`, avec `ext4-plain.img` chiffré dedans. Clé de volume : `gpt-luks2.vk` |
| `whole-luks2-4k.img` | Sans table. LUKS2 `--sector-size 4096`, aes-xts-plain64 512 bits, argon2i t=4 m=32768 p=2, ext4 dedans. Clé `whole-luks2-4k.vk` |
| `whole-luks2-2slots.img` | Sans table. LUKS2 aes-cbc-plain64 256 bits, pbkdf2-sha256 1000 itérations. Slot 0 = `pass.txt`, slot 1 = `pass2.txt`. ext4 dedans |
| `mbr-luks1-essiv.img` | MBR, partition 1 de type 0x83 à LBA 2048. LUKS1 aes-cbc-essiv:sha256 256 bits, hash sha256, `--pbkdf-force-iterations 1000`, ext4 dedans |
| `whole-luks1-xts.img` | Sans table. LUKS1 aes-xts-plain64 512 bits, hash sha1, 1000 itérations, ext4 dedans |
| `whole-luks2-exact.img` | **Paramètres exacts du disque réel** : argon2id t=7 m=1048576 p=4, aes-xts-plain64 512, secteurs de 512, ext4 dedans. Utilisé seulement par les tests étiquetés `lent` |
| `whole-luks2-impair.img` | Copie de `whole-luks2-4k.img` à laquelle on ajoute 1536 octets à la fin |
| `luks2-entete-abime.img` | Copie de `whole-luks2-4k.img`, avec un octet inversé au décalage 4096 + 100 (JSON du premier en-tête) |
| `luks-tronque.img` | Les 4096 premiers octets de `whole-luks2-4k.img` |
| `mbr-etendue.img` | 16 Mio, MBR : p1 0x0C (LBA 2048, 4 Mio), p2 0x05 étendue contenant p5 0x83 (2 Mio) et p6 0x82 (2 Mio) |
| `gpt-secours.img` | 8 Mio, GPT avec 2 partitions ; CRC de l'en-tête **primaire** abîmé (l'en-tête de secours reste bon) |
| `gpt-hors-bornes.img` | 8 Mio, GPT avec 2 partitions dont la n° 2 se termine au LBA 100 000 ; CRC recalculés (`craft.py`) |
| `fat16.img` / `fat32.img` | `mkfs.vfat -F 16 -n FLUFAT16` (32 Mio) / `mkfs.vfat -F 32 -n FLUFAT32` (64 Mio) |
| `ntfs.img` | 32 Mio, `mkfs.ntfs -F -f -q -L FLUNTFS` |
| `swap.img` | 4 Mio, `mkswap` |
| `sig-exfat.img`, `sig-lvm2.img`, `sig-bitlocker.img`, `sig-btrfs.img`, `sig-xfs.img` | 1 Mio de zéros + la signature seule (`craft.py`, voir T5) |
| `cipher-kat.json` | Vecteurs de référence calculés par Python : pour chaque mode (`xts-plain64`/512 bits/secteur 512, `xts-plain64`/512 bits/secteur 4096, `cbc-essiv:sha256`/256 bits, `cbc-plain64`/256 bits) : `key`, `sector_size`, `iv_sector` = 1000, `plain` (2 secteurs au motif), `cipher`, le tout en hexadécimal |

Toutes les images LUKS se font en deux temps. D'abord
`cryptsetup luksFormat -q --volume-key-file <vk> --key-file pass.txt …` sur un fichier aux dimensions
voulues. Ensuite `luks_encrypt.py` chiffre `ext4-plain.img` dans le segment de données, au décalage lu
par `cryptsetup luksDump`.

- [ ] **Step 1 : écrire `luks_encrypt.py`**, à lancer avec `python3 -I`.
  `luks_encrypt.py encrypt|decrypt --image F --offset O --sector-size S --mode M --key-file K --plain P [--large-iv]`.
  Conventions d'IV *(décision du planificateur, qui doit être confirmée au step 5 par le noyau)* :
  - `plain64` : numéro de secteur sur 64 bits little-endian, complété par des zéros jusqu'à 16 octets ;
  - `essiv:sha256` : IV = AES-ECB(clé = SHA-256(clé de volume)) appliqué au bloc `plain64` ;
  - `cbc-plain64` : IV = le bloc `plain64` ;
  - le numéro de secteur compte en **unités de 512 octets pour LUKS1**, et en **unités de
    `sector_size` pour LUKS2** (`iv_large_sectors`), en partant de 0 au début du segment.

  `kat` produit `cipher-kat.json`.

- [ ] **Step 2 : écrire `make_tree.py`, `craft.py` et `make_fixtures.sh`.** Le script est idempotent : il
  ne refait rien si `$OUT/.stamp` contient déjà le SHA-256 des scripts. Il s'arrête à la première erreur
  (`set -euo pipefail`). Si `mke2fs -d` ne garde pas `clairsemé.bin` creux et que l'image déborde,
  injecter ce fichier ensuite avec `debugfs -w -R "write …"` à la place.

- [ ] **Step 3 : vérifier sans root** : `ctest --test-dir build/native -R generate_fixtures` passe,
  `e2fsck -fn fixtures/ext4-plain.img` sort avec 0, et `cryptsetup luksDump` sur chaque image LUKS
  affiche les paramètres du tableau. Un déchiffrement aller-retour
  `luks_encrypt.py decrypt … | cmp - ext4-plain.img` réussit.

- [ ] **Step 4 : écrire `verify_with_kernel.sh`** (sudo). Pour chaque image LUKS (sauf `-exact`,
  `-impair`, `-abime` et `-tronque`), il enchaîne :
  1. `losetup -r -P -f --show`, puis `cryptsetup open --readonly --key-file pass.txt` ;
  2. `mount -o ro,noload` ;
  3. `diff -r tree/ <point de montage>` (sans `clairsemé.bin`, qu'on compare par `cmp` sur ses
     11 derniers octets) ;
  4. le nettoyage, assuré par un `trap`.

  Il affiche `NOYAU OK : <image>` ou bien `NOYAU ÉCHEC : <image>`, et sort avec un code ≠ 0 au premier
  échec.

- [ ] **Step 5 : faire lancer le script par l'utilisateur** :
  `sudo tools/fixtures/verify_with_kernel.sh build/native/fixtures`.
  Résultat attendu : un `NOYAU OK` pour chacune des 5 images. **Si une image échoue**, c'est la
  convention d'IV du step 1 qui est fausse : la corriger avant de continuer, car T7 et T8 en dépendent.

- [ ] **Step 6 : commit**, en cochant T2 — `git commit -m "test: génération des images de test et vérification par le noyau"`

---

### Task 3 : Périphériques bloc

**Files :** Create `native/include/fluluks/blockdev.h`, `native/src/blockdev/{blockdev.c,file_bdev.c,slice_bdev.c}`, `native/tests/test_blockdev.c`

**Interfaces :**
- Produces :
  ```c
  typedef struct fl_bdev fl_bdev;
  typedef struct {
    int  (*read)(fl_bdev *d, uint64_t lba, uint32_t count, void *buf);
    int  (*write)(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf);
    int  (*flush)(fl_bdev *d);
    void (*destroy)(fl_bdev *d);
  } fl_bdev_ops;
  struct fl_bdev { const fl_bdev_ops *ops; uint32_t sector_size; uint64_t sector_count; bool read_only; void *priv; };

  int  fl_bdev_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf);         /* FL_EINVAL hors bornes */
  int  fl_bdev_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf);  /* FL_EROFS si read_only */
  int  fl_bdev_flush(fl_bdev *d);
  void fl_bdev_close(fl_bdev *d);                                                 /* NULL accepté */
  int  fl_bdev_read_bytes(fl_bdev *d, uint64_t offset, size_t len, void *buf);    /* non aligné ; FL_EINVAL hors bornes */
  uint64_t fl_bdev_size_bytes(const fl_bdev *d);

  int fl_file_bdev_open(const char *path, uint32_t sector_size, bool read_only, fl_bdev **out);
  int fl_slice_bdev_new(fl_bdev *parent, uint64_t first_lba, uint64_t count, fl_bdev **out); /* ne possède pas parent */
  ```
  `fl_file_bdev_open` : pour un périphérique bloc (`S_ISBLK`), la taille de secteur et la taille totale
  viennent de `BLKSSZGET`/`BLKGETSIZE64`, et l'argument `sector_size` est ignoré. Pour un fichier, la
  taille vient de `fstat`, et le dernier secteur partiel est ignoré. Les lectures passent par `pread`
  en boucle (lectures courtes, `EINTR`). Erreur système → `FL_EIO`, fichier absent → `FL_ENOENT`.

- [ ] **Step 1 : écrire les tests qui échouent** (`test_blockdev.c`, sur un fichier temporaire de 10 000
  octets au motif `i & 0xFF` créé par le test) :
  - `test_fichier_taille` : `sector_size` = 512, `sector_count` = 19 (10 000 / 512, partiel ignoré) ;
  - `test_lecture_secteur` : le secteur 3 est égal aux octets 1536 à 2047 du motif ;
  - `test_hors_bornes` : `fl_bdev_read(d, 18, 2, buf)` renvoie `FL_EINVAL`, et
    `fl_bdev_read(d, UINT64_MAX, 1, buf)` aussi (pas de dépassement d'entier) ;
  - `test_lecture_octets_non_alignee` : `read_bytes(1000, 700)` correspond au motif ;
  - `test_lecture_seule` : `fl_bdev_write` renvoie `FL_EROFS` ;
  - `test_tranche` : une tranche (2, 5), puis `read(lba 0)` sur la tranche = secteur 2 du parent ;
    `read(lba 5)` → `FL_EINVAL` ; une tranche qui dépasse le parent → `FL_EINVAL` à la création ;
  - `test_absent` : un chemin inexistant renvoie `FL_ENOENT`.
- [ ] **Step 2 : vérifier l'échec** (symboles indéfinis).
- [ ] **Step 3 : implémenter** les trois fichiers.
- [ ] **Step 4 : vérifier** : `ctest -R test_blockdev` passe, sans rapport ASan.
- [ ] **Step 5 : commit**, en cochant T3 — `feat: périphériques bloc fichier et tranche`

---

### Task 4 : Tables de partitions

**Files :** Create `native/include/fluluks/partition.h`, `native/src/part/{partition.c,gpt.c,mbr.c,crc32.c,crc32.h}`, `native/tests/test_partition.c`

**Interfaces :**
- Consumes : `fl_bdev_read_bytes`, `fl_bdev_size_bytes` (T3).
- Produces :
  ```c
  typedef enum { FL_PT_NONE = 0, FL_PT_MBR = 1, FL_PT_GPT = 2 } fl_pt_kind;
  typedef struct {
    uint32_t index;          /* GPT : 1..128 ; MBR : 1..4 primaires, 5.. logiques ; NONE : 0 */
    uint64_t first_lba, sector_count;
    uint8_t  mbr_type;       /* 0 en GPT */
    uint8_t  gpt_type[16];   /* zéros en MBR */
    char     name[73];       /* nom GPT en UTF-8 (converti depuis UTF-16LE), "" sinon */
  } fl_partition;
  typedef struct {
    fl_pt_kind kind; uint32_t count; fl_partition parts[128];
    uint32_t skipped;        /* entrées ignorées car hors bornes */
    bool used_backup_gpt;
  } fl_part_table;
  int fl_part_scan(fl_bdev *d, fl_part_table *out);
  ```
  Règles :
  - un MBR de protection (type 0xEE) mène à la lecture du GPT ;
  - GPT : vérifier le CRC32 de l'en-tête et celui du tableau d'entrées ; en cas d'échec, lire
    l'en-tête de secours au dernier LBA ;
  - MBR étendu (types 0x05, 0x0F, 0x85) : suivre la chaîne des EBR (au plus 128 maillons, contre les
    boucles), les partitions logiques sont numérotées à partir de 5 ;
  - aucune signature 0x55AA en 510, ou aucune partition → `FL_PT_NONE` avec une seule entrée d'index 0
    couvrant tout le disque ;
  - une entrée qui sort du disque est ignorée (`skipped++`).

- [ ] **Step 1 : écrire les tests qui échouent** :
  - `test_gpt` (`gpt-luks2.img`) : `kind` = GPT, `count` = 1, `parts[0].index` = 1, `first_lba` = 2048,
    `name` = `"FluData"` ;
  - `test_mbr_etendue` (`mbr-etendue.img`) : les index valent {1, 5, 6} (la p2 étendue elle-même
    n'est pas listée), les types {0x0C, 0x83, 0x82} ;
  - `test_mbr_simple` (`mbr-luks1-essiv.img`) : `count` = 1, `mbr_type` = 0x83, `first_lba` = 2048 ;
  - `test_sans_table` (`whole-luks2-4k.img`) : `kind` = NONE, `count` = 1, `index` = 0,
    `sector_count` = tout le disque ;
  - `test_gpt_secours` (`gpt-secours.img`) : `count` = 2, `used_backup_gpt` = true ;
  - `test_gpt_hors_bornes` (`gpt-hors-bornes.img`) : `count` = 1, `skipped` = 1, aucun rapport ASan.
- [ ] **Step 2 : vérifier l'échec.**
- [ ] **Step 3 : implémenter** (CRC32 IEEE en table, dans `crc32.c`).
- [ ] **Step 4 : vérifier** que `ctest -R test_partition` passe.
- [ ] **Step 5 : commit**, en cochant T4 — `feat: lecture des tables de partitions GPT et MBR`

---

### Task 5 : Détection du contenu

**Files :** Create `native/include/fluluks/probe.h`, `native/src/probe/probe.c`, `native/tests/test_probe.c`

**Interfaces :**
- Consumes : `fl_bdev_read_bytes` (T3).
- Produces :
  ```c
  typedef enum {
    FL_CONTENT_UNKNOWN = 0, FL_CONTENT_LUKS1, FL_CONTENT_LUKS2, FL_CONTENT_EXT4, FL_CONTENT_FAT12,
    FL_CONTENT_FAT16, FL_CONTENT_FAT32, FL_CONTENT_EXFAT, FL_CONTENT_NTFS, FL_CONTENT_LVM2,
    FL_CONTENT_BITLOCKER, FL_CONTENT_BTRFS, FL_CONTENT_XFS, FL_CONTENT_SWAP
  } fl_content;
  typedef struct { fl_content content; char label[64]; char uuid[40]; } fl_probe_result;
  int fl_probe(fl_bdev *d, fl_probe_result *out);      /* FL_OK même si UNKNOWN */
  const char *fl_content_name(fl_content c);           /* "LUKS2", "ext4", "FAT32", "exFAT", "NTFS", "LVM2", "BitLocker", "btrfs", "XFS", "swap", "inconnu", … */
  bool fl_content_supported(fl_content c);             /* vrai : LUKS1, LUKS2, EXT4 (phase 1) */
  ```
  Signatures, testées dans cet ordre :
  - **LUKS** : `"LUKS\xBA\xBE"` à 0, version u16 BE à 6. UUID à 168. Label LUKS2 à 24.
  - **BitLocker** : `"-FVE-FS-"` à 3.
  - **NTFS** : `"NTFS    "` à 3. Label : il exige de parcourir la MFT, donc on laisse `""` en phase 1.
  - **exFAT** : `"EXFAT   "` à 3.
  - **FAT** : 0x55AA à 510 et un BPB cohérent (octets par secteur dans {512, 1024, 2048, 4096},
    secteurs par cluster en puissance de 2). Le type se déduit du nombre de clusters (< 4085 → FAT12,
    < 65525 → FAT16, sinon FAT32). Label à 43 (FAT12/16) ou à 71 (FAT32), espaces finaux retirés.
  - **ext2/3/4** : 0xEF53 LE à 1080. Label à 1144 (16 octets). UUID à 1128, formaté
    `xxxxxxxx-xxxx-…`.
  - **LVM2** : `"LABELONE"` à 512 et `"LVM2 001"` à 536.
  - **XFS** : `"XFSB"` à 0.
  - **btrfs** : `"_BHRfS_M"` à 65600.
  - **swap** : `"SWAPSPACE2"` à 4086.

  Une lecture qui sortirait d'un petit périphérique compte comme une signature absente, pas comme une
  erreur. `craft.py` (T2) doit écrire exactement ces signatures.

- [ ] **Step 1 : écrire les tests qui échouent.**
  - `test_probe_ext4` : `ext4-plain.img` → EXT4, label `"FLUTEST"`,
    uuid `"0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0"`.
  - `test_probe_luks` : `whole-luks2-4k.img` → LUKS2, `whole-luks1-xts.img` → LUKS1, et la tranche p1 de
    `gpt-luks2.img` → LUKS2.
  - `test_probe_fat` : `fat16.img` → FAT16 `"FLUFAT16"`, `fat32.img` → FAT32 `"FLUFAT32"`.
  - `test_probe_autres` : `ntfs.img` → NTFS ; `swap.img` → SWAP ; `sig-*` → respectivement EXFAT, LVM2,
    BITLOCKER, BTRFS, XFS.
  - `test_probe_inconnu` : la tranche p5 de `mbr-etendue.img` (des zéros) → UNKNOWN.
  - `test_probe_minuscule` : une tranche de 1 secteur → UNKNOWN, sans erreur.
- [ ] **Step 2 : vérifier l'échec.**
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** que `ctest -R test_probe` passe.
- [ ] **Step 5 : commit**, en cochant T5 — `feat: détection du contenu des partitions`

---

### Task 6 : Briques cryptographiques

**Files :** Create `native/src/crypto/{fl_crypto.h,fl_crypto.c,sector_cipher.c}`, `native/tests/test_crypto.c`

**Interfaces :**
- Produces (interne à la bibliothèque, en-tête `src/crypto/fl_crypto.h`) :
  ```c
  size_t fl_hash_len(const char *hash);                  /* "sha1"→20, "sha256"→32, "sha512"→64, inconnu→0 */
  int fl_hash(const char *hash, const void *data, size_t len, void *out);
  int fl_pbkdf2(const char *hash, const void *pw, size_t pwlen, const void *salt, size_t saltlen,
                uint32_t iterations, void *out, size_t outlen);
  typedef enum { FL_ARGON2I = 1, FL_ARGON2ID = 2 } fl_argon2_type;
  int fl_argon2(fl_argon2_type t, const void *pw, size_t pwlen, const void *salt, size_t saltlen,
                uint32_t t_cost, uint32_t m_cost_kib, uint32_t parallel, void *out, size_t outlen);
                /* FL_ENOMEM si l'allocation échoue ; version 0x13 */
  typedef struct fl_sector_cipher fl_sector_cipher;
  int  fl_sector_cipher_new(const char *spec /* "aes-xts-plain64", "aes-cbc-essiv:sha256", "aes-cbc-plain64" */,
                            const uint8_t *key, size_t keylen, uint32_t sector_size, fl_sector_cipher **out);
                            /* spec inconnue → FL_ENOTSUP ; mauvaise longueur de clé → FL_EINVAL */
  int  fl_sector_decrypt(fl_sector_cipher *c, uint64_t iv_sector, const void *in, void *out, size_t len);
  int  fl_sector_encrypt(fl_sector_cipher *c, uint64_t iv_sector, const void *in, void *out, size_t len);
       /* len multiple de sector_size ; le secteur k du tampon utilise l'IV iv_sector + k */
  void fl_sector_cipher_free(fl_sector_cipher *c);       /* efface les clés */
  ```
  L'appelant fournit `iv_sector` dans la bonne unité (512 octets ou `sector_size`) : le chiffreur n'en
  décide pas. La structure interne contient les contextes mbedTLS (et le contexte ESSIV), et son
  allocation passe par `fl_secure_alloc`.

- [ ] **Step 1 : écrire les tests qui échouent.**
  - `test_pbkdf2_rfc7914` : `fl_pbkdf2("sha256", "passwd", 6, "salt", 4, 1, out, 64)` = `55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783`.
  - `test_argon2i_reference` : `fl_argon2(FL_ARGON2I, "password", 8, "somesalt", 8, 2, 65536, 4, out, 24)` = `45d7ac72e76f242b20b77b9bf9bf9d5915894e669a24e6c6` (le vecteur du README de phc-winner-argon2, version 1.3).
  - `test_kat_secteurs` : pour chaque entrée de `cipher-kat.json` (lue avec cJSON), `encrypt(plain)` = `cipher` et `decrypt(cipher)` = `plain`.
  - `test_spec_inconnue` : `"twofish-xts-plain64"` → `FL_ENOTSUP` ; `"aes-xts-plain64"` avec une clé de 20 octets → `FL_EINVAL`.
- [ ] **Step 2 : vérifier l'échec.**
- [ ] **Step 3 : implémenter.** Pour XTS, utiliser `mbedtls_aes_xts_setkey_dec/enc` et
  `mbedtls_aes_crypt_xts`, avec une unité de données égale au secteur. Pour ESSIV, chiffrer le bloc
  `plain64` en AES-ECB avec la clé SHA-256(clé). Pour PBKDF2, utiliser `mbedtls_pkcs5_pbkdf2_hmac_ext`.
  Pour Argon2, utiliser `argon2_ctx`, avec `lanes = threads = parallel`.
- [ ] **Step 4 : vérifier** que `ctest -R test_crypto` passe.
- [ ] **Step 5 : commit**, en cochant T6 — `feat: briques cryptographiques (PBKDF2, Argon2, chiffrement de secteurs)`

---

### Task 7 : LUKS1

**Files :** Create `native/include/fluluks/luks.h`, `native/src/luks/{luks.c,luks_internal.h,luks1.c,af.c,crypt_bdev.c}`, `native/tests/test_luks1.c`

**Interfaces :**
- Consumes : T3 (bdev), T6 (crypto), `fl_secure_alloc` (T1).
- Produces :
  ```c
  /* luks.h — API publique */
  typedef struct fl_luks fl_luks;
  int  fl_luks_open(fl_bdev *dev, fl_luks **out);      /* lit et valide l'en-tête ; FL_ENOTSUP si pas LUKS ; FL_ECORRUPT si incohérent */
  int  fl_luks_version(const fl_luks *l);              /* 1 ou 2 */
  const char *fl_luks_cipher(const fl_luks *l);        /* ex. "aes-xts-plain64" */
  int  fl_luks_unlock(fl_luks *l, const void *pass, size_t passlen, fl_bdev **decrypted);
       /* essaie chaque keyslot actif ; FL_EBADPASS si aucun ne convient. Le bdev rendu dépend de dev et de l (fermer le bdev avant fl_luks_close) */
  void fl_luks_close(fl_luks *l);                      /* efface toute clé ; NULL accepté */

  /* luks_internal.h */
  int fl_af_merge(const char *hash, const uint8_t *src, size_t keylen, uint32_t stripes, uint8_t *key);
  int fl_crypt_bdev_new(fl_bdev *dev, uint64_t data_offset_bytes, uint64_t data_size_bytes /* 0 = jusqu'à la fin */,
                        const char *cipher_spec, const uint8_t *mk, size_t mklen,
                        uint32_t sector_size, bool iv_large_sectors, uint64_t iv_tweak, fl_bdev **out);
  ```
  - En-tête LUKS1 : tous les champs sont big-endian. `cipherName` à 8, `cipherMode` à 40, `hashSpec` à
    72, `payloadOffset` (en secteurs de 512) à 104, `keyBytes` à 108, `mkDigest[20]` à 112,
    `mkDigestSalt[32]` à 132, `mkDigestIter` à 164, `uuid` à 168. Les 8 keyslots de 48 octets
    commencent à 208 : `active` (0x00AC71F3 = actif), `iterations`, `salt[32]`, `keyMaterialOffset`
    (en secteurs), `stripes`.
  - Déverrouillage d'un slot :
    1. `K = PBKDF2(hashSpec, pass, salt, iterations, keyBytes)` ;
    2. déchiffrer `keyBytes*stripes` octets, arrondis au secteur, au décalage `keyMaterialOffset*512`,
       avec `cipherName-cipherMode` et des IV 0, 1, … en unités de 512 ;
    3. `fl_af_merge` ;
    4. vérifier `PBKDF2(hashSpec, mk, mkDigestSalt, mkDigestIter, 20)` == `mkDigest`, sinon passer au
       slot suivant.
  - Avant toute allocation, contrôler que `stripes` ≤ 65 536, que `keyBytes` ∈ {16, 32, 64}, et que les
    zones de clé sont dans le périphérique.
  - AF merge, selon la spécification LUKS. On pose `d` = zéros, puis pour i de 0 à stripes−2 :
    `d = diffuse(d XOR bloc_i)`. La clé vaut alors `d XOR bloc_(stripes−1)`. `diffuse` hache chaque
    morceau de `hash_len` octets : `H(be32(index_morceau) || morceau)`, tronqué à la taille du morceau
    (le dernier peut être plus court).
  - `crypt_bdev` : `sector_size` = celui du segment. `sector_count` =
    ⌊(taille utile) / sector_size⌋. IV = `iv_tweak + lba × (iv_large_sectors ? 1 : sector_size/512)`.
    `read_only` est hérité du parent.
- [ ] **Step 1 : écrire les tests qui échouent.**
  - `test_luks1_essiv` : la tranche p1 de `mbr-luks1-essiv.img`, `pass.txt` → FL_OK ; version 1 ;
    cipher `"aes-cbc-essiv:sha256"` ; le contenu déchiffré est **identique octet par octet** à
    `ext4-plain.img` (comparaison complète) ; `fl_probe` → EXT4.
  - `test_luks1_xts_sha1` : même chose avec `whole-luks1-xts.img`.
  - `test_luks1_mauvaise_phrase` : `"faux"` → `FL_EBADPASS`.
  - `test_af_merge_aller_retour` : découper une clé connue avec une implémentation de `af_split` locale
    au test, puis vérifier que `fl_af_merge` la retrouve (stripes 4000, sha256).
- [ ] **Step 2 : vérifier l'échec.**
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** que `ctest -R test_luks1` passe, sans rapport ASan.
- [ ] **Step 5 : commit**, en cochant T7 — `feat: déverrouillage LUKS1`

---

### Task 8 : LUKS2

**Files :** Create `native/src/luks/{luks2.c,b64.c}`, `native/tests/test_luks2.c` ; Modify `native/src/luks/luks.c` (aiguillage selon la version)

**Interfaces :**
- Consumes : `fl_af_merge`, `fl_crypt_bdev_new` (T7) ; cJSON ; T6.
- Produces : aucune nouvelle API publique (on passe par `fl_luks_*`).
  `int fl_b64_decode(const char *in, uint8_t *out, size_t outmax, size_t *outlen);` (interne,
  `FL_ECORRUPT` si invalide).
- En-tête binaire (big-endian) : `magic` à 0 (`"LUKS\xBA\xBE"` pour le primaire, `"SKUL\xBA\xBE"` pour le
  secondaire), `version` à 6, `hdr_size` (u64) à 8, `seqid` à 16, `label[48]` à 24, `csum_alg[32]` à 72,
  `uuid[40]` à 168, `hdr_offset` à 256, `csum[64]` à 448.
  - Somme de contrôle : `csum_alg`(sha256) sur les `hdr_size` octets (binaire + JSON), le champ `csum`
    étant mis à zéro.
  - Le secondaire se trouve à `hdr_size` octets ; si le primaire est illisible, on le cherche aux
    décalages 0x4000, 0x8000, 0x10000, 0x20000, 0x40000, 0x80000, 0x100000, 0x200000 et 0x400000.
  - Si les deux en-têtes sont valides, on garde le `seqid` le plus grand.
  - `hdr_size` doit être compris entre 16 Kio et 4 Mio.
- JSON : les entiers 64 bits sont des **chaînes**.
  - `keyslots` : on ne gère que le type `"luks2"`. Champs : `key_size`, `af{type:"luks1", stripes,
    hash}`, `area{type:"raw", offset, size, encryption, key_size}` et
    `kdf{type: "pbkdf2"|"argon2i"|"argon2id", hash|time|memory|cpus, iterations, salt(b64)}`.
  - `segments` : on ne gère que le type `"crypt"`. Champs : `offset`, `size` (`"dynamic"` ou un
    nombre), `iv_tweak`, `encryption`, `sector_size`.
  - `digests` : type `"pbkdf2"`. Champs : `keyslots[]`, `segments[]`, `hash`, `iterations`, `salt`,
    `digest`.
  - Plusieurs segments, ou un segment non `crypt` (réchiffrement en cours) → `FL_ENOTSUP`.
- Déverrouillage :
  - Les slots sont triés par `priority` (2 = haute, 1 = normale, 0 = jamais).
  - `K = kdf(pass, salt)` sur `area.key_size` octets.
  - On déchiffre la zone (`area.encryption`, IV en unités de 512 à partir de 0), puis on applique
    `fl_af_merge` sur `key_size` octets.
  - Le digest lié au slot doit valoir `PBKDF2(hash, mk, salt, iterations, len(digest))`.
  - On crée enfin le `crypt_bdev` avec `iv_large_sectors = true` et `iv_tweak` = celui du segment.
- [ ] **Step 1 : écrire les tests qui échouent.** Pour chaque déverrouillage réussi, le contenu
  déchiffré doit être identique à `ext4-plain.img`.
  - `test_luks2_gpt_argon2id` : la tranche p1 de `gpt-luks2.img`.
  - `test_luks2_4k_argon2i` : `whole-luks2-4k.img`, et `sector_size` du bdev déchiffré = 4096.
  - `test_luks2_deuxieme_slot_utf8` : `whole-luks2-2slots.img` avec `pass2.txt` → FL_OK, et avec
    `pass.txt` → FL_OK aussi.
  - `test_luks2_mauvaise_phrase` → `FL_EBADPASS`.
  - `test_luks2_entete_secondaire` : `luks2-entete-abime.img` → FL_OK grâce à l'en-tête secondaire.
  - `test_luks2_tronque` : `luks-tronque.img` → `fl_luks_open` renvoie `FL_ECORRUPT`, sans rapport
    ASan.
  - `test_luks2_taille_dynamique_impaire` : `whole-luks2-impair.img` → FL_OK ; `sector_count` est
    inchangé par rapport à `whole-luks2-4k.img`.
  - `test_luks2_parametres_exacts` (étiquette **`lent`**) : `whole-luks2-exact.img` → FL_OK, le temps
    est affiché sur la sortie du test.
- [ ] **Step 2 : vérifier l'échec.**
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** : `ctest -R test_luks2` passe (y compris `lent` :
  `ctest -R test_luks2 -L lent`).
- [ ] **Step 5 : commit**, en cochant T8 — `feat: déverrouillage LUKS2 (Argon2, PBKDF2, en-tête de secours)`

---

### Task 9 : ext4 en lecture et API fichiers unifiée

**Files :** Create `native/include/fluluks/fs.h`, `native/src/fs/{fs.c,fs_internal.h,ext4_glue.c}`, `native/tests/test_ext4.c`

**Interfaces :**
- Consumes : `fl_bdev` (T3), `fl_content` (T5), lwext4.
- Produces :
  ```c
  typedef enum { FL_FT_FILE = 1, FL_FT_DIR = 2, FL_FT_SYMLINK = 3, FL_FT_OTHER = 4 } fl_ftype;
  typedef struct { fl_ftype type; uint64_t size; int64_t mtime; uint32_t mode, uid, gid; } fl_stat;
  typedef struct { char name[256]; fl_stat st; } fl_dirent;           /* UTF-8 */
  typedef struct {
    uint64_t total_bytes, free_bytes; char label[64];
    bool read_only; char ro_reason[128];                              /* "" si écriture possible */
    bool needs_recovery;                                             /* journal non rejoué */
  } fl_statfs;
  typedef struct fl_fs fl_fs;
  typedef struct fl_file fl_file;
  enum { FL_O_RDONLY = 0, FL_O_RDWR = 1, FL_O_CREAT = 2, FL_O_TRUNC = 4 };

  int fl_fs_mount(fl_bdev *dev, fl_content kind, bool want_rw, fl_fs **out); /* kind non géré → FL_ENOTSUP */
  int fl_fs_statfs(fl_fs *fs, fl_statfs *out);
  int fl_fs_stat(fl_fs *fs, const char *path, fl_stat *out);
  int fl_fs_readdir(fl_fs *fs, const char *path, int (*cb)(void *ctx, const fl_dirent *e), void *ctx);
      /* sans "." ni ".." ; un cb non nul arrête le parcours et sa valeur est renvoyée */
  int fl_fs_open(fl_fs *fs, const char *path, int flags, fl_file **out);
  int64_t fl_file_pread(fl_file *f, void *buf, size_t len, uint64_t off);   /* octets lus, 0 = fin, <0 = fl_err */
  int64_t fl_file_pwrite(fl_file *f, const void *buf, size_t len, uint64_t off);
  int fl_file_truncate(fl_file *f, uint64_t size);
  int fl_file_close(fl_file *f);
  int fl_fs_mkdir(fl_fs *fs, const char *path);
  int fl_fs_unlink(fl_fs *fs, const char *path);
  int fl_fs_rmdir(fl_fs *fs, const char *path);
  int fl_fs_rename(fl_fs *fs, const char *from, const char *to);
  int fl_fs_sync(fl_fs *fs);
  int fl_fs_unmount(fl_fs *fs);                                             /* NULL accepté */
  ```
  - Chemins absolus en UTF-8 : `"/"` pour la racine, sans `/` final.
  - `fs.c` aiguille vers une table de fonctions par système de fichiers (`fs_internal.h`) ; la phase 4
    y ajoutera FAT et la phase 5 NTFS.
  - **Phase 1 :** `fl_fs_mount` monte toujours en lecture seule, même si `want_rw` est vrai, avec
    `ro_reason` = `"écriture pas encore disponible"`. Toutes les opérations d'écriture, et `fl_fs_open`
    avec `RDWR`, `CREAT` ou `TRUNC`, renvoient `FL_EROFS`.
  - `ext4_glue.c` :
    - un `struct ext4_blockdev` par montage, branché sur `fl_bdev` (`ph_bsize` = `sector_size` du bdev,
      `bread`/`bwrite` → `fl_bdev_read`/`write`) ;
    - un nom de périphérique et un point de montage uniques par montage (`"fl%u"`, `"/fl%u/"`) ;
    - `ext4_mount(..., read_only = true)` ;
    - `needs_recovery` = le bit `EXT4_FINCOM_RECOVER` du superbloc ;
    - les codes d'erreur errno de lwext4 sont convertis en `fl_err` (`ENOENT` → `FL_ENOENT`,
      `ENOTDIR` → `FL_ENOTDIR`, `ENOMEM` → `FL_ENOMEM`, le reste → `FL_EIO`).
- [ ] **Step 1 : écrire les tests qui échouent.** Ils montent `ext4-plain.img` directement, sauf le
  dernier.
  - `test_ext4_racine` : `readdir("/")` contient exactement {`hello.txt`, `vide.txt`, `accentué é ☀.txt`,
    `sous-dossier`, `gros.bin`, `beaucoup`, `clairsemé.bin`, `lost+found`}.
  - `test_ext4_stat` : `hello.txt` → FILE, taille 16 ; `sous-dossier` → DIR ; `/absent` → `FL_ENOENT`.
  - `test_ext4_lecture` : `hello.txt` = `"Bonjour FluLuks\n"` ; `vide.txt` → `pread` renvoie 0.
  - `test_ext4_motif_decalages` : sur `gros.bin`, des `pread` de 4093 octets aux décalages 0, 4095,
    1 048 577 et 5 242 879 (la lecture de fin est courte, 1 octet) correspondent au motif.
  - `test_ext4_beaucoup` : `readdir("/beaucoup")` compte 300 entrées.
  - `test_ext4_lecture_au_dela_de_4gio` : `clairsemé.bin` a une taille de 5 368 709 120 ; un `pread`
    de 11 octets à 5 368 709 109 donne `"FIN-FLULUKS"`, et un `pread` à 4 294 967 296 donne des zéros.
  - `test_ext4_lecture_seule` : `fl_fs_open(..., FL_O_RDWR)`, `mkdir` et `unlink` → `FL_EROFS` ;
    `statfs.read_only` = true, `label` = `"FLUTEST"`.
  - `test_ext4_sur_luks2` : le déverrouillage de `gpt-luks2.img` p1, puis le montage, donnent
    `hello.txt` correct.
  - `test_ext4_deux_montages` : deux montages simultanés (plain + LUKS2) fonctionnent, puis se
    démontent proprement.
- [ ] **Step 2 : vérifier l'échec.**
- [ ] **Step 3 : implémenter.** Si lwext4 refuse une option du disque réel (voir Global Constraints),
  la corriger dans le sous-module, sur une branche `fluluks` du fork. Noter le correctif dans
  `native/third_party/PATCHES.md`.
- [ ] **Step 4 : vérifier** que `ctest -R test_ext4` passe, sans rapport ASan.
- [ ] **Step 5 : commit**, en cochant T9 — `feat: lecture ext4 et API fichiers unifiée`

---

### Task 10 : Outil `fluluks-cli` et lecture du vrai disque sur PC

**Files :** Create `native/tools/fluluks_cli.c`, `native/tests/test_cli.sh` ; Modify `native/CMakeLists.txt` (exécutable `fluluks-cli` et test `test_cli`)

**Interfaces :**
- Consumes : toute l'API publique (T3–T9).
- Produces : l'exécutable `build/native/fluluks-cli`.
  ```
  fluluks-cli parts <image|périphérique>
  fluluks-cli ls    <image|périphérique> <n°> <chemin> [--pass-file F]
  fluluks-cli cat   <image|périphérique> <n°> <chemin> [--pass-file F]
  ```
  - `parts` affiche une ligne par partition :
    `<n°>\t<début LBA>\t<taille en Mo>\t<contenu>\t<label>\t<pris en charge: oui|non>`, où
    `<contenu>` = `fl_content_name`. Pour une partition LUKS, il affiche aussi `\tLUKS<v> <cipher>`.
  - `ls` affiche `<d|f|l|o>\t<taille>\t<nom>` par entrée, triées par nom (strcmp).
  - `cat` écrit le contenu brut sur stdout, par blocs de 1 Mio.
  - `<n°>` = l'index `fl_partition.index` (0 pour un disque sans table).
  - Phrase de passe : `--pass-file` prend le fichier tel quel, octet pour octet, comme
    `cryptsetup --key-file`. Sans ce paramètre, l'outil demande `Phrase de passe : ` sur `/dev/tty` avec
    l'écho coupé, et retire le `\n` final. Le tampon est effacé après usage.
  - Code de sortie : 0 en cas de succès, 2 en cas d'erreur d'usage, 1 sinon. En cas d'erreur, stderr
    reçoit `fluluks-cli: <fl_strerror>`.
  - L'image s'ouvre toujours en lecture seule.
- [ ] **Step 1 : écrire le test qui échoue** `test_cli.sh` (lancé par CTest avec `$FL_FIXTURES` et le
  chemin de l'outil). Il vérifie :
  - `parts gpt-luks2.img` contient une ligne commençant par `1\t2048\t` et contenant `LUKS2` et
    `aes-xts-plain64` ;
  - `cat gpt-luks2.img 1 /hello.txt --pass-file pass.txt` = `Bonjour FluLuks` ;
  - `ls whole-luks1-xts.img 0 / --pass-file pass.txt` contient `f\t16\thello.txt` ;
  - un `cat` de `sous-dossier/données.bin` comparé par `cmp` à `tree/sous-dossier/données.bin` →
    identique ;
  - un mauvais fichier de phrase de passe → code 1, avec stderr = `fluluks-cli: phrase de passe
    incorrecte` ;
  - **ni stdout ni stderr ne contiennent `motdepasse-FluLuks`** ;
  - sans argument → code 2.
- [ ] **Step 2 : vérifier l'échec.**
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : vérifier** que toute la suite passe :
  `ctest --test-dir build/native --output-on-failure` (avec `lent`).
- [ ] **Step 5 : faire tester le vrai disque par l'utilisateur, en lecture seule.** Le disque est branché
  sur le PC ; s'il est ouvert dans Ubuntu, ce n'est pas gênant puisqu'on ne fait que lire.

```bash
sudo build/native/fluluks-cli parts /dev/sda
sudo build/native/fluluks-cli ls /dev/sda 1 /
```

  Attendu : `1 … LUKS2 aes-xts-plain64` ; après la saisie de la phrase de passe (moins de 15 s), la
  liste de la racine correspond à `ls /media/<point de montage>` sous Ubuntu. Noter la durée du
  déverrouillage dans la feuille de route.
- [ ] **Step 6 : commit**, en cochant T10 — `feat: outil fluluks-cli`

### Clôture de la phase

- [ ] `git checkout develop && git merge --no-ff feature/moteur-natif-lecture -m "Merge branch 'feature/moteur-natif-lecture' into develop" && git branch -d feature/moteur-natif-lecture`
- [ ] Cocher la ligne « Fusion » dans la feuille de route, puis commiter sur `develop`
  (`chore(suivi): phase 1 terminée`).
