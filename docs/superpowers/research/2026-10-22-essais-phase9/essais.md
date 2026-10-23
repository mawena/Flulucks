# Essais de la phase 9 (compression et outils du gestionnaire) sur le Pixel 4 XL

APK de la branche `feature/compression`, installée avant l'essai (le numéro affiché reste 1.1.0 jusqu'à la
v1.2.0). Cochez chaque ligne faite ; notez ce qui ne va pas sous la ligne.

**Disques** : pour tout ce qui écrit sur un disque USB, utilisez une **clé de test** ou un disque dont vous avez
une sauvegarde, jamais le vrai disque sans sauvegarde.

## 1. Archives

Préparer sur le PC, dans un dossier copié ensuite dans `Download` du téléphone : un ZIP fait par Windows (« Envoyer
vers → Dossier compressé », avec un nom accentué), un `.7z` (7-Zip), un `.rar` récent (WinRAR 5 ou plus) et un
`.tar.xz` (`tar -cJf essai.tar.xz dossier/` sous Ubuntu).

- [x] Toucher chaque archive dans l'explorateur : la feuille propose **Extraire ici**, **Extraire vers…** et
      **Ouvrir avec…**.
- [x] **Extraire ici** sur les quatre archives : un dossier au nom de l'archive apparaît, la barre affiche
      « Extraction : … » puis « Terminé » ; les fichiers s'ouvrent, les noms accentués sont justes.
- [x] **Extraire vers…** une archive du téléphone, puis ouvrir un dossier de la clé de test et toucher
      **Extraire ici** dans la barre : l'archive est extraite sur la clé.
- [x] Dans l'autre sens : une archive copiée sur la clé, extraite vers le téléphone.
- [x] Sélectionner un dossier de photos et un fichier texte, **⋮ → Compresser** : le nom proposé est
      « Archive » ; le ZIP apparaît. Le copier sur le PC : il s'ouvre sous **Windows** (noms accentués justes)
      et sous **Ubuntu** (`unzip -t archive.zip` : « No errors detected »).
- [x] Annuler une extraction ou une compression d'un gros dossier en cours : aucun fichier
      « .portail-partiel » ne reste.
- [x] Une archive protégée par mot de passe (ZIP chiffré) : « Archive protégée par mot de passe : non prise
      en charge », rien n'est extrait.

## 2. Corbeille

- [x] Supprimer un fichier du téléphone : la boîte dit que les éléments restent 30 jours dans la corbeille ;
      **Supprimer** : le fichier disparaît du dossier.
- [x] Accueil → bouton **Corbeille** de la carte du téléphone : le fichier y est, avec son emplacement
      d'origine et la date ; **Restaurer** : il revient à sa place (dossier d'origine recréé s'il a été
      supprimé entre-temps).
- [x] Même chose sur la clé de test (bouton **Corbeille** de la tuile du volume ouvert, ou menu de
      l'explorateur à la racine).
- [x] **Supprimer définitivement** depuis la boîte de suppression : le fichier ne va pas dans la corbeille.
- [x] **Vider la corbeille** : après confirmation, elle est vide.
- [x] Supprimer un fichier depuis l'application **Fichiers** d'Android sur un disque ouvert : la suppression
      reste définitive (comportement voulu).

## 3. Analyse de l'espace, propriétés, renommage, filtres

- [x] Explorateur du téléphone, racine, menu **Trier par → Analyse de l'espace** : les dossiers sont classés
      du plus gros au plus petit, avec barres et pourcentages ; toucher un dossier y descend, Retour remonte ;
      l'onglet **Plus gros fichiers** liste les plus gros.
- [x] Sélectionner un fichier, **⋮ → Propriétés** : taille, date, emplacement ; sur la clé en ext4 : droits
      (`rw-r--r-- (0644)`) et propriétaire (`uid 1000, gid 1000`).
- [x] **Calculer l'empreinte SHA-256** d'un gros fichier (une vidéo) : progression, puis l'empreinte ;
      la comparer à `sha256sum` sur le PC (même fichier) : identiques.
- [x] Propriétés d'un dossier : sa taille totale et le nombre de fichiers et de dossiers.
- [x] Sélectionner plusieurs photos de l'appareil photo, **⋮ → Renommer** : mode **Date de prise de vue** ;
      l'aperçu montre « AAAA-MM-JJ HH.MM.SS.jpg » ; **Renommer** : les photos sont renommées.
- [x] Même écran, mode **Numéroter** avec « Vacances » : « Vacances (01).jpg », « Vacances (02).jpg »… ; un
      nom déjà pris par un autre fichier est signalé en rouge et **Renommer** reste grisé.
- [x] Accueil, **Raccourcis** : Photos, Vidéos, Musique, Documents, Archives, Récents listent les fichiers du
      téléphone (et de la carte SD), du plus récent ; toucher un fichier l'ouvre, l'icône de dossier ouvre
      l'explorateur à son emplacement.
