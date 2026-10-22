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
  static String transferLine(bool move, String name, int i, int n, int pct) =>
      '${move ? 'Déplacement' : 'Copie'} : $name ($i/$n) — $pct %';
  static String transferDone(int n) => 'Terminé : $n fichier(s)';

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
}
