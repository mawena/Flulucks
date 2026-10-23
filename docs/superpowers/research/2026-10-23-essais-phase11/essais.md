# Essais de la phase 11 (interface complète et professionnelle) sur le Pixel 4 XL

APK de la branche `feature/interface` en mode profile (compilée comme une release, même signature : vos données
sont gardées), installée avant l'essai. Le numéro affiché reste 1.3.0 jusqu'à la
v1.4.0. Cochez chaque ligne réussie et notez ce qui ne va pas sous la ligne : texte coupé ou qui se chevauche,
couleur peu lisible, bouton introuvable, animation saccadée.

**Ordre** : faites la section 1 **avant d'ouvrir Portail** pour la première fois depuis l'installation. La bienvenue
ne se montre qu'une fois.

**Disques** : pour tout ce qui écrit sur un disque USB, utilisez une **clé de test** ou un disque dont vous avez
une sauvegarde, jamais le vrai disque sans sauvegarde. Sur le vrai disque, cochez « Ouvrir en lecture seule » au
déverrouillage : cette fiche n'a pas besoin d'y écrire.

**Fichiers d'essai** : un dossier est prêt sur le téléphone, `Download/portail-tests/phase11/Essai`. Il contient
12 photos (`Photos`), une grande photo de 6 Mo (pour les copies), `notes.txt` (12 lignes, LF), `windows.txt` (CRLF),
trois morceaux de musique, un PDF, une archive `.zip`, un dossier `Vide` et un dossier `2000-fichiers`. Les
suppressions se font dans ce dossier. Une copie de cette fiche est à côté : `Download/portail-tests/phase11/essais.md`.

## 1. Premier lancement et bienvenue

- [x] Avant d'ouvrir Portail : Paramètres Android → Applications → Portail → Accès à tous les fichiers →
      désactiver.
- [x] Ouvrir Portail : l'écran de démarrage est terracotta, avec le logo blanc. Puis « Bienvenue dans Portail »
      s'affiche : le logo, trois points (« Vos disques, même chiffrés », « Votre phrase de passe ne quitte jamais
      la mémoire », « Accès aux fichiers du téléphone… »), et les boutons « Autoriser l'accès » et « Commencer ».
- [x] Toucher **Commencer** sans autoriser : l'accueil s'ouvre avec la carte « Accès aux fichiers du téléphone »
      et son bouton **Autoriser**. Les Raccourcis n'apparaissent pas, et la tuile Téléphone ne s'ouvre pas.
- [x] Toujours sans l'accès, brancher la clé de test : elle apparaît dans **Disques USB**, s'ouvre et se parcourt
      normalement. L'éjecter.
- [x] Fermer Portail (le retirer des applications récentes), puis le rouvrir : la bienvenue ne revient pas,
      l'accueil s'ouvre directement.
- [x] Toucher **Autoriser** sur la carte, activer Portail dans la page Android, puis revenir : la carte disparaît,
      les Raccourcis apparaissent et la tuile Téléphone s'ouvre.
- [x] *Facultatif, car il efface les serveurs, les favoris et les réglages de Portail* : Paramètres Android →
      Applications → Portail → Stockage et cache → **Vider le stockage**, puis retirer l'accès comme plus haut.
      Ouvrir Portail : la bienvenue revient. Toucher **Autoriser l'accès**, activer Portail, revenir : la puce
      « Autorisé » remplace le bouton. Toucher **Commencer**.

## 2. Thèmes et démarrage en sombre

- [x] Accueil → roue dentée (**Réglages**) → section **Apparence** → **Thème** : trois choix, Système, Clair et
      Sombre.
- [x] Toucher **Sombre** : toute l'application passe en sombre aussitôt, sans redémarrer. Parcourir l'accueil, un
      dossier, les Réglages et une boîte de dialogue (Renommer, par exemple) : aucun texte difficile à lire, les
      boutons principaux restent terracotta.
- [x] Toucher **Clair** : fond blanc cassé, légèrement rosé ; boutons principaux et éléments actifs terracotta.
- [x] Toucher **Système**, puis basculer le thème sombre d'Android (Paramètres → Affichage → Thème sombre) :
      Portail suit, dans les deux sens.
- [x] Démarrage en sombre : Portail sur **Système** et Android en thème sombre. Fermer Portail (applications
      récentes), puis le relancer trois fois : écran de démarrage terracotta, puis accueil sombre, **sans éclair
      blanc** entre les deux.
- [x] Portail sur **Sombre** et Android en thème clair : relancer Portail et noter si un fond clair apparaît un
      instant avant l'accueil.
- [x] Remettre le thème voulu pour la suite (Système conseillé).

## 3. Accueil et vrai disque

- [x] En-tête : logo, « Portail », la roue dentée et le menu ⋮ (Aide, À propos). Dessous, la barre « Rechercher
      dans tous les stockages ». Puis, dans l'ordre : Raccourcis (six tuiles rondes), Stockage, Disques USB,
      Dossiers distants et Favoris (s'il y en a).
- [x] Sans disque branché, **Disques USB** montre une icône USB, « Aucun disque branché » et une phrase
      d'explication.
- [x] Tuile Téléphone : place libre (« … libres sur … ») et jauge. Son menu ⋮ propose **Corbeille** et **Analyse
      de l'espace**, qui s'ouvrent.
- [x] Brancher le vrai disque : la carte montre l'icône USB, le nom, la taille et un menu ⋮ (ne pas toucher
      **Formater…**). La partition chiffrée porte un cadenas fermé et les puces du contenu (LUKS2), « Chiffré » et
      « Verrouillé », et un bouton **Déverrouiller**.
- [x] Toucher **Déverrouiller** : le dialogue a un cadenas en titre, le champ « Phrase de passe », un œil à droite
      du champ et la case « Ouvrir en lecture seule », cochée ou non selon le réglage « Ouvrir les disques en
      lecture seule ».
- [x] Taper une phrase fausse, puis **Déverrouiller** : une barre de progression défile pendant le calcul, puis
      l'erreur s'affiche dans un bandeau rouge, avec une icône.
- [x] Taper la bonne phrase, toucher l'œil : la phrase devient lisible. Toucher l'œil à nouveau : elle est
      masquée. Cocher « Ouvrir en lecture seule », puis **Déverrouiller**.
- [x] Partition ouverte : cadenas ouvert, puces « Chiffré », « Ouvert » (teinte douce) et « Lecture seule »
      (teinte d'avertissement), la ligne « Ouvert — visible dans l'application Fichiers », et les boutons
      **Parcourir** (plein) et **Éjecter** (contour). Le menu ⋮ de la partition propose Ouvrir dans Fichiers,
      Corbeille et Analyse de l'espace.
- [x] Toucher **Parcourir** : l'explorateur montre le bandeau de lecture seule, et aucun bouton rond « Nouveau
      dossier ». Dans le menu ⋮ d'un fichier (affichage en liste), **Couper**, **Renommer**, **Compresser** et
      **Supprimer** sont grisés.
- [x] Revenir à l'accueil, toucher **Éjecter** : « Écriture des données en attente… » s'affiche, puis la partition
      redevient « Verrouillé ». Garder le disque branché pour la section 6.

## 4. Explorateur

Sur le téléphone, dans `Download/portail-tests/phase11/Essai`.

- [x] Le bouton de la barre de titre (infobulle « Afficher en grille ») passe en grille : 3 colonnes en portrait,
      vignettes arrondies, noms sur deux lignes au plus. Le même bouton revient en liste.
- [x] Hors de la racine, le fil d'Ariane apparaît sous le titre : une maison, puis « Download › portail-tests ›
      phase11 › Essai », le fil défilé jusqu'à la fin. Toucher « Download » remonte directement à ce dossier, la
      maison à la racine.
- [x] En liste, toucher ⋮ à droite d'un fichier : une feuille s'ouvre, avec une poignée en haut, et propose
      Ouvrir avec…, Partager, Copier, Couper, Renommer, Compresser, Propriétés et Supprimer (en rouge).
- [x] **Renommer** depuis ce menu : le nom sans l'extension est présélectionné. Taper `a/b` : le message
      d'erreur s'affiche et **OK** reste grisé. Annuler.
- [x] Appui long sur un élément : il est coché, le titre devient « 1 sélectionné ». Le bouton **Tout
      sélectionner** coche tout le dossier (« 5 sélectionnés », par exemple). Le × de gauche annule la
      sélection.
- [x] Sélectionner un fichier, **Supprimer**, puis **Supprimer** (pas « Supprimer définitivement ») : le message
      flottant « 1 élément mis à la corbeille » propose **Annuler**. Le toucher : le fichier revient à sa place.
- [x] Recommencer en laissant passer le message : le fichier est dans la **Corbeille** (menu ⋮ de la tuile
      Téléphone), et **Restaurer** le remet.
- [x] Ouvrir `2000-fichiers` et faire défiler vite, en liste puis en grille, de haut en bas et retour : pas
      d'à-coups visibles, les lignes ne sautent pas.
- [x] Le dossier `Vide` montre une icône et « Dossier vide ». Tirer la liste vers le bas la recharge.
- [x] Passer d'un dossier à l'autre : les changements d'écran sont des fondus courts, sans saut ni éclair.

## 5. Raccourcis et recherche en grille (votre demande du 23 octobre)

Réglages → Apparence → **Affichage des dossiers** sur **Grille**.

- [x] Accueil → Raccourcis → **Photos** : les photos du téléphone s'affichent en **grille, avec leurs vignettes**,
      comme dans l'explorateur.
- [x] Toucher une photo : elle s'ouvre dans la visionneuse de Portail, avec le compteur « 3 / 12 » sous son nom.
      Le glissement passe aux autres photos du même stockage, dans l'ordre affiché.
- [x] Appui long sur une photo : une feuille propose Ouvrir le dossier, Ouvrir avec…, Partager et Propriétés.
      **Ouvrir le dossier** ouvre l'explorateur sur son dossier.
- [x] Dans Photos, le bouton de la barre de titre repasse en **liste** (lignes avec stockage, dossier et taille).
      Ouvrir ensuite un dossier du téléphone : l'explorateur est aussi en liste. Remettre la grille.
- [x] **Vidéos**, **Musique**, **Documents**, **Archives** et **Récents** suivent le même choix. Un document
      (PDF) touché s'ouvre dans une autre application, comme avant.
- [x] Dans l'explorateur, la loupe → taper un nom présent dans le dossier : les résultats suivent la grille.
      Toucher un résultat image : la visionneuse de Portail s'ouvre.
- [x] La **Corbeille** et l'**Analyse de l'espace** restent en liste, même en grille.

## 6. Recherche dans tous les stockages

Déverrouiller le vrai disque (en lecture seule) avant cette section.

- [x] Accueil → barre « Rechercher dans tous les stockages » : un écran de recherche s'ouvre, le clavier sorti,
      avec une phrase d'aide (« Tapez un nom de fichier… »).
- [x] Taper un mot présent sur le téléphone **et** sur le disque : « Recherche dans … » indique le stockage en
      cours, puis les résultats arrivent groupés, un en-tête par stockage (« Téléphone · 12 », puis le disque).
      Les dossiers distants ne sont pas fouillés.
- [x] Les résultats suivent la grille ou la liste ; le bouton de la barre de titre bascule ici aussi. Toucher une
      photo du disque : elle s'ouvre dans la visionneuse de Portail.
- [x] Modifier la saisie pendant une recherche : les anciens résultats ne s'affichent pas, seuls ceux de la
      nouvelle saisie arrivent.
- [x] Un mot introuvable : « Aucun résultat ». Un mot très courant (une lettre) : un stockage à plus de 500
      résultats affiche « Plus de 500 résultats : précisez la recherche » sous son en-tête.
- [x] Depuis l'explorateur, loupe → taper un mot → puce **Partout** : la recherche globale s'ouvre avec la même
      saisie.
- [x] Quitter la recherche pendant qu'elle tourne : retour immédiat, rien ne reste bloqué. Éjecter le disque.

## 7. Texte à 200 % et TalkBack

Android 13 limite la taille de police à 130 % environ. Pour l'essai à 200 %, sur le PC :
`adb shell settings put system font_scale 2.0` ; pour revenir à votre réglage actuel,
`adb shell settings put system font_scale 0.85`.

- [x] Paramètres Android → Accessibilité → Taille d'affichage et texte → **Taille de la police** au maximum.
      Accueil, explorateur, Réglages, bienvenue (si refaite) : rien de coupé ni de superposé.
- [x] Texte à 200 % : accueil, explorateur en liste et en grille, Réglages, À propos, Aide. Les textes passent à
      la ligne ; les écrans défilent ; aucun texte n'est tronqué au point d'être illisible.
- [x] À 200 %, les actions vitales restent visibles et touchables, au besoin après un défilement :
      **Déverrouiller** (dialogue de déverrouillage, avec la clé de test chiffrée ou le vrai disque),
      **Éjecter**, **Annuler** sur la barre d'une copie en cours, **Enregistrer** dans l'éditeur de texte.
- [x] Remettre la taille de police habituelle.
- [x] TalkBack (Paramètres → Accessibilité → TalkBack), sur l'accueil, en glissant d'élément en élément : le logo
      n'est pas lu ; la barre de recherche est annoncée « Rechercher dans tous les stockages », comme un bouton ;
      chaque raccourci est lu par son nom ; la jauge du téléphone annonce « … % utilisés » ; les puces d'une
      partition (Chiffré, Verrouillé…) sont lues.
- [x] TalkBack dans l'explorateur en grille : chaque tuile est lue « nom, type, taille » (« vacances.jpg, image,
      3,2 Mo »), et « sélectionné » quand elle l'est. En liste, le bouton ⋮ est annoncé « Actions de … ».
- [x] TalkBack sur le lecteur réduit : « Lecture en cours : titre ». Sur l'écran Lecture en cours, le curseur est
      annoncé « Position 1:23 sur 4:05 ».
- [x] Couper TalkBack.

## 8. Paysage

Rotation automatique activée.

- [x] Accueil en paysage : deux colonnes, Raccourcis, Stockage et Favoris à gauche ; Disques USB et Dossiers
      distants à droite. Les raccourcis tiennent sur une seule ligne de six tuiles.
- [x] Explorateur en grille : davantage de colonnes qu'en portrait, vignettes nettes. Retour en portrait :
      3 colonnes.
- [x] Dialogue de déverrouillage en paysage, clavier ouvert : le champ, la case et les boutons restent
      accessibles en faisant défiler le dialogue.
- [x] Réglages, À propos, formulaire d'un serveur : le contenu reste centré dans une largeur lisible, sans lignes
      d'un bord à l'autre de l'écran.

## 9. Retours haptiques

Le retour haptique d'Android doit être actif (Paramètres → Son et vibration → Vibration et retour haptique).

- [x] Réglages → **Confort** → **Retours haptiques** activé (par défaut). Appui long sur un fichier : petit clic.
      **Tout sélectionner** : petit clic.
- [x] Déverrouillage réussi : vibration légère. Phrase fausse : vibration plus marquée.
- [x] Copie terminée (un fichier de quelques Mo, du téléphone vers la clé de test) : vibration légère.
- [x] **Supprimer définitivement**, ou **Vider la corbeille**, une fois confirmé : vibration marquée.
- [x] Désactiver **Retours haptiques**, puis refaire les quatre lignes ci-dessus : plus aucune vibration de
      Portail.
- [x] Réactiver le réglage.

## 10. À propos, licences et aide

- [x] Accueil → ⋮ → **À propos** (ou Réglages → section Portail → **À propos**) : logo, « Portail », « Version
      1.3.0 (10) », « Android 13 · Pixel 4 XL », la phrase « Disques chiffrés LUKS et gestionnaire de fichiers,
      sans root », le bouton « Licences des bibliothèques » et la note sur la GPL.
- [x] **Licences des bibliothèques** : la page « Licences » liste lwext4, ntfs-3g, FatFs, libarchive, XZ Utils,
      Argon2, cJSON, Mbed TLS, Media3, Apache Commons Net, JSch et Bouncy Castle, parmi les bibliothèques de
      Flutter. Toucher **ntfs-3g** : le texte de la GPL s'affiche en entier.
- [x] Accueil → ⋮ → **Aide** : huit questions, de « Ouvrir un disque chiffré » à « Formater un disque ». Chacune
      se déplie en une réponse courte et juste, et se replie.
- [x] Réglages : cinq sections dans l'ordre, Apparence, Fichiers, Disques, Confort et Portail. Chaque réglage
      d'avant (lecture seule, verrouillage automatique, Ouvrir dans Portail, accès aux fichiers) est présent.

## 11. Musique, lecteurs et dossiers distants

- [x] Toucher un morceau de `Essai` : le lecteur réduit apparaît en bas, sur une surface commune avec un
      trait au-dessus. Il reste en bas de l'accueil, de l'explorateur et des Réglages.
- [x] Sélectionner un fichier → **Copier** : la barre « 1 élément à copier » (Annuler, Coller ici) s'affiche
      **au-dessus** du lecteur réduit. Coller sur la clé de test : la barre de la copie (« Copie de … — 1 sur 1 ·
      34 % », Annuler) s'affiche **sous** le lecteur.
- [x] Toucher le lecteur réduit : l'écran **Lecture en cours** montre une pochette de remplacement (icône sur fond
      teinté, coins arrondis) et les temps, dont les chiffres ne tremblent pas pendant la lecture.
- [x] Ouvrir `notes.txt` : en bas, la ligne d'état « UTF-8 · LF · 12 lignes ». `windows.txt` : « CRLF ». Ouvrir
      une photo de `Photos` : fond noir, et compteur « 3 / 12 » sous le nom, en haut.
- [x] Accueil → **Dossiers distants** → **Ajouter un serveur** → **FTP** : le bandeau d'avertissement « FTP n'est
      pas chiffré… » s'affiche. Port `0`, puis `70000` : « Port de 1 à 65535 ». Adresse vide : « Saisissez une
      adresse ». **Enregistrer** ne passe pas tant que ces champs sont faux. Annuler sans enregistrer.
- [x] Serveur SFTP déjà approuvé (celui de la phase 10) : crayon de sa tuile (**Modifier**). L'empreinte du serveur
      s'affiche en police à chasse fixe, avec un bouton **Copier l'empreinte** ; le toucher affiche « Empreinte
      copiée ».
- [x] Toucher le serveur, parcourir un dossier en grille et en liste, puis **Déconnecter** : rien ne change par
      rapport à la v1.3.0, sinon l'apparence.

## 12. Choix à confirmer

Le plan a fait ces choix en votre absence ; ils se jugent à l'usage. Cochez ceux qui vous conviennent, notez les
autres.

- [x] Couleurs terracotta du logo partout, plutôt que les couleurs tirées du fond d'écran.
- [x] Police Roboto du système, sans police embarquée.
- [x] Un accueil unique avec la recherche globale, sans barre de navigation en bas.
- [x] Bienvenue montrée une seule fois, y compris après la mise à jour depuis la v1.3.0.
- [x] Menu ⋮ sur chaque élément (en liste) et « Annuler » après une mise à la corbeille.
- [x] Case « Ouvrir en lecture seule » dans le dialogue de déverrouillage.
- [x] Dossiers distants exclus de la recherche globale.
- [x] Corbeille et analyse de l'espace gardées en liste, même en grille.
- [x] Écran de démarrage terracotta plein, en clair comme en sombre.
- [x] Retours haptiques activés par défaut.
