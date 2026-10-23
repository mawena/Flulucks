/// Chaînes de l'interface (français). Les messages d'erreur du moteur arrivent déjà en français.
///
/// Typographie (vérifiée par test/strings_test.dart) : espace fine insécable (U+202F) avant « ? », « ! », « ; »
/// et « % » ; espace insécable (U+00A0) avant « : » et à l'intérieur des guillemets ; pluriels par [plural].
class Fr {
  Fr._();

  static const title = 'Portail';
  static const noDiskTitle = 'Aucun disque branché';
  static const noDiskBody = 'Branchez un disque USB-C, chiffré LUKS ou non\u00A0: ext4, NTFS, exFAT et FAT sont reconnus.';

  /// Singulier pour 0 et 1, comme en français (« 0 fichier »).
  static String plural(int n, String one, String many) => n.abs() < 2 ? one : many;

  /// « 1 élément », « 3 éléments ».
  static String count(int n, String one, String many) => '$n ${plural(n, one, many)}';
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
      "Le volume n'a pas été démonté proprement\u00A0: contenu possiblement incomplet";
  static const passphrase = 'Phrase de passe';
  static const showPassphrase = 'Afficher la phrase de passe';
  static const hidePassphrase = 'Masquer la phrase de passe';
  static const openReadOnly = 'Ouvrir en lecture seule';
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

  // accueil (phase 11)
  static const searchEverywhere = 'Rechercher dans Portail';
  static String sourceMenu(String label) => 'Options de $label';
  static const chipEncrypted = 'Chiffré';
  static const chipOpen = 'Ouvert';
  static const chipReadOnly = 'Lecture seule';
  static const chipLocked = 'Verrouillé';
  static const chipDirty = 'Non démonté proprement';
  static const help = 'Aide';
  static const about = 'À propos';
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
  static const deleteRemoteBody = "Pas de corbeille sur un dossier distant\u00A0: la suppression est définitive.";
  static const unknownSpace = 'Espace inconnu';
  static const nowPlaying = 'Lecture en cours';
  static const play = 'Lecture';
  static const pause = 'Pause';
  static const nextTrack = 'Suivant';
  static const previousTrack = 'Précédent';
  static const back10 = 'Reculer de 10 s';
  static const forward10 = 'Avancer de 10 s';
  static const stopPlayback = 'Arrêter la lecture';
  static const copy = 'Copier';
  static const copyText = 'Copier le texte';
  static const cut = 'Couper';
  static const pasteHere = 'Coller ici';
  static const compress = 'Compresser';
  static const archiveName = "Nom de l'archive";
  static const archiveDefault = 'Archive';
  static const trash = 'Corbeille';
  static const trashEmpty = 'La corbeille est vide';
  static const emptyTrash = 'Vider la corbeille';
  static const emptyTrashTitle = 'Vider la corbeille\u202F?';
  static const emptyAction = 'Vider';
  static const restore = 'Restaurer';
  static const deletePermanently = 'Supprimer définitivement';
  static const deleteToTrashBody = 'Les éléments restent 30 jours dans la corbeille de ce stockage.';
  static const trashNote = 'Les éléments supprimés depuis plus de 30 jours sont effacés.';
  static const spaceAnalysis = "Analyse de l'espace";
  static const spaceFolders = 'Dossiers';
  static const biggestFiles = 'Plus gros fichiers';
  static const analyzing = 'Analyse en cours…';
  static const spaceTruncated = "Analyse incomplète\u00A0: arrêtée, ou trop d'éléments";
  static const properties = 'Propriétés';
  static const moreActions = "Plus d'actions";
  static const location = 'Emplacement';
  static const typeLabel = 'Type';
  static const sizeLabel = 'Taille';
  static const modifiedLabel = 'Modifié le';
  static const fsLabel = 'Système de fichiers';
  static const permissionsLabel = 'Droits';
  static const ownerLabel = 'Propriétaire';
  static const computing = 'Calcul…';
  static const computeHash = "Calculer l'empreinte SHA-256";
  static const hashLabel = 'SHA-256';
  static const hashCancelled = 'Calcul annulé';
  static const copyHash = "Copier l'empreinte";
  static const hashCopied = 'Empreinte copiée';
  static const modeNumber = 'Numéroter';
  static const modeReplace = 'Remplacer';
  static const modePhotoDate = 'Date de prise de vue';
  static const baseName = 'Nom de base';
  static const startNumber = 'Premier numéro';
  static const findLabel = 'Rechercher';
  static const replaceWith = 'Remplacer par';
  static const caseSensitive = 'Respecter la casse';
  static const shortcuts = 'Raccourcis';
  static const filterPhotos = 'Photos';
  static const filterVideos = 'Vidéos';
  static const filterMusic = 'Musique';
  static const filterDocuments = 'Documents';
  static const filterArchives = 'Archives';
  static const filterRecent = 'Récents';
  static const filterEmpty = 'Aucun fichier';
  static const filterNote = "Téléphone et cartes SD, d'après l'index d'Android\u202F; 500 fichiers au plus";
  static const openFolder = 'Ouvrir le dossier';
  static const photoDateHelp = 'Date de prise de vue des photos (EXIF), sinon date de modification\u202F; extension gardée';
  static const extractHere = 'Extraire ici';
  static const extractTo = 'Extraire vers…';
  static const share = 'Partager';
  static const openWith = 'Ouvrir avec…';

  // Lecteurs de Portail (phase 10)
  static const builtInViewers = 'Ouvrir dans Portail';
  static const builtInViewersHelp = "Images, musique, vidéos et textes s'ouvrent dans les lecteurs de Portail";
  static const editAsText = 'Modifier comme texte';
  static const save = 'Enregistrer';
  static const saved = 'Enregistré';
  static const dontSave = 'Ne pas enregistrer';
  static const unsavedTitle = 'Modifications non enregistrées';
  static const unsavedBody = 'Enregistrer les modifications avant de quitter\u202F?';
  static const textChangedTitle = 'Fichier modifié entre-temps';
  static const textChangedBody = 'Le fichier a été modifié depuis son ouverture. Le remplacer par votre version\u202F?';
  static const replace = 'Remplacer';

  // Dossiers distants (phase 10)
  static const remoteFolders = 'Dossiers distants';
  static const addServer = 'Ajouter un serveur';
  static const editServer = 'Modifier';
  static const disconnect = 'Déconnecter';
  static const serverForm = 'Serveur distant';
  static const serverName = 'Nom';
  static const serverHost = 'Adresse';
  static const serverPort = 'Port';
  static const serverUser = 'Utilisateur';
  static const serverUserHelpFtp = 'Vide\u00A0: connexion anonyme';
  static const password = 'Mot de passe';
  static const passwordKeep = 'Laisser vide pour garder le mot de passe mémorisé';
  static const rememberPassword = 'Mémoriser le mot de passe';
  static const rememberHelp = 'Chiffré par le téléphone\u202F; sinon demandé à chaque connexion';
  static const startFolder = 'Dossier de départ';
  static const ftpClear = "FTP n'est pas chiffré\u00A0: le mot de passe et les fichiers circulent en clair sur le réseau.";
  static const hostKey = 'Empreinte du serveur acceptée';
  static const forgetHostKey = "Oublier l'empreinte";
  static const privateKey = 'Clé privée';
  static const noPrivateKey = 'Aucune\u00A0: connexion par mot de passe';
  static const importKey = 'Importer une clé';
  static const removeKey = 'Retirer la clé';
  static const keyPassphrase = 'Phrase de la clé';
  static const keyPassphraseHelp = 'Clé chiffrée\u00A0: laissez vide pour la saisir à chaque connexion';
  static const keyAfterSave = 'Enregistrez le serveur pour pouvoir importer une clé privée (OpenSSH\u00A0: Ed25519, ECDSA ou RSA).';
  static const deleteServer = 'Supprimer le serveur';
  static const connect = 'Se connecter';
  static const wrongPassword = 'Mot de passe refusé';
  static const trustTitle = 'Serveur inconnu';
  static const trust = 'Faire confiance';
  static const hostKeyChangedTitle = 'Clé du serveur changée';
  static const emptyFolder = 'Dossier vide';
  static const protectedFolder = 'Dossier protégé par Android';
  static const volumeClosed = 'Le disque a été fermé';
  static const storageGoneKeepText =
      'Stockage fermé\u00A0: le texte ne peut plus être enregistré. Copiez-le avant de quitter (appui long, Tout sélectionner).';
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
  static const theme = 'Thème';
  static const themeSystem = 'Système';
  static const themeLight = 'Clair';
  static const themeDark = 'Sombre';
  static const addFavorite = 'Ajouter aux favoris';
  static const add = 'Ajouter';
  static const encryptedFavorite = "Le nom de ce dossier restera visible sur l'accueil, même disque verrouillé";
  static const removeFavorite = 'Retirer des favoris';
  static const favoriteUnavailable = 'Disque non ouvert';
  static const search = 'Rechercher';
  static const searchHint = 'Nom du fichier';
  static const noResult = 'Aucun résultat';
  static const tooManyResults = 'Plus de 500 résultats\u00A0: précisez la recherche';
  static const transferCancelled = 'Opération annulée';
  static const ok = 'OK';

  // composants communs (phase 11)
  static const retry = 'Réessayer';
  static const undo = 'Annuler';
  static const nameEmpty = 'Saisissez un nom';
  static const nameSlash = 'Le nom ne peut pas contenir «\u00A0/\u00A0»';
  static const nameReserved = 'Nom réservé';
  static const nameTooLong = 'Nom trop long';
  static const nameBadChar = 'Caractère interdit dans le nom';
  static String usedPercent(int p) => '$p\u202F% utilisés';

  static String freeOf(int free, int total) => '${humanSize(free)} libres sur ${humanSize(total)}';
  static String deleteOne(String name) => 'Supprimer «\u00A0$name\u00A0»\u202F?';
  static String trackOf(int i, int n) => 'Morceau $i sur $n';

  /// Durée « m:ss » ou « h:mm:ss » ; « --:-- » si inconnue.
  static String clock(int ms) {
    if (ms < 0) return '--:--';
    final s = ms ~/ 1000;
    final ss = (s % 60).toString().padLeft(2, '0');
    if (s < 3600) return '${s ~/ 60}:$ss';
    return '${s ~/ 3600}:${(s ~/ 60 % 60).toString().padLeft(2, '0')}:$ss';
  }
  static String deleteMany(int n) => 'Supprimer ${count(n, 'élément', 'éléments')}\u202F?';
  static String clipboard(int n, bool move) => '${count(n, 'élément', 'éléments')} ${move ? 'à déplacer' : 'à copier'}';
  static String selected(int n) => count(n, 'sélectionné', 'sélectionnés');
  /// [kind] : `copy`, `move`, `compress` ou `extract`.
  static String transferLine(String kind, String name, int i, int n, int pct) {
    // extraction, premier passage : en-têtes lus, aucun fichier encore compté
    if (kind == 'extract' && n == 0) return "Lecture de l'archive $name…";
    final what = switch (kind) {
      'move' => 'Déplacement',
      'compress' => 'Compression',
      'extract' => 'Extraction',
      _ => 'Copie',
    };
    return '$what de $name — $i sur $n · $pct\u202F%';
  }

  static String readOnlyText(String reason) => 'Lecture seule\u00A0: $reason';
  static String serverLine(String protocol, String user, String host, int port, bool connected) =>
      '${protocol.toUpperCase()} · ${user.isEmpty ? '' : '$user@'}$host:$port${connected ? ' · connecté' : ''}';
  static String passwordFor(String name) => 'Mot de passe de $name';
  static String trustBody(String fingerprint) =>
      "Première connexion à ce serveur. Vérifiez que son empreinte est bien celle-ci avant de lui faire confiance\u00A0:\n\n$fingerprint";
  static String deleteServerBody(String name) => 'Le serveur «\u00A0$name\u00A0» et ses secrets mémorisés seront supprimés.';
  static String imagePosition(int i, int n) => '$i / $n';
  static String toExtract(String name) => '«\u00A0$name\u00A0» à extraire';
  static String trashSummary(int n, int bytes) => '${count(n, 'élément', 'éléments')}, ${humanSize(bytes)}';
  static String emptyTrashBody(int n) =>
      n < 2 ? "L'élément sera supprimé définitivement." : 'Les $n éléments seront supprimés définitivement.';
  static String actionsOf(String name) => 'Actions de $name';
  static String spaceTotal(int bytes, int files) => '${humanSize(bytes)} — ${count(files, 'fichier', 'fichiers')}';
  static String spaceOthers(int n) => '… et ${count(n, 'autre élément', 'autres éléments')}';
  static String percent(int p) => '$p\u202F%';
  static String folderSize(int bytes, int files, int dirs, {bool truncated = false}) =>
      '${truncated ? 'Au moins ' : ''}${humanSize(bytes)} — ${count(files, 'fichier', 'fichiers')}, '
      '${count(dirs, 'dossier', 'dossiers')}';
  static String ownerLine(int uid, int gid) => 'uid $uid, gid $gid';
  static String batchRenameTitle(int n) => 'Renommer ${count(n, 'élément', 'éléments')}';
  static String filterLine(String source, String folder, int size) => '$source · $folder · ${humanSize(size)}';
  static String typeName(String type) => switch (type) {
        'dir' => 'Dossier',
        'file' => 'Fichier',
        'link' => 'Lien symbolique',
        _ => 'Autre',
      };
  static String transferDone(int n) => 'Terminé\u00A0: ${count(n, 'fichier', 'fichiers')}';

  /// « Terminé : 3 fichiers, 34 Mo/s » ; sans débit sous 1 s ou sous 1 Mo (chiffre sans intérêt).
  static String transferDoneRate(int n, int bytes, int elapsedMs) {
    if (elapsedMs < 1000 || bytes < 1000000) return transferDone(n);
    return '${transferDone(n)}, ${(bytes / 1000 / elapsedMs).round()} Mo/s';
  }

  static String _two(int v) => v.toString().padLeft(2, '0');

  /// Date courte d'un élément (jj/mm/aaaa hh:mm, heure locale), depuis des secondes UTC : propriétés.
  static String shortDate(int seconds) {
    final d = DateTime.fromMillisecondsSinceEpoch(seconds * 1000);
    return '${_two(d.day)}/${_two(d.month)}/${d.year} ${_two(d.hour)}:${_two(d.minute)}';
  }

  static const _months = [
    'janvier', 'février', 'mars', 'avril', 'mai', 'juin', //
    'juillet', 'août', 'septembre', 'octobre', 'novembre', 'décembre',
  ];

  /// Date des listes, relative aux jours proches (heure locale) : « Aujourd'hui, 14:05 », « Hier, 09:12 »,
  /// « 3 mars, 14:05 » dans l'année, sinon « 03/03/2025 ». [now] : pour les tests.
  static String friendlyDate(int seconds, {DateTime? now}) {
    final d = DateTime.fromMillisecondsSinceEpoch(seconds * 1000);
    final n = now ?? DateTime.now();
    final time = '${_two(d.hour)}:${_two(d.minute)}';
    final day = DateTime(d.year, d.month, d.day), today = DateTime(n.year, n.month, n.day);
    if (day == today) return "Aujourd'hui, $time";
    if (day == DateTime(n.year, n.month, n.day - 1)) return 'Hier, $time';
    if (d.year == n.year) return '${d.day} ${_months[d.month - 1]}, $time';
    return '${_two(d.day)}/${_two(d.month)}/${d.year}';
  }

  static String unsupported(String content) => 'Non pris en charge ($content)';
  static String partitionName(int index) => 'Partition $index';
  static String readOnly(String reason) => 'Lecture seule\u00A0: $reason';
  static String unlockTitle(String name) => 'Déverrouiller $name';

  static String partitionLine(String? label, int index, int sizeBytes, String content) {
    final name = (label == null || label.isEmpty) ? partitionName(index) : label;
    return '$name · ${humanSize(sizeBytes)} · $content';
  }

  static const unknownSize = 'taille inconnue';
  static const _units = ['ko', 'Mo', 'Go', 'To'];

  /// Taille décimale, virgule française, une décimale sauf en octets (« 512 octets », « 1,5 ko », « 2,0 To »).
  static String humanSize(int bytes) {
    if (bytes < 0) return unknownSize; // disque hostile : u64 > 2^63
    if (bytes < 1000) return count(bytes, 'octet', 'octets');
    var value = bytes / 1000;
    var unit = 0;
    // l'arrondi à une décimale décide de l'unité : 999 960 octets font « 1,0 Mo », pas « 1000,0 ko »
    while (unit < _units.length - 1 && double.parse(value.toStringAsFixed(1)) >= 1000) {
      value /= 1000;
      unit++;
    }
    return '${value.toStringAsFixed(1).replaceAll('.', ',')} ${_units[unit]}';
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
  // signes séparés par des espaces insécables : la liste ne se coupe pas
  static const labelLimitExfat = '11 caractères au plus, sans /\u00A0\\\u00A0:\u00A0*\u00A0?\u00A0"\u00A0<\u00A0>\u00A0|';
  static const labelInvalid = 'Étiquette refusée par ce système de fichiers';
  static const formatButton = 'Formater';
  static String formatConfirm(String name, String size) =>
      'Tout le contenu de «\u00A0$name\u00A0» ($size) sera effacé. Tapez le nom du disque pour confirmer.';
  static const formatConfirmHint = 'Nom du disque';
  static String formatDanger(String name, String size) =>
      'Tout le contenu de «\u00A0$name\u00A0» ($size) sera effacé définitivement, sur toutes ses partitions.';
  static String formatting(int pct) => 'Formatage… $pct\u202F%';
  static const formatted = 'Disque formaté';
}
