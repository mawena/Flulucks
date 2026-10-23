# Essais de la phase 11 (interface complète et professionnelle) sur le Pixel 4 XL

APK release de la branche `feature/interface`, installée avant l'essai. Le numéro affiché reste 1.3.0 jusqu'à la
v1.4.0. Cochez chaque ligne réussie et notez ce qui ne va pas sous la ligne : texte coupé ou qui se chevauche,
couleur peu lisible, bouton introuvable, animation saccadée.

**Ordre** : faites la section 1 **avant d'ouvrir Portail** pour la première fois depuis l'installation. La bienvenue
ne se montre qu'une fois.

**Disques** : pour tout ce qui écrit sur un disque USB, utilisez une **clé de test** ou un disque dont vous avez
une sauvegarde, jamais le vrai disque sans sauvegarde. Sur le vrai disque, cochez « Ouvrir en lecture seule » au
déverrouillage : cette fiche n'a pas besoin d'y écrire.

**Fichiers d'essai** : les suppressions se font sur le téléphone, dans un dossier `Download/Essai` créé pour
l'occasion (bouton rond « Nouveau dossier » en bas de l'explorateur). Mettez-y quelques photos, un `.txt` et deux
ou trois morceaux de musique.

## 1. Premier lancement et bienvenue

- [ ] Avant d'ouvrir Portail : Paramètres Android → Applications → Portail → Accès à tous les fichiers →
      désactiver.
- [ ] Ouvrir Portail : l'écran de démarrage est terracotta, avec le logo blanc. Puis « Bienvenue dans Portail »
      s'affiche : le logo, trois points (« Vos disques, même chiffrés », « Votre phrase de passe ne quitte jamais
      la mémoire », « Accès aux fichiers du téléphone… »), et les boutons « Autoriser l'accès » et « Commencer ».
- [ ] Toucher **Commencer** sans autoriser : l'accueil s'ouvre avec la carte « Accès aux fichiers du téléphone »
      et son bouton **Autoriser**. Les Raccourcis n'apparaissent pas, et la tuile Téléphone ne s'ouvre pas.
- [ ] Toujours sans l'accès, brancher la clé de test : elle apparaît dans **Disques USB**, s'ouvre et se parcourt
      normalement. L'éjecter.
- [ ] Fermer Portail (le retirer des applications récentes), puis le rouvrir : la bienvenue ne revient pas,
      l'accueil s'ouvre directement.
- [ ] Toucher **Autoriser** sur la carte, activer Portail dans la page Android, puis revenir : la carte disparaît,
      les Raccourcis apparaissent et la tuile Téléphone s'ouvre.
- [ ] *Facultatif, car il efface les serveurs, les favoris et les réglages de Portail* : Paramètres Android →
      Applications → Portail → Stockage et cache → **Vider le stockage**, puis retirer l'accès comme plus haut.
      Ouvrir Portail : la bienvenue revient. Toucher **Autoriser l'accès**, activer Portail, revenir : la puce
      « Autorisé » remplace le bouton. Toucher **Commencer**.

## 2. Thèmes et démarrage en sombre

- [ ] Accueil → roue dentée (**Réglages**) → section **Apparence** → **Thème** : trois choix, Système, Clair et
      Sombre.
- [ ] Toucher **Sombre** : toute l'application passe en sombre aussitôt, sans redémarrer. Parcourir l'accueil, un
      dossier, les Réglages et une boîte de dialogue (Renommer, par exemple) : aucun texte difficile à lire, les
      boutons principaux restent terracotta.
- [ ] Toucher **Clair** : fond blanc cassé, légèrement rosé ; boutons principaux et éléments actifs terracotta.
- [ ] Toucher **Système**, puis basculer le thème sombre d'Android (Paramètres → Affichage → Thème sombre) :
      Portail suit, dans les deux sens.
- [ ] Démarrage en sombre : Portail sur **Système** et Android en thème sombre. Fermer Portail (applications
      récentes), puis le relancer trois fois : écran de démarrage terracotta, puis accueil sombre, **sans éclair
      blanc** entre les deux.
- [ ] Portail sur **Sombre** et Android en thème clair : relancer Portail et noter si un fond clair apparaît un
      instant avant l'accueil.
- [ ] Remettre le thème voulu pour la suite (Système conseillé).

## 3. Accueil et vrai disque

- [ ] En-tête : logo, « Portail », la roue dentée et le menu ⋮ (Aide, À propos). Dessous, la barre « Rechercher
      dans tous les stockages ». Puis, dans l'ordre : Raccourcis (six tuiles rondes), Stockage, Disques USB,
      Dossiers distants et Favoris (s'il y en a).
- [ ] Sans disque branché, **Disques USB** montre une icône USB, « Aucun disque branché » et une phrase
      d'explication.
- [ ] Tuile Téléphone : place libre (« … libres sur … ») et jauge. Son menu ⋮ propose **Corbeille** et **Analyse
      de l'espace**, qui s'ouvrent.
- [ ] Brancher le vrai disque : la carte montre l'icône USB, le nom, la taille et un menu ⋮ (ne pas toucher
      **Formater…**). La partition chiffrée porte un cadenas fermé et les puces du contenu (LUKS2), « Chiffré » et
      « Verrouillé », et un bouton **Déverrouiller**.
- [ ] Toucher **Déverrouiller** : le dialogue a un cadenas en titre, le champ « Phrase de passe », un œil à droite
      du champ et la case « Ouvrir en lecture seule », cochée ou non selon le réglage « Ouvrir les disques en
      lecture seule ».
- [ ] Taper une phrase fausse, puis **Déverrouiller** : une barre de progression défile pendant le calcul, puis
      l'erreur s'affiche dans un bandeau rouge, avec une icône.
- [ ] Taper la bonne phrase, toucher l'œil : la phrase devient lisible. Toucher l'œil à nouveau : elle est
      masquée. Cocher « Ouvrir en lecture seule », puis **Déverrouiller**.
- [ ] Partition ouverte : cadenas ouvert, puces « Chiffré », « Ouvert » (teinte douce) et « Lecture seule »
      (teinte d'avertissement), la ligne « Ouvert — visible dans l'application Fichiers », et les boutons
      **Parcourir** (plein) et **Éjecter** (contour). Le menu ⋮ de la partition propose Ouvrir dans Fichiers,
      Corbeille et Analyse de l'espace.
- [ ] Toucher **Parcourir** : l'explorateur montre le bandeau de lecture seule, et aucun bouton rond « Nouveau
      dossier ». Dans le menu ⋮ d'un fichier (affichage en liste), **Couper**, **Renommer**, **Compresser** et
      **Supprimer** sont grisés.
- [ ] Revenir à l'accueil, toucher **Éjecter** : « Écriture des données en attente… » s'affiche, puis la partition
      redevient « Verrouillé ». Garder le disque branché pour la section 6.

## 4. Explorateur

Sur le téléphone, dans `Download/Essai`.

- [ ] Le bouton de la barre de titre (infobulle « Afficher en grille ») passe en grille : 3 colonnes en portrait,
      vignettes arrondies, noms sur deux lignes au plus. Le même bouton revient en liste.
- [ ] Hors de la racine, le fil d'Ariane apparaît sous le titre : une maison, puis « Download › Essai ». Toucher
      « Download » remonte directement à ce dossier, la maison à la racine. Sur un chemin long, le fil montre la
      fin.
- [ ] En liste, toucher ⋮ à droite d'un fichier : une feuille s'ouvre, avec une poignée en haut, et propose
      Ouvrir avec…, Partager, Copier, Couper, Renommer, Compresser, Propriétés et Supprimer (en rouge).
- [ ] **Renommer** depuis ce menu : le nom sans l'extension est présélectionné. Taper `a/b` : le message
      d'erreur s'affiche et **OK** reste grisé. Annuler.
- [ ] Appui long sur un élément : il est coché, le titre devient « 1 sélectionné ». Le bouton **Tout
      sélectionner** coche tout le dossier (« 5 sélectionnés », par exemple). Le × de gauche annule la
      sélection.
- [ ] Sélectionner un fichier, **Supprimer**, puis **Supprimer** (pas « Supprimer définitivement ») : le message
      flottant « 1 élément mis à la corbeille » propose **Annuler**. Le toucher : le fichier revient à sa place.
- [ ] Recommencer en laissant passer le message : le fichier est dans la **Corbeille** (menu ⋮ de la tuile
      Téléphone), et **Restaurer** le remet.
- [ ] Un dossier vide montre une icône et « Dossier vide ». Tirer la liste vers le bas la recharge.
- [ ] Passer d'un dossier à l'autre : les changements d'écran sont des fondus courts, sans saut ni éclair.

## 5. Raccourcis et recherche en grille (votre demande du 23 octobre)

Réglages → Apparence → **Affichage des dossiers** sur **Grille**.

- [ ] Accueil → Raccourcis → **Photos** : les photos du téléphone s'affichent en **grille, avec leurs vignettes**,
      comme dans l'explorateur.
- [ ] Toucher une photo : elle s'ouvre dans la visionneuse de Portail, avec le compteur « 3 / 12 » sous son nom.
      Le glissement passe aux autres photos du même stockage, dans l'ordre affiché.
- [ ] Appui long sur une photo : une feuille propose Ouvrir le dossier, Ouvrir avec…, Partager et Propriétés.
      **Ouvrir le dossier** ouvre l'explorateur sur son dossier.
- [ ] Dans Photos, le bouton de la barre de titre repasse en **liste** (lignes avec stockage, dossier et taille).
      Ouvrir ensuite un dossier du téléphone : l'explorateur est aussi en liste. Remettre la grille.
- [ ] **Vidéos**, **Musique**, **Documents**, **Archives** et **Récents** suivent le même choix. Un document
      (PDF) touché s'ouvre dans une autre application, comme avant.
- [ ] Dans l'explorateur, la loupe → taper un nom présent dans le dossier : les résultats suivent la grille.
      Toucher un résultat image : la visionneuse de Portail s'ouvre.
- [ ] La **Corbeille** et l'**Analyse de l'espace** restent en liste, même en grille.

## 6. Recherche dans tous les stockages

Déverrouiller le vrai disque (en lecture seule) avant cette section.

- [ ] Accueil → barre « Rechercher dans tous les stockages » : un écran de recherche s'ouvre, le clavier sorti,
      avec une phrase d'aide (« Tapez un nom de fichier… »).
- [ ] Taper un mot présent sur le téléphone **et** sur le disque : « Recherche dans … » indique le stockage en
      cours, puis les résultats arrivent groupés, un en-tête par stockage (« Téléphone · 12 », puis le disque).
      Les dossiers distants ne sont pas fouillés.
- [ ] Les résultats suivent la grille ou la liste ; le bouton de la barre de titre bascule ici aussi. Toucher une
      photo du disque : elle s'ouvre dans la visionneuse de Portail.
- [ ] Modifier la saisie pendant une recherche : les anciens résultats ne s'affichent pas, seuls ceux de la
      nouvelle saisie arrivent.
- [ ] Un mot introuvable : « Aucun résultat ». Un mot très courant (une lettre) : un stockage à plus de 500
      résultats affiche « Plus de 500 résultats : précisez la recherche » sous son en-tête.
- [ ] Depuis l'explorateur, loupe → taper un mot → puce **Partout** : la recherche globale s'ouvre avec la même
      saisie.
- [ ] Quitter la recherche pendant qu'elle tourne : retour immédiat, rien ne reste bloqué. Éjecter le disque.

## 7. Texte à 200 % et TalkBack

Android 13 limite la taille de police à 130 % environ. Pour l'essai à 200 %, sur le PC :
`adb shell settings put system font_scale 2.0` ; pour revenir, `adb shell settings put system font_scale 1.0`
(ou la valeur d'avant, lue par `adb shell settings get system font_scale`).

- [ ] Paramètres Android → Accessibilité → Taille d'affichage et texte → **Taille de la police** au maximum.
      Accueil, explorateur, Réglages, bienvenue (si refaite) : rien de coupé ni de superposé.
- [ ] Texte à 200 % : accueil, explorateur en liste et en grille, Réglages, À propos, Aide. Les textes passent à
      la ligne ; les écrans défilent ; aucun texte n'est tronqué au point d'être illisible.
- [ ] À 200 %, les actions vitales restent visibles et touchables, au besoin après un défilement :
      **Déverrouiller** (dialogue de déverrouillage, avec la clé de test chiffrée ou le vrai disque),
      **Éjecter**, **Annuler** sur la barre d'une copie en cours, **Enregistrer** dans l'éditeur de texte.
- [ ] Remettre la taille de police habituelle.
- [ ] TalkBack (Paramètres → Accessibilité → TalkBack), sur l'accueil, en glissant d'élément en élément : le logo
      n'est pas lu ; la barre de recherche est annoncée « Rechercher dans tous les stockages », comme un bouton ;
      chaque raccourci est lu par son nom ; la jauge du téléphone annonce « … % utilisés » ; les puces d'une
      partition (Chiffré, Verrouillé…) sont lues.
- [ ] TalkBack dans l'explorateur en grille : chaque tuile est lue « nom, type, taille » (« vacances.jpg, image,
      3,2 Mo »), et « sélectionné » quand elle l'est. En liste, le bouton ⋮ est annoncé « Actions de … ».
- [ ] TalkBack sur le lecteur réduit : « Lecture en cours : titre ». Sur l'écran Lecture en cours, le curseur est
      annoncé « Position 1:23 sur 4:05 ».
- [ ] Couper TalkBack.

## 8. Paysage

Rotation automatique activée.

- [ ] Accueil en paysage : deux colonnes, Raccourcis, Stockage et Favoris à gauche ; Disques USB et Dossiers
      distants à droite. Les raccourcis tiennent sur une seule ligne de six tuiles.
- [ ] Explorateur en grille : davantage de colonnes qu'en portrait, vignettes nettes. Retour en portrait :
      3 colonnes.
- [ ] Dialogue de déverrouillage en paysage, clavier ouvert : le champ, la case et les boutons restent
      accessibles en faisant défiler le dialogue.
- [ ] Réglages, À propos, formulaire d'un serveur : le contenu reste centré dans une largeur lisible, sans lignes
      d'un bord à l'autre de l'écran.

## 9. Retours haptiques

Le retour haptique d'Android doit être actif (Paramètres → Son et vibration → Vibration et retour haptique).

- [ ] Réglages → **Confort** → **Retours haptiques** activé (par défaut). Appui long sur un fichier : petit clic.
      **Tout sélectionner** : petit clic.
- [ ] Déverrouillage réussi : vibration légère. Phrase fausse : vibration plus marquée.
- [ ] Copie terminée (un fichier de quelques Mo, du téléphone vers la clé de test) : vibration légère.
- [ ] **Supprimer définitivement**, ou **Vider la corbeille**, une fois confirmé : vibration marquée.
- [ ] Désactiver **Retours haptiques**, puis refaire les quatre lignes ci-dessus : plus aucune vibration de
      Portail.
- [ ] Réactiver le réglage.

## 10. À propos, licences et aide

- [ ] Accueil → ⋮ → **À propos** (ou Réglages → section Portail → **À propos**) : logo, « Portail », « Version
      1.3.0 (10) », « Android 13 · Pixel 4 XL », la phrase « Disques chiffrés LUKS et gestionnaire de fichiers,
      sans root », le bouton « Licences des bibliothèques » et la note sur la GPL.
- [ ] **Licences des bibliothèques** : la page « Licences » liste lwext4, ntfs-3g, FatFs, libarchive, XZ Utils,
      Argon2, cJSON, Mbed TLS, Media3, Apache Commons Net, JSch et Bouncy Castle, parmi les bibliothèques de
      Flutter. Toucher **ntfs-3g** : le texte de la GPL s'affiche en entier.
- [ ] Accueil → ⋮ → **Aide** : huit questions, de « Ouvrir un disque chiffré » à « Formater un disque ». Chacune
      se déplie en une réponse courte et juste, et se replie.
- [ ] Réglages : cinq sections dans l'ordre, Apparence, Fichiers, Disques, Confort et Portail. Chaque réglage
      d'avant (lecture seule, verrouillage automatique, Ouvrir dans Portail, accès aux fichiers) est présent.

## 11. Musique, lecteurs et dossiers distants

- [ ] Toucher un morceau de `Download/Essai` : le lecteur réduit apparaît en bas, sur une surface commune avec un
      trait au-dessus. Il reste en bas de l'accueil, de l'explorateur et des Réglages.
- [ ] Sélectionner un fichier → **Copier** : la barre « 1 élément à copier » (Annuler, Coller ici) s'affiche
      **au-dessus** du lecteur réduit. Coller sur la clé de test : la barre de la copie (« Copie de … — 1 sur 1 ·
      34 % », Annuler) s'affiche **sous** le lecteur.
- [ ] Toucher le lecteur réduit : l'écran **Lecture en cours** montre une pochette de remplacement (icône sur fond
      teinté, coins arrondis) et les temps, dont les chiffres ne tremblent pas pendant la lecture.
- [ ] Ouvrir le `.txt` : en bas, la ligne d'état « UTF-8 · LF · 12 lignes » (ou CRLF pour un fichier de
      Windows). Ouvrir une photo : fond noir, et compteur « 3 / 5 » sous le nom, en haut.
- [ ] Accueil → **Dossiers distants** → **Ajouter un serveur** → **FTP** : le bandeau d'avertissement « FTP n'est
      pas chiffré… » s'affiche. Port `0`, puis `70000` : « Port de 1 à 65535 ». Adresse vide : « Saisissez une
      adresse ». **Enregistrer** ne passe pas tant que ces champs sont faux. Annuler sans enregistrer.
- [ ] Serveur SFTP déjà approuvé (celui de la phase 10) : crayon de sa tuile (**Modifier**). L'empreinte du serveur
      s'affiche en police à chasse fixe, avec un bouton **Copier l'empreinte** ; le toucher affiche « Empreinte
      copiée ».
- [ ] Toucher le serveur, parcourir un dossier en grille et en liste, puis **Déconnecter** : rien ne change par
      rapport à la v1.3.0, sinon l'apparence.

## 12. Choix à confirmer

Le plan a fait ces choix en votre absence ; ils se jugent à l'usage. Cochez ceux qui vous conviennent, notez les
autres.

- [ ] Couleurs terracotta du logo partout, plutôt que les couleurs tirées du fond d'écran.
- [ ] Police Roboto du système, sans police embarquée.
- [ ] Un accueil unique avec la recherche globale, sans barre de navigation en bas.
- [ ] Bienvenue montrée une seule fois, y compris après la mise à jour depuis la v1.3.0.
- [ ] Menu ⋮ sur chaque élément (en liste) et « Annuler » après une mise à la corbeille.
- [ ] Case « Ouvrir en lecture seule » dans le dialogue de déverrouillage.
- [ ] Dossiers distants exclus de la recherche globale.
- [ ] Corbeille et analyse de l'espace gardées en liste, même en grille.
- [ ] Écran de démarrage terracotta plein, en clair comme en sombre.
- [ ] Retours haptiques activés par défaut.
