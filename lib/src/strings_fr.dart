/// Chaînes de l'interface (français). Les messages d'erreur du moteur arrivent déjà en français.
class Fr {
  Fr._();

  static const title = 'FluLuks';
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
