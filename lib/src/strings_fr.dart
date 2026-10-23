/// Chaînes de l'interface (français). Les messages d'erreur du moteur arrivent déjà en français.
class Fr {
  Fr._();

  static const title = 'Portail';
  static const emptyList = 'Branchez un disque USB-C';
  static const grantAccess = "Autoriser l'accès";
  static const unlock = 'Déverrouiller';
  static const open = 'Ouvrir';
  static const openInFiles = 'Ouvrir dans Fichiers';
  static const eject = 'Éjecter';
  static const openedInFiles = "Ouvert — visible dans l'application Fichiers";
  static const openedInFilesRw = "Ouvert en lecture et écriture — visible dans l'application Fichiers";
  static const readOnlySwitch = 'Ouvrir les disques en lecture seule';
  static const ejecting = 'Écriture des données en attente…';
  static const needsRecovery =
      "Le volume n'a pas été démonté proprement : contenu possiblement incomplet";
  static const passphrase = 'Phrase de passe';
  static const unlocking =
      'Déverrouillage en cours… cela peut prendre quelques secondes';
  static const cancel = 'Annuler';
  static const scanning = 'Analyse du disque…';
  static const genericError = "L'opération a échoué";

  // --- gestionnaire de fichiers
  static const phone = 'Téléphone';
  static const sdCard = 'Carte SD';
  static const storage = 'Stockage';
  static const usbDisks = 'Disques USB';
  static const favorites = 'Favoris';
  static const allFilesTitle = 'Accès aux fichiers du téléphone';
  static const allFilesBody =
      'Autorisez Portail à gérer tous les fichiers pour parcourir le téléphone et la carte SD.';
  static const allFilesButton = 'Autoriser';
  static const allFilesGranted = 'Autorisé';
  static const browse = 'Parcourir';
  static const settings = 'Réglages';
  static const readOnlyHelp = "S'applique aux prochaines ouvertures de disques";
  static const autoLock = 'Verrouillage automatique';
  static const autoLockHelp = 'Disques chiffrés, après ce délai sans activité';
  static const never = 'Jamais';

  /// « 15 min », « 1 h ».
  static String minutes(int m) => m == 60 ? '1 h' : '$m min';
  static const newFolder = 'Nouveau dossier';
  static const folderName = 'Nom du dossier';
  static const rename = 'Renommer';
  static const newName = 'Nouveau nom';
  static const delete = 'Supprimer';
  static const deleteBody = 'La suppression est définitive.';
  static const copy = 'Copier';
  static const cut = 'Couper';
  static const pasteHere = 'Coller ici';
  static const compress = 'Compresser';
  static const archiveName = "Nom de l'archive";
  static const archiveDefault = 'Archive';
  static const trash = 'Corbeille';
  static const trashEmpty = 'La corbeille est vide';
  static const emptyTrash = 'Vider la corbeille';
  static const emptyTrashTitle = 'Vider la corbeille ?';
  static const emptyAction = 'Vider';
  static const restore = 'Restaurer';
  static const deletePermanently = 'Supprimer définitivement';
  static const deleteToTrashBody = 'Les éléments restent 30 jours dans la corbeille de ce stockage.';
  static const trashNote = 'Les éléments supprimés depuis plus de 30 jours sont effacés.';
  static const spaceAnalysis = "Analyse de l'espace";
  static const spaceFolders = 'Dossiers';
  static const biggestFiles = 'Plus gros fichiers';
  static const analyzing = 'Analyse en cours…';
  static const spaceTruncated = "Analyse incomplète : arrêtée, ou trop d'éléments";
  static const extractHere = 'Extraire ici';
  static const extractTo = 'Extraire vers…';
  static const share = 'Partager';
  static const openWith = 'Ouvrir avec…';
  static const emptyFolder = 'Dossier vide';
  static const protectedFolder = 'Dossier protégé par Android';
  static const volumeClosed = 'Le disque a été fermé';
  static const sortBy = 'Trier par';
  static const sortName = 'Nom';
  static const sortSize = 'Taille';
  static const sortDate = 'Date';
  static const ascending = 'Croissant';
  static const descending = 'Décroissant';
  static const display = 'Affichage des dossiers';
  static const grid = 'Grille';
  static const list = 'Liste';
  static const showHidden = 'Afficher les fichiers cachés';
  static const addFavorite = 'Ajouter aux favoris';
  static const add = 'Ajouter';
  static const encryptedFavorite = "Le nom de ce dossier restera visible sur l'accueil, même disque verrouillé";
  static const removeFavorite = 'Retirer des favoris';
  static const favoriteUnavailable = 'Disque non ouvert';
  static const search = 'Rechercher';
  static const searchHint = 'Nom du fichier';
  static const noResult = 'Aucun résultat';
  static const tooManyResults = 'Plus de 500 résultats : précisez la recherche';
  static const transferCancelled = 'Opération annulée';
  static const ok = 'OK';

  static String freeOf(int free, int total) => '${humanSize(free)} libres sur ${humanSize(total)}';
  static String deleteOne(String name) => 'Supprimer « $name » ?';
  static String deleteMany(int n) => 'Supprimer $n éléments ?';
  static String clipboard(int n, bool move) => '$n élément(s) ${move ? 'à déplacer' : 'à copier'}';
  static String selected(int n) => '$n sélectionné(s)';
  /// [kind] : `copy`, `move`, `compress` ou `extract`.
  static String transferLine(String kind, String name, int i, int n, int pct) {
    final what = switch (kind) {
      'move' => 'Déplacement',
      'compress' => 'Compression',
      'extract' => 'Extraction',
      _ => 'Copie',
    };
    return '$what : $name ($i/$n) — $pct %';
  }

  static String toExtract(String name) => '« $name » à extraire';
  static String trashSummary(int n, int bytes) => '$n élément(s), ${humanSize(bytes)}';
  static String emptyTrashBody(int n) => 'Les $n élément(s) seront supprimés définitivement.';
  static String actionsOf(String name) => 'Actions de $name';
  static String spaceTotal(int bytes, int files) => '${humanSize(bytes)} — $files fichier(s)';
  static String spaceOthers(int n) => '… et $n autre(s) élément(s)';
  static String percent(int p) => '$p %';
  static String transferDone(int n) => 'Terminé : $n fichier(s)';

  /// « Terminé : 3 fichier(s), 34 Mo/s » ; sans débit sous 1 s ou sous 1 Mo (chiffre sans intérêt).
  static String transferDoneRate(int n, int bytes, int elapsedMs) {
    if (elapsedMs < 1000 || bytes < 1000000) return transferDone(n);
    return '${transferDone(n)}, ${(bytes / 1000 / elapsedMs).round()} Mo/s';
  }

  /// Date courte d'un élément (jj/mm/aaaa hh:mm, heure locale), depuis des secondes UTC.
  static String shortDate(int seconds) {
    final d = DateTime.fromMillisecondsSinceEpoch(seconds * 1000);
    String two(int v) => v.toString().padLeft(2, '0');
    return '${two(d.day)}/${two(d.month)}/${d.year} ${two(d.hour)}:${two(d.minute)}';
  }

  static String unsupported(String content) => 'Non pris en charge ($content)';
  static String partitionName(int index) => 'Partition $index';
  static String readOnly(String reason) => 'Lecture seule : $reason';
  static String unlockTitle(String name) => 'Déverrouiller $name';

  static String partitionLine(String? label, int index, int sizeBytes, String content) {
    final name = (label == null || label.isEmpty) ? partitionName(index) : label;
    return '$name · ${humanSize(sizeBytes)} · $content';
  }

  /// Taille décimale en Go ou Mo, virgule française, une décimale (ex. « 1,5 Go »).
  static const unknownSize = 'taille inconnue';

  static String humanSize(int bytes) {
    if (bytes < 0) return unknownSize; // disque hostile : u64 > 2^63
    final isGo = bytes >= 1000000000;
    final value = bytes / (isGo ? 1e9 : 1e6);
    return '${value.toStringAsFixed(1).replaceAll('.', ',')} ${isGo ? 'Go' : 'Mo'}';
  }

  // --- formatage (phase 8)
  static const diskMenu = 'Options du disque';
  static const formatMenu = 'Formater…';
  static const formatEjectFirst = "Éjectez d'abord les volumes de ce disque";
  static const formatTitle = 'Formater le disque';
  static const fsHelpExfat = 'Windows, macOS, Linux, TV';
  static const fsHelpFat32 = 'Ancien matériel, 4 Gio par fichier au plus';
  static const fsHelpExt4 = 'Ubuntu';
  static const fsHelpNtfs = 'Windows';
  static const encryptSwitch = 'Chiffrer (LUKS2)';
  static const encryptHelp = 'La phrase de passe sera demandée à chaque ouverture, ici comme sous Ubuntu';
  static const passConfirm = 'Confirmer la phrase de passe';
  static const passEmpty = 'Saisissez une phrase de passe';
  static const passMismatch = 'Les deux phrases de passe diffèrent';
  static const formatLabel = 'Étiquette';
  static String labelLimitChars(int n) => '$n caractères au plus';
  static String labelLimitBytes(int n) => '$n octets au plus (une lettre accentuée en compte deux)';
  static const labelLimitFat32 = '11 caractères au plus, sans accent';
  static const labelLimitExfat = r'11 caractères au plus, sans / \ : * ? " < > |';
  static const labelInvalid = 'Étiquette refusée par ce système de fichiers';
  static const formatButton = 'Formater';
  static String formatConfirm(String name, String size) =>
      'Tout le contenu de « $name » ($size) sera effacé. Tapez le nom du disque pour confirmer.';
  static const formatConfirmHint = 'Nom du disque';
  static String formatting(int pct) => 'Formatage… $pct %';
  static const formatted = 'Disque formaté';
}
