# FluLuks — Document de conception

Date : 2026-10-17
Statut : en attente d'approbation de l'utilisateur

## 1. Intention

L'utilisateur possède un disque dur externe chiffré avec **LUKS**, formaté en **ext4**, qu'il utilise
sous Ubuntu. Il veut l'utiliser sur son téléphone, un **Pixel 4 XL non rooté (Android 13, API 33)**,
branché en USB-C. Aucune application existante ne le permet sur un téléphone non rooté. FluLuks comble
ce manque.

Au-delà de ce disque, l'application doit aussi ouvrir des disques **FAT16, FAT32, exFAT et NTFS**,
chiffrés en LUKS ou non.

L'expérience visée : on branche le disque, on saisit la phrase de passe si besoin, et le contenu
du disque **apparaît dans l'application « Fichiers » d'Android et dans le sélecteur de fichiers
système**. On peut alors ouvrir une vidéo dans VLC, un PDF dans son lecteur, une photo dans la
galerie, et enregistrer depuis n'importe quelle application directement sur le disque.

## 2. Objectifs, non-objectifs, anti-objectifs

### Objectifs (v1)
- Détecter un disque USB de stockage de masse branché en USB-C (OTG) sans root.
- Lire la table de partitions (GPT et MBR) et gérer aussi un disque sans table (LUKS ou système de
  fichiers directement sur le disque entier).
- Déverrouiller **LUKS1 et LUKS2** avec une phrase de passe.
- Systèmes de fichiers en **lecture et écriture** (lire, créer, modifier, renommer, déplacer,
  supprimer fichiers et dossiers) : **ext4** (priorité n° 1), **FAT16, FAT32, exFAT, NTFS**.
- Exposer chaque volume ouvert via un **DocumentsProvider** Android, pour que les fichiers
  s'ouvrent dans les applications habituelles, avec accès aléatoire (avance rapide dans une vidéo).
- Un explorateur intégré simple dans l'application Flutter, en complément.
- Éjection propre : vider les caches, valider le journal, `SYNCHRONIZE CACHE`, puis libérer l'USB.

### Non-objectifs (v1)
- Formater des disques, créer ou redimensionner des partitions, créer des conteneurs LUKS, changer
  la phrase de passe ou gérer les emplacements de clé (keyslots).
- Fichiers de clé, jetons FIDO2/TPM2, en-têtes LUKS détachés. *(décision du concepteur)*
- LVM à l'intérieur de LUKS, RAID logiciel, btrfs, XFS, HFS+, APFS, BitLocker. Ces cas doivent
  être **détectés et signalés** (« non pris en charge »), sans planter.
- Mémoriser la phrase de passe. *(décision du concepteur : jamais stockée en v1)*
- Publication sur le Play Store. L'application est d'abord destinée à un usage personnel et
  s'installe à la main (sideload). *(décision du concepteur, voir § 8 Licences)*
- iOS, ordinateur.

### Anti-objectifs (ce qui en ferait un échec même si « ça marche »)
- **Corrompre le disque.** Une application qui lit bien mais abîme l'ext4 (ou un autre système de
  fichiers) en écrivant est un échec. En cas de doute, on passe en **lecture seule** et on prévient
  l'utilisateur.
- Faire courir un risque au téléphone : pas de root, pas de déverrouillage du bootloader, pas de
  modification du système.
- Laisser fuir la clé : la clé maîtresse et la phrase de passe ne doivent jamais être écrites sur le
  stockage du téléphone, dans les journaux (logs) ou dans des rapports de plantage.

## 3. Contraintes

- **Pas de root** : impossible d'utiliser dm-crypt, `mount` ou les pilotes de systèmes de fichiers du
  noyau. **Tout se fait dans l'espace utilisateur**, à l'intérieur de l'application :
  USB → protocole de stockage de masse → déchiffrement → système de fichiers.
- Appareil de référence : Pixel 4 XL, Snapdragon 855 (ARMv8 avec extensions AES/SHA), 6 Go de RAM,
  Android 13. `minSdk` 26 (nécessaire pour `StorageManager.openProxyFileDescriptor`).
- **Paramètres réels du disque de l'utilisateur** (relevés le 2026-10-17, ils servent de référence pour
  les tests) :
  - LUKS : **version 2**, un seul segment `crypt` à l'offset 16 780 288 octets, longueur « whole
    device », chiffrement **aes-xts-plain64**, **secteurs de 512 octets**, clé de **512 bits**
    (AES-256-XTS). Un seul keyslot (0), de type `luks2` : **argon2id, time cost 7, mémoire
    1 048 576 Kio (1 Gio), 4 threads**, AF stripes 4000, AF hash sha256. Digest : pbkdf2-sha256,
    357 144 itérations. Pas de jeton (token), pas d'étiquette.
  - ext4 : options `has_journal ext_attr resize_inode dir_index filetype extent 64bit flex_bg
    sparse_super large_file huge_file dir_nlink extra_isize metadata_csum` ; journal
    `journal_incompat_revoke journal_64bit journal_checksum_v3` ; blocs de 4096 octets, inodes de
    256 octets, état `clean`. **Pas d'`orphan_file` ni de `metadata_csum_seed`.**
- **Décision de l'utilisateur : le moteur doit gérer le disque tel qu'il est**, sans lui demander de le
  modifier sous Ubuntu (pas de `tune2fs`). Toutes les options listées ci-dessus doivent donc être
  **entièrement prises en charge en écriture**. Si lwext4 en gère mal une (par exemple
  `journal_checksum_v3`, les arbres `dir_index` avec `metadata_csum`, ou la mise à jour de
  `resize_inode` et des groupes `flex_bg`), le constructeur **l'étend ou la corrige** au lieu de
  contourner le problème. Pour les **autres** disques ext4 qui présenteraient des options non gérées,
  la règle du § 4.2 s'applique : lecture seule.
- Argon2id avec 1 Gio et time cost 7 : à calculer en natif (4 fils), hors du fil d'interface, avec un
  indicateur de progression. Il faut prévoir l'allocation de 1 Gio sans se faire tuer par Android
  (le Pixel 4 XL a 6 Go de RAM).
- Le **DocumentsProvider tourne même quand l'interface Flutter n'est pas ouverte** (une autre
  application l'interroge). Le moteur (USB, LUKS, systèmes de fichiers) ne doit donc **pas dépendre
  du moteur Flutter/Dart** : il vit en code natif + Kotlin, et Flutter n'est qu'une interface au-dessus.
- Environnement de développement actuel : Flutter 3.41.4 / Dart 3.11.1, le Pixel 4 XL est visible par
  `adb`. **Le SDK Android est incomplet** : `/usr/lib/android-sdk` ne contient que `platform-tools`
  (pas de `platforms`, `build-tools`, `cmdline-tools` ni **NDK**), et `ANDROID_HOME` pointe vers
  `~/Android`, qui n'existe pas. La première étape du plan est de réparer cela.
- Le dossier projet `FluLuks` est vide et n'est pas encore un dépôt git.
- **Disque réel relevé** (2026-10-17) : 476,9 Gio, une seule partition GPT/MBR `sda1` de type
  `crypto_LUKS`, boîtier **JMicron 152d:0581** (« HAYSENSER »). Sous Linux il utilise le pilote UAS, mais
  l'interface expose aussi **Bulk-Only Transport en `bAlternateSetting` 0** : la v1 n'implémente que
  BOT et sélectionne explicitement ce réglage alternatif 0 (UAS hors périmètre).

## 4. Architecture (décidée)

```
┌──────────────────────────── Application Android ────────────────────────────┐
│  Interface Flutter (Dart)                                                   │
│   liste des disques · déverrouillage · explorateur intégré · éjecter · réglages │
│        │  canaux de plateforme (Pigeon ou MethodChannel)                     │
│  Couche Kotlin                                                              │
│   UsbManager (détection, permission) · Service au premier plan (session)     │
│   DocumentsProvider (racines = volumes ouverts) · ProxyFileDescriptor        │
│        │  JNI                                                               │
│  Moteur natif C/C++ (bibliothèque partagée, portable Linux/Android)          │
│   périphérique bloc USB (BOT/SCSI) → partitions (GPT/MBR) → LUKS1/2 →        │
│   systèmes de fichiers : ext4 · FAT/exFAT · NTFS → API fichiers unifiée      │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 4.1 Couches du moteur natif
1. **Périphérique bloc (interface abstraite)** : `read(lba, n)`, `write(lba, n)`, `flush()`,
   `taille_secteur`, `nb_secteurs`. Deux implémentations :
   - **USB** : USB Mass Storage Bulk-Only Transport + commandes SCSI (`INQUIRY`, `TEST UNIT READY`,
     `READ CAPACITY(10/16)`, `READ(10/16)`, `WRITE(10/16)`, `SYNCHRONIZE CACHE`, `REQUEST SENSE`),
     à partir du descripteur de fichier fourni par `UsbDeviceConnection` (Kotlin ouvre, le natif fait
     les transferts, par exemple avec `libusb_wrap_sys_device` ou des ioctl usbdevfs directs : au choix du
     constructeur).
   - **Fichier image** : pour les tests sur ordinateur Linux (voir § 7).
   - Un **cache de blocs** (lecture anticipée, écriture différée bornée et vidée sur `flush`).
2. **Partitions** : GPT (avec vérification CRC), MBR (y compris partitions étendues), « disque
   entier ».
3. **Détection** de ce que contient chaque partition par signatures : LUKS1/LUKS2, ext2/3/4, FAT12/16/32,
   exFAT, NTFS, et identification au moins des formats non gérés (LVM, BitLocker, btrfs, etc.).
4. **LUKS** : implémentation propre, en lecture des en-têtes :
   - LUKS1 : en-tête binaire, keyslots, PBKDF2 (SHA-1/SHA-256/…), séparateur anti-forensique (AF),
     vérification par digest.
   - LUKS2 : en-tête binaire + métadonnées JSON (avec leur copie de secours), keyslots
     **Argon2id / Argon2i / PBKDF2**, segments, digests.
   - Chiffrements : **aes-xts-plain64** (indispensable), aes-cbc-essiv:sha256 et aes-cbc-plain64
     (anciens LUKS1). Tout autre chiffrement : refus clair.
   - Le résultat est un **périphérique bloc déchiffré** (même interface que le 1.), sur lequel se
     posent les systèmes de fichiers. Chiffrement à l'écriture symétrique.
   - Cryptographie : une bibliothèque éprouvée (BoringSSL/OpenSSL ou mbedTLS) qui utilise
     les instructions AES matérielles d'ARMv8, et l'implémentation de référence d'Argon2.
     Ne pas écrire soi-même AES/XTS/SHA.
   - La clé maîtresse vit uniquement en mémoire native, est effacée (`explicit_bzero`) au
     verrouillage, à l'éjection et au débranchement, et n'est jamais remontée côté Dart ni Kotlin.
5. **Systèmes de fichiers** : bibliothèques C existantes, éprouvées, branchées sur l'interface bloc :
   - ext4 : **lwext4** (lecture/écriture, journal, extents) est le candidat retenu. Le constructeur
     **doit vérifier** qu'elle gère correctement toutes les options du disque réel (§ 3), et la
     corriger ou la compléter si besoin. Cette vérification passe par les tests du § 7, avec une image
     créée avec exactement ces options.
   - FAT16/FAT32/exFAT : **FatFs** (ChaN).
   - NTFS : **libntfs-3g** (la bibliothèque de ntfs-3g, sans FUSE, avec ses propres opérations de
     périphérique `ntfs_device_operations`).
   - Le constructeur peut proposer une autre bibliothèque s'il démontre qu'elle est plus sûre.
6. **API fichiers unifiée** (au-dessus des trois) : `stat`, `readdir`, `open/read/write/truncate/close`
   avec décalage (`offset`), `mkdir`, `unlink`, `rmdir`, `rename`, `statfs`, `sync`. Un seul verrou de
   volume (les bibliothèques ne sont pas multi-fils) ; les accès concurrents du DocumentsProvider sont
   sérialisés.

### 4.2 Règles de sécurité à l'écriture (non négociables)
Au montage, le volume est monté **en lecture seule** (avec un message qui explique pourquoi) si :
- ext4 : une option `incompat` ou `ro_compat` n'est pas prise en charge en écriture par le moteur ;
  le journal demande une récupération (`needs_recovery`) et la relecture du journal n'est pas sûre ;
  le système de fichiers est marqué en erreur (`s_state` ≠ propre) ; les sommes de contrôle échouent.
- NTFS : volume « sale » ou hiberné (démarrage rapide de Windows).
- FAT/exFAT : bit « volume sale » positionné.
- Le disque signale une protection en écriture.
- L'utilisateur a activé le réglage « lecture seule ».

En écriture :
- ext4 avec journal **activé** (transactions de métadonnées).
- **Propriétaire et droits des fichiers créés sur ext4** *(décision du concepteur)* : un fichier ou dossier
  créé depuis le téléphone **hérite de l'uid et du gid de son dossier parent**. Les droits sont
  `0644` pour les fichiers et `0755` pour les dossiers ; si le parent a le bit setgid, il est hérité
  sur les dossiers. Un fichier modifié garde son propriétaire et ses droits. But : sous Ubuntu, les
  fichiers écrits depuis le téléphone appartiennent à l'utilisateur, sans avoir besoin de `sudo`.
  Les horodatages (`mtime`, `ctime`, `crtime`) sont mis à jour à l'heure du téléphone.
- `sync` à chaque fermeture de fichier ouvert en écriture, et périodiquement. Au débranchement
  inattendu, le journal doit permettre de récupérer à la prochaine ouverture sous Ubuntu.
- Bouton **Éjecter** dans l'application et dans la notification : vide tout, puis
  `SYNCHRONIZE CACHE`, démonte, efface la clé, puis libère l'interface USB.

### 4.3 Couche Kotlin
- **Détection** : `intent-filter` `USB_DEVICE_ATTACHED` filtré sur la classe stockage de masse (0x08),
  demande de permission `UsbManager`, prise de l'interface (avec `force=true`). Note : Android peut
  avoir déjà monté lui-même une clé FAT/exFAT ; en prenant l'interface, FluLuks lui retire l'accès,
  ce qui est voulu.
- **Service au premier plan** tant qu'au moins un volume est ouvert : il garde le processus vivant pour
  que les autres applications puissent lire, et affiche une notification « Disque ouvert : [nom] —
  Éjecter ».
- **DocumentsProvider** :
  - une **racine par volume ouvert** (nom = étiquette du volume, sinon « Disque [taille] »), notifiée
    à l'ouverture et à la fermeture ; pas de racine quand rien n'est ouvert ;
  - `queryRoots`, `queryDocument`, `queryChildDocuments`, `openDocument` (modes `r`, `w`, `rw`,
    `wt`) via `StorageManager.openProxyFileDescriptor` (accès aléatoire, indispensable pour VLC),
    `createDocument`, `deleteDocument`, `renameDocument`, `moveDocument`, `copyDocument`,
    `isChildDocument` ;
  - types MIME déduits de l'extension ; vignettes facultatives (pas en v1 si coûteuses).
  - Identifiants de documents stables : `<id-volume>:<chemin>`.
- Débranchement (`USB_DEVICE_DETACHED`) : fermeture forcée, effacement de la clé, retrait de la racine,
  notification d'avertissement si des écritures étaient en cours.

### 4.4 Interface Flutter
- **Écran Disques** : disques branchés → partitions (taille, type détecté, chiffré ou non, étiquette,
  état : verrouillé / ouvert / lecture seule et pourquoi / non pris en charge).
- **Déverrouillage** : saisie de la phrase de passe, avec une progression pendant le calcul Argon2 et des
  messages d'erreur clairs (mauvaise phrase de passe, format non pris en charge).
- **Explorateur intégré** (simple) : naviguer, ouvrir avec… (Intent `ACTION_VIEW` sur l'URI du
  provider), partager, créer un dossier, renommer, supprimer, importer depuis le téléphone, exporter vers
  le téléphone, avec une progression pour les grosses copies.
- **Éjecter**, et **Réglages** (lecture seule forcée, verrouillage automatique après X minutes
  d'inactivité, désactivé par défaut).
- Langue de l'interface : **français**. *(décision du concepteur)*

## 5. Performances visées *(décision du concepteur)*
- Lecture séquentielle d'un fichier sur LUKS/ext4 ≥ **20 Mo/s** sur Pixel 4 XL avec un disque USB 3,
  suffisant pour lire une vidéo 1080p sans saccade dans VLC, avance rapide comprise.
- Déverrouillage LUKS2/Argon2id avec les réglages par défaut d'Ubuntu en moins de ~15 s, sans plantage
  pour manque de mémoire.
- Listage d'un dossier de 1 000 fichiers en moins de 2 s.

## 6. Ordre de construction *(décision du concepteur)*
Livrer de façon incrémentale, chaque étape étant utilisable :
1. Réparer l'environnement (SDK Android, cmdline-tools, NDK, `ANDROID_HOME`), créer le projet Flutter,
   puis `git init`.
2. Moteur natif testable sur Linux : périphérique bloc image + partitions + détection.
3. LUKS1/LUKS2 en lecture (sur image).
4. ext4 **en lecture seule** sur LUKS (sur image) → puis sur le téléphone via USB + DocumentsProvider.
   **Premier jalon utilisable : lire son disque dans VLC.**
5. ext4 en écriture, avec les règles du § 4.2 et une batterie de tests de non-corruption.
6. FAT/exFAT (lecture puis écriture), puis NTFS (lecture puis écriture).
7. Finitions de l'interface, réglages, robustesse au débranchement.

## 7. Stratégie de test
- Le moteur natif compile **aussi pour Linux x86_64** et se teste sur l'ordinateur contre des **images
  disque** générées par script sous Ubuntu : `cryptsetup luksFormat --type luks1|luks2` (PBKDF2,
  Argon2id, Argon2i, plusieurs chiffrements), `mkfs.ext4` (options par défaut **et** celles du disque
  réel, reproduites avec `mkfs.ext4 -O ^orphan_file,^metadata_csum_seed,...` selon la version
  d'e2fsprogs, et une image LUKS2 aux paramètres exacts du disque : argon2id 7/1 Gio/4, AES-XTS-512,
  secteurs de 512 octets), `mkfs.vfat -F 16/32`, `mkfs.exfat`, `mkfs.ntfs`, GPT/MBR/sans table.
- Tests de lecture : comparer avec ce que voit le noyau Linux (images montées via loop + dm-crypt sous
  Ubuntu, avec sudo, en préparation des données de test).
- Tests d'écriture : après chaque série d'opérations (création, ajout, écrasement, troncature,
  renommage, suppression, gros fichiers > 4 Gio, nombreux petits fichiers, arborescences profondes,
  noms Unicode), lancer **`e2fsck -fn`**, `fsck.vfat -n`, `fsck.exfat -n`, `ntfsfix -n` : **aucune
  erreur tolérée**, puis vérifier le contenu par montage noyau.
- Tests de coupure : interrompre l'écriture à des points aléatoires (simulation sur l'image) et vérifier
  que `e2fsck` récupère via le journal.
- Sur téléphone : d'abord une **clé USB sacrifiée**, jamais le disque réel. Le disque réel n'est
  utilisé **qu'en lecture** tant que l'étape 5 n'a pas passé ses tests, et **après une sauvegarde** de
  son contenu par l'utilisateur.

## 8. Licences
libntfs-3g est sous GPLv2+, et certaines parties de lwext4 aussi. L'application dans son ensemble sera
donc considérée comme **GPL**. Ce n'est pas gênant pour un usage personnel ou une publication
open source, mais c'est à revoir si l'utilisateur veut un jour la distribuer en code fermé.

## 9. Laissé au constructeur
- Le mécanisme exact d'accès USB natif (libusb enveloppé ou ioctl usbdevfs directs).
- La bibliothèque cryptographique (BoringSSL, OpenSSL ou mbedTLS), sous réserve de l'accélération
  matérielle AES.
- La forme des canaux Flutter↔Kotlin (Pigeon recommandé).
- La gestion d'état Flutter et l'organisation du code Dart.
- La taille et la politique du cache de blocs.
- Le visuel de l'interface (sobre, Material 3).
- Le système de build natif (CMake via le NDK, recommandé).

## 10. Risques connus
- **Écriture ext4 hors noyau** : c'est le plus gros risque. lwext4 est moins éprouvée que le pilote du
  noyau ; d'où le § 4.2, la batterie de fsck, et le passage en lecture seule à la moindre option non
  maîtrisée.
- **Alimentation** : un disque dur 2,5" alimenté par le bus peut demander plus que ce que fournit le
  port du Pixel. Il faudra peut-être un hub USB-C alimenté. Hors du périmètre logiciel, mais à
  signaler dans l'application si le disque ne répond pas.
- **Android tue le processus** : atténué par le service au premier plan ; il faut tout de même un
  comportement sûr (rien de corrompu) si cela arrive.
- **Argon2 gourmand en mémoire** : à surveiller sur l'appareil.

## 11. Évolution du 2026-10-20 : Portail, gestionnaire de fichiers

Décisions de l'utilisateur, après la phase 5 :
- **Nom** : l'application s'appelle **Portail**. Seul le nom visible change : le paquet
  `com.mawena.fluluks`, le dépôt et la bibliothèque native gardent leur nom *(décision du planificateur :
  changer l'`applicationId` créerait une autre application et ferait perdre les réglages)*.
- **Logo** *(remplacé le 2026-10-22, voir § 13)* : un dossier en forme de bouclier, avec une serrure, sur un dégradé indigo → violet
  (`assets/logo/portail.svg`).
- **Gestionnaire de fichiers à part entière.** L'« explorateur intégré simple » du § 2 et du § 4.4
  devient un gestionnaire complet :
  - stockages parcourus : téléphone, carte SD et disques USB (chiffrés ou non) ;
  - accès au téléphone par la permission « Accès à tous les fichiers » (`MANAGE_EXTERNAL_STORAGE`),
    acceptable puisque l'application est installée à la main (§ 2, non-objectifs) ;
  - fonctions : naviguer, ouvrir avec, partager, créer, renommer, supprimer, **copier et déplacer entre
    tous les stockages** avec progression et annulation, **recherche** par nom, **vignettes** des photos
    et vidéos, affichage en grille, **tri**, fichiers cachés, **favoris**.
- **Confidentialité** : les vignettes et résultats de recherche d'un disque chiffré ne sont jamais écrits
  sur le stockage du téléphone (cache en mémoire seulement).
- Le § 4.2 s'applique inchangé à toute écriture sur un disque USB, qu'elle vienne du gestionnaire ou du
  DocumentsProvider.
- Ordre de construction (§ 6, point 7) : phase 6 = nom, logo et gestionnaire de fichiers (v0.5.0) ;
  phase 7 = verrouillage automatique, débranchement inattendu, performances (v1.0.0).

## 12. Évolution du 2026-10-20 (suite) : demandes après les essais de la phase 6

Décisions de l'utilisateur (`docs/Manuel/maj-20-10-2026.md`) :
- **Affichage des dossiers** en liste ou en grille, choisi dans les Réglages (en plus du menu de
  l'explorateur). Phase 6, T13.
- **Lecteurs intégrés et stockages distants**, à planifier après la v1.0.0 (phase 8, devenue phase 10 le
  2026-10-22, voir § 14 et § 15) :
  - voir des images, écouter de la musique, éditer des fichiers texte, lire des vidéos dans Portail ;
  - accéder à des dossiers distants par FTP, puis par SFTP, comme sources du gestionnaire de fichiers.
- Ce qui est lu ou affiché depuis un disque chiffré suit la règle du § 11 : rien n'en est écrit sur le
  stockage du téléphone sans action explicite de l'utilisateur (copie, export).

## 13. Évolution du 2026-10-22 : phase 7

- **Logo** *(décision de l'utilisateur, dessin choisi par le planificateur)* : fond orange terracotta
  (`#E07B53` → `#C2410C`), clin d'œil à la couleur de Claude sans reprendre son logo ; une arche de
  portail blanche qui encadre un dossier, avec un petit cadenas : l'accès aux fichiers, même chiffrés.
- **Verrouillage automatique** (§ 4.4) *(décision du planificateur)* : seuls les volumes **chiffrés**
  sont verrouillés (éjectés, clé effacée) après le délai choisi : jamais, 5, 15, 30 ou 60 min ; jamais
  pendant une copie ; toute lecture ou écriture repousse le délai.
- **Débranchement** (§ 4.3) : l'avertissement donne un conseil selon le système de fichiers (ext4 : Ubuntu
  rejoue le journal ; FAT/exFAT : vérification sur un ordinateur ; NTFS : vérification par Windows).

## 14. Évolution du 2026-10-22 (suite) : formatage

Décision de l'utilisateur : le **formatage des disques USB** devient la **phase 8** (v1.1.0) ; les lecteurs
intégrés et les stockages distants passent en phase 9, puis en **phase 10** (v1.3.0, voir § 15).
- Formater un disque USB branché sur le téléphone, en **FAT32, exFAT, ext4 ou NTFS**, en clair ou **chiffré
  LUKS2** (le système de fichiers choisi dans le volume chiffré), avec une étiquette.
- Jamais le stockage du téléphone ni une carte SD montée par Android : seulement un disque dont Portail a
  pris l'interface USB.
- Formater détruit tout le contenu : confirmation en tapant le nom affiché du disque, refus si un de ses
  volumes est ouvert, taille et modèle du disque affichés.
- Les disques formatés doivent être reconnus sans réserve par les outils de référence (`e2fsck`,
  `fsck.fat`, `fsck.exfat`, `ntfsfix`, `cryptsetup luksDump`/`open`, montage par le noyau Linux) : vérifié
  sur des images sur le PC, puis sur une clé USB sacrifiée, jamais sur le vrai disque.
- Autres options évoquées (changer la phrase de passe, gérer les partitions, effacement sécurisé, vérifier
  un disque) : non retenues pour l'instant.

## 15. Évolution du 2026-10-22 (suite) : compression et outils du gestionnaire

Décision de l'utilisateur : la **phase 9** (v1.2.0) porte la **compression et la décompression**, plus des
fonctions choisies par le planificateur ; les lecteurs et stockages distants passent en **phase 10** (v1.3.0).
- **Compression et décompression** : créer une archive ZIP depuis une sélection ; extraire ZIP, 7z, RAR
  (lecture seule : le format RAR n'est pas libre en écriture), TAR, TAR.GZ et TAR.XZ, d'une source vers
  n'importe quelle destination (téléphone, carte SD, disque), avec progression et annulation, comme une
  copie. Une archive hostile (chemins `../`, liens, bombe de décompression) ne sort jamais du dossier de
  destination et ne remplit pas le disque sans limite.
- *(décisions du planificateur)* :
  - **Corbeille** : une suppression depuis Portail déplace dans un dossier caché du même stockage
    (`.Portail-corbeille`), récupérable 30 jours, vidable à la main ; « Supprimer définitivement » reste
    possible. Sur un disque chiffré, la corbeille reste dans le volume chiffré.
  - **Analyse de l'espace** : taille de chaque dossier et liste des plus gros fichiers d'un stockage.
  - **Propriétés** : taille, dates, propriétaire et droits sur ext4, empreinte SHA-256 calculée à la demande.
  - **Renommage par lot** : numérotation, remplacement d'un texte, date de prise de vue des photos.
  - **Filtres rapides** sur l'accueil : photos, vidéos, musique, documents, archives, fichiers récents.

## 16. Évolution du 2026-10-22 (suite) : vérification et réparation

Décision de l'utilisateur : la **phase 12** (v1.5.0, phase 11 avant le 2026-10-23, voir § 19) apporte la **réparation des disques** dans
l'application, avec les outils de référence de Linux intégrés au moteur natif.
- **Vérifier d'abord, en lecture seule**, et montrer un rapport en français ; réparer seulement après
  confirmation, avec le conseil de sauvegarder les données importantes avant.
- ext4 : `e2fsck` (e2fsprogs), branché sur un gestionnaire d'entrées-sorties qui passe par l'accès USB du
  moteur ; NTFS : `ntfsfix` (ntfs-3g), qui ne remplace pas `chkdsk` de Windows ; FAT32 et exFAT :
  `fsck.fat` (dosfstools) et `fsck.exfat` (exfatprogs) ; LUKS : vérification des deux copies de l'en-tête,
  sauvegarde de l'en-tête dans un fichier choisi par l'utilisateur, restauration.
- Jamais sur un volume ouvert ; un disque réparé ne s'ouvre plus en lecture seule pour « mal éjecté » ou
  « journal à rejouer ».
- Le § 4.2 reste la règle : Portail n'écrit sur un système de fichiers douteux que par ces outils, à la
  demande explicite de l'utilisateur.

## 17. Évolution du 2026-10-22 (suite) : gestion des partitions

Décision de l'utilisateur : la **phase 13** (v1.6.0, phase 12 avant le 2026-10-23, voir § 19) apporte la **gestion des partitions** d'un disque USB,
à la manière de GParted, et le choix de la **taille** au formatage.
- **Choisir la taille** : formater une partition de la taille voulue au lieu de tout le disque ; le reste
  reste libre.
- **Voir le disque comme dans GParted** : une barre avec chaque partition et l'espace libre, à
  l'échelle ; pour chaque partition, son système de fichiers, son étiquette, sa taille et sa place
  occupée.
- **Créer** une partition dans un espace libre (taille, système de fichiers, chiffrement LUKS2,
  étiquette), **supprimer** une partition, **formater une seule partition**, changer son **étiquette**,
  créer une **nouvelle table** (MBR ou GPT).
- **Redimensionner** une partition et son système de fichiers, agrandir ou réduire :
  - ext4 par `resize2fs` (e2fsprogs, déjà intégré en phase 12) ;
  - NTFS par `ntfsresize` (ntfs-3g) ;
  - dans un volume LUKS2, le système de fichiers intérieur (la taille du segment LUKS2 suit la
    partition) ;
  - *(décision du planificateur)* FAT32 et exFAT n'ont pas d'outil de référence libre pour le
    redimensionnement : le plan de la phase étudie la question ; à défaut, Portail les refuse en le
    disant.
- **Déplacer** une partition (changer son début) *(décision du planificateur)* : seulement si le plan de
  la phase sait le rendre sûr, car un déplacement interrompu détruit les données.
- **Sûreté** :
  - comme dans GParted, les opérations sont préparées, résumées, puis appliquées après confirmation, une
    à la fois ;
  - jamais sur un volume ouvert ;
  - le système de fichiers est vérifié (outils de la phase 12) avant un redimensionnement ;
  - avertissement : un débranchement pendant un redimensionnement ou un déplacement peut rendre la
    partition illisible, sauvegardez d'abord ;
  - toutes les opérations sont jugées sur des images par les outils de référence (`sfdisk`, `sgdisk`,
    `e2fsck`, `ntfsfix`, `cryptsetup`, montage par le noyau Linux), puis essayées sur une clé USB
    sacrifiée, jamais sur le vrai disque.

## 18. Évolution du 2026-10-23 : lecteurs intégrés et dossiers distants (phase 10)

Demande de l'utilisateur (§ 12) : voir des images, écouter de la musique, éditer des fichiers texte et lire des vidéos
dans Portail ; accéder à des dossiers distants par FTP puis SFTP comme sources du gestionnaire. Phase 10, **v1.3.0**.
*(Décisions du planificateur, à valider aux essais.)*
- **Ouvrir dans Portail** : toucher un fichier ouvre le lecteur de Portail selon son extension (images → visionneuse,
  musique → lecteur de musique, vidéos → lecteur vidéo, textes → éditeur) ; les autres fichiers s'ouvrent comme avant
  dans une autre application. Un réglage « Ouvrir dans Portail » (activé par défaut) rend l'ancien comportement ;
  « Ouvrir avec… » reste dans les actions d'un fichier.
- **Visionneuse d'images** : images du dossier dans l'ordre de l'explorateur, glisser de l'une à l'autre, zoom par
  pincement et double toucher ; JPEG, PNG, GIF, WebP et BMP décodés par Flutter, HEIC et HEIF convertis par
  Android ; 64 Mio au plus par image ; partager, ouvrir avec, propriétés.
- **Lecteur de musique** : la file d'attente est faite des fichiers audio du dossier, à partir de celui touché ;
  lecture en arrière-plan avec notification et commandes (lecture, pause, précédent, suivant) ; écran « Lecture en
  cours » et barre réduite dans l'application ; arrêt propre, avec un message, si le stockage est fermé ou le
  disque débranché. Bibliothèque **Media3** d'AndroidX (ExoPlayer, MediaSession).
- **Lecteur vidéo** : écran Android plein écran (Media3) : lecture, pause, barre de progression, recul et avance de
  10 s ; sous-titres SubRip à côté de la vidéo (`film.srt`, `film.fr.srt`…), en UTF-8 ou Windows-1252, au choix
  dans le lecteur.
- **Éditeur de texte** : fichiers de 2 Mio au plus, en UTF-8 seulement (avec ou sans BOM, gardé) ; fins de ligne
  Windows (CRLF) gardées ; enregistrement par un fichier temporaire qui remplace ensuite l'original (jamais d'original
  à moitié écrit) ; si le fichier a changé depuis son ouverture, Portail demande avant d'écraser ; lecture seule si
  le stockage l'est.
- **Dossiers distants** (FTP et SFTP) : serveurs enregistrés (nom, adresse, port, utilisateur, dossier de départ),
  connectés à la demande, puis parcourus comme les autres stockages : naviguer, ouvrir (avec les lecteurs),
  partager, copier et déplacer dans les deux sens, renommer, créer, supprimer, compresser et extraire.
  - Mot de passe mémorisé seulement si l'utilisateur coche « Mémoriser », alors chiffré par une clé du Keystore
    d'Android ; sinon demandé à chaque connexion et gardé en mémoire le temps de la connexion.
  - FTP n'est pas chiffré : le formulaire le dit (« le mot de passe et les fichiers circulent en clair »).
  - SFTP : empreinte de la clé du serveur montrée et acceptée à la première connexion, puis vérifiée ; une clé
    changée est refusée avec un avertissement (on peut oublier l'ancienne dans la fiche du serveur).
    Authentification par mot de passe ou par une clé privée OpenSSH importée (Ed25519, ECDSA, RSA), chiffrée comme le
    mot de passe.
  - Pas de corbeille sur un dossier distant : la suppression y est définitive, et la boîte le dit. Liens
    symboliques affichés mais non suivis, comme sur les disques. Place libre inconnue : non vérifiée avant une copie.
  - Ce qui vient d'un disque chiffré ne part vers un serveur que par une action explicite (copie, déplacement,
    compression) ; un ZIP compressé vers un serveur y est écrit directement, sans fichier temporaire sur le
    téléphone.
- **Confidentialité (§ 11, § 12)** : images, sous-titres et textes lus en mémoire ; rien d'un disque chiffré n'est
  écrit sur le stockage du téléphone par les lecteurs.
- **Bibliothèques** *(décision du planificateur)* : Media3 1.11.1 (Apache-2.0), Apache Commons Net 3.13.0
  (Apache-2.0), JSch 2.28.8 de mwiede (BSD) avec Bouncy Castle 1.86 (licence MIT) pour Ed25519 et Curve25519 sur
  Android ; pour les tests JVM seulement, Apache FtpServer 1.2.1 et Apache MINA SSHD 2.20.0 (Apache-2.0).

## 19. Évolution du 2026-10-23 (suite) : interface complète et professionnelle

Décision de l'utilisateur : une **phase 11** (v1.4.0) améliore l'interface de Portail pour en faire une application
« vraiment complète et pro ». La réparation des disques devient la phase 12 (v1.5.0) et la gestion des partitions la
phase 13 (v1.6.0).
- Le contenu exact (charte visuelle, navigation, écrans revus, animations, thème sombre, accessibilité, tablette…) est
  arrêté avec l'utilisateur au moment d'écrire le plan de la phase, après la v1.3.0.
