// Générer avec (depuis la racine du dépôt) :
//   dart run pigeon --input pigeons/fluluks_api.dart
// Pigeon 27.3.0 : la 29.0.6 demandée est incompatible avec meta 1.17.0 imposé par Flutter 3.41.4.
// Les sorties (voir ConfigurePigeon) sont commitées.
import 'package:pigeon/pigeon.dart';

@ConfigurePigeon(PigeonOptions(
  dartOut: 'lib/src/bridge/fluluks_api.g.dart',
  dartOptions: DartOptions(),
  kotlinOut: 'android/app/src/main/kotlin/com/mawena/fluluks/bridge/FluLuksApi.g.kt',
  kotlinOptions: KotlinOptions(package: 'com.mawena.fluluks.bridge'),
))
class PartitionMsg {
  PartitionMsg({
    required this.index,
    required this.sizeBytes,
    required this.content,
    required this.label,
    required this.supported,
    required this.encrypted,
    this.cipher,
    this.volumeId,
  });
  int index;
  int sizeBytes;
  String content;
  String label;
  bool supported;
  bool encrypted;
  String? cipher;
  String? volumeId;
}

class DiskMsg {
  DiskMsg({
    required this.id,
    required this.name,
    required this.sizeBytes,
    required this.hasPermission,
    this.error,
    required this.partitions,
  });
  String id;
  String name;
  int sizeBytes;
  bool hasPermission;
  String? error;
  List<PartitionMsg> partitions;
}

class VolumeMsg {
  VolumeMsg({
    required this.id,
    required this.diskId,
    required this.partIndex,
    required this.title,
    required this.totalBytes,
    required this.freeBytes,
    required this.needsRecovery,
    required this.roReason,
    required this.readOnly,
  });
  String id;
  String diskId;
  int partIndex;
  String title;
  int totalBytes;
  int freeBytes;
  bool needsRecovery;
  String roReason;
  bool readOnly;
}

/// Un stockage du gestionnaire de fichiers. `kind` : `phone`, `sd` ou `disk`.
class SourceMsg {
  SourceMsg({
    required this.id,
    required this.stableKey,
    required this.kind,
    required this.label,
    required this.totalBytes,
    required this.freeBytes,
    required this.readOnly,
    required this.roReason,
    required this.encrypted,
  });
  String id;
  String stableKey;
  String kind;
  String label;
  int totalBytes;
  int freeBytes;
  bool readOnly;
  String roReason;

  /// Disque ouvert avec une phrase de passe (un favori y montre un nom de dossier chiffré).
  bool encrypted;
}

/// Élément d'un dossier. `type` : `dir`, `file`, `link` ou `other` ; `mtime` en secondes UTC.
class EntryMsg {
  EntryMsg({
    required this.name,
    required this.path,
    required this.type,
    required this.size,
    required this.mtime,
  });
  String name;
  String path;
  String type;
  int size;
  int mtime;
}

/// Musique en cours (phase 10) : morceau `index` sur `count` de la file ; positions en millisecondes (durée −1 :
/// inconnue).
class PlayerMsg {
  PlayerMsg({
    required this.sourceId,
    required this.title,
    required this.index,
    required this.count,
    required this.playing,
    required this.positionMs,
    required this.durationMs,
  });
  String sourceId;
  String title;
  int index;
  int count;
  bool playing;
  int positionMs;
  int durationMs;
}

/// Avancement d'une copie ou d'un déplacement ; `error` peut accompagner `finished`.
class TransferMsg {
  TransferMsg({
    required this.kind,
    required this.move,
    required this.currentName,
    required this.fileIndex,
    required this.fileCount,
    required this.doneBytes,
    required this.totalBytes,
    required this.finished,
    required this.cancelled,
    this.error,
    required this.elapsedMs,
  });
  bool move;
  String currentName;
  int fileIndex;
  int fileCount;
  int doneBytes;
  int totalBytes;
  bool finished;
  bool cancelled;
  String? error;

  /// Durée de l'opération en millisecondes (débit affiché à la fin).
  int elapsedMs;

  /// `copy`, `move`, `compress` ou `extract`.
  String kind;
}

/// Élément de la corbeille d'un stockage : `id` est son nom dans la corbeille, `name` et `originalPath` disent
/// d'où il vient ; `deletedAt` en millisecondes (0 si inconnue).
class TrashItemMsg {
  TrashItemMsg({
    required this.id,
    required this.name,
    required this.originalPath,
    required this.deletedAt,
    required this.size,
    required this.isDir,
  });
  String id;
  String name;
  String originalPath;
  int deletedAt;
  int size;
  bool isDir;
}

/// Propriétés d'un élément ; `mode`, `uid`, `gid` et `permissions` (« rwxr-xr-x ») sur ext4 seulement ; `fs` :
/// système de fichiers d'un volume de disque, vide pour le téléphone.
class DetailsMsg {
  DetailsMsg({
    required this.name,
    required this.path,
    required this.type,
    required this.size,
    required this.mtime,
    this.mode,
    this.uid,
    this.gid,
    this.permissions,
    required this.fs,
  });
  String name;
  String path;
  String type;
  int size;
  int mtime;
  int? mode;
  int? uid;
  int? gid;
  String? permissions;
  String fs;
}

/// Texte ouvert dans l'éditeur : `text` sans BOM, fins de ligne en « \n » ; `bom` et `crlf` à rendre à
/// l'enregistrement ; `mtime` (secondes) et `size` pour voir s'il a changé ; `readOnlyReason` si le stockage est en
/// lecture seule.
class TextFileMsg {
  TextFileMsg({
    required this.text,
    required this.bom,
    required this.crlf,
    required this.mtime,
    required this.size,
    this.readOnlyReason,
  });
  String text;
  bool bom;
  bool crlf;
  int mtime;
  int size;
  String? readOnlyReason;
}

/// Enregistrement d'un texte : `changed` si le fichier a changé depuis son ouverture (rien n'est alors écrit).
class TextSaveMsg {
  TextSaveMsg({required this.saved, required this.changed, required this.mtime, required this.size});
  bool saved;
  bool changed;
  int mtime;
  int size;
}

/// Règle de renommage par lot : `mode` `number` (`base`, `start`), `replace` (`find`, `replace`, `caseSensitive`)
/// ou `photoDate`.
class RenameRuleMsg {
  RenameRuleMsg({
    required this.mode,
    required this.base,
    required this.start,
    required this.find,
    required this.replace,
    required this.caseSensitive,
  });
  String mode;
  String base;
  int start;
  String find;
  String replace;
  bool caseSensitive;
}

/// Nouveau nom d'un élément du lot ; `conflict` : raison du refus (null si permis).
class RenamePreviewMsg {
  RenamePreviewMsg({required this.path, required this.newName, this.conflict});
  String path;
  String newName;
  String? conflict;
}

/// Fichier d'un filtre rapide de l'accueil : stockage (téléphone, carte SD) et chemin dans ce stockage.
class FilterItemMsg {
  FilterItemMsg({required this.sourceId, required this.path, required this.name, required this.size, required this.mtime});
  String sourceId;
  String path;
  String name;
  int size;
  int mtime;
}

/// Taille d'un dossier et de son contenu ; `truncated` si le calcul s'est arrêté avant la fin.
class FolderSizeMsg {
  FolderSizeMsg({required this.size, required this.files, required this.dirs, required this.truncated});
  int size;
  int files;
  int dirs;
  bool truncated;
}

/// Élément d'un dossier analysé (taille totale pour un dossier).
class SpaceItemMsg {
  SpaceItemMsg({required this.name, required this.path, required this.isDir, required this.size, required this.fileCount});
  String name;
  String path;
  bool isDir;
  int size;
  int fileCount;
}

/// Occupation d'un dossier : enfants du plus gros au plus petit (200 au plus, le reste résumé par `otherCount` et
/// `otherBytes`), plus gros fichiers sous lui ; `truncated` si l'analyse s'est arrêtée avant la fin.
class SpaceReportMsg {
  SpaceReportMsg({
    required this.path,
    required this.totalBytes,
    required this.fileCount,
    required this.children,
    required this.biggest,
    required this.truncated,
    required this.otherCount,
    required this.otherBytes,
  });
  String path;
  int totalBytes;
  int fileCount;
  List<SpaceItemMsg> children;
  List<EntryMsg> biggest;
  bool truncated;
  int otherCount;
  int otherBytes;
}

/// Résultats d'une recherche ; `truncated` au-delà de 500.
class SearchResultMsg {
  SearchResultMsg({required this.entries, required this.truncated});
  List<EntryMsg> entries;
  bool truncated;
}

/// Serveur distant enregistré (phase 10) : `protocol` `ftp` ou `sftp` ; `remember` : mot de passe mémorisé (scellé
/// par le Keystore) ; `hostKey` : clé SFTP acceptée ; `keyInfo` : clé privée importée ; `hasPassword` : un mot de
/// passe est mémorisé ; `connected` : connexion ouverte (source `remote:<id>`).
class RemoteServerMsg {
  RemoteServerMsg({
    required this.id,
    required this.protocol,
    required this.name,
    required this.host,
    required this.port,
    required this.user,
    required this.startPath,
    required this.remember,
    this.hostKey,
    this.keyInfo,
    required this.hasPassword,
    required this.connected,
  });
  String id;
  String protocol;
  String name;
  String host;
  int port;
  String user;
  String startPath;
  bool remember;
  String? hostKey;
  String? keyInfo;
  bool hasPassword;
  bool connected;
}

/// Préférences d'affichage ; `sort` : `name`, `size` ou `date` ; `builtInViewers` : ouvrir images, musique, vidéos et
/// textes dans les lecteurs de Portail (sinon dans une autre application).
class ViewPrefsMsg {
  ViewPrefsMsg({
    required this.sort,
    required this.ascending,
    required this.grid,
    required this.showHidden,
    required this.builtInViewers,
  });
  String sort;
  bool ascending;
  bool grid;
  bool showHidden;
  bool builtInViewers;
}

/// Favori ; `sourceId` null si le stockage n'est pas ouvert.
class FavoriteMsg {
  FavoriteMsg({required this.key, required this.path, required this.label, this.sourceId});
  String key;
  String path;
  String label;
  String? sourceId;
}

@HostApi()
abstract class FluLuksHostApi {
  List<DiskMsg> listDisks();
  List<VolumeMsg> listVolumes();
  void requestPermission(String diskId);
  @async
  VolumeMsg unlock(String diskId, int partIndex, Uint8List passphrase, bool writable);
  @async
  VolumeMsg mountPlain(String diskId, int partIndex, bool writable);
  void eject(String volumeId);
  void openInFiles(String volumeId);
  bool getReadOnlyPreference();
  void setReadOnlyPreference(bool value);

  /// Verrouillage automatique des disques chiffrés, en minutes (0 = jamais ; 5, 15, 30 ou 60).
  int getAutoLockMinutes();
  void setAutoLockMinutes(int minutes);

  // --- gestionnaire de fichiers (phase 6)
  List<SourceMsg> listSources();
  bool hasAllFilesAccess();
  void requestAllFilesAccess();
  @async
  List<EntryMsg> listDir(String sourceId, String path, bool showHidden);
  @async
  String createFolder(String sourceId, String parent, String name);
  @async
  String rename(String sourceId, String path, String newName);
  /// `permanent` faux : à la corbeille du stockage (définitif pour ce qui y est déjà).
  @async
  void deleteEntries(String sourceId, List<String> paths, bool permanent);
  @async
  List<TrashItemMsg> listTrash(String sourceId);
  @async
  void restoreTrash(String sourceId, List<String> ids);
  @async
  void purgeTrash(String sourceId, List<String> ids);
  @async
  void emptyTrash(String sourceId);
  @async
  void openWith(String sourceId, String path);

  /// Lecteur vidéo de Portail (plein écran, sous-titres `.srt` à côté de la vidéo).
  @async
  void playVideo(String sourceId, String path);

  /// Musique : file faite des sons de `paths` (le dossier dans l'ordre affiché), départ sur `paths[startIndex]`.
  /// Asynchrone : les URI d'un dossier distant se préparent par le réseau, hors du fil principal.
  @async
  void playAudio(String sourceId, List<String> paths, int startIndex);

  /// `play`, `pause`, `next`, `previous`, `back`, `forward`, `seek` (`positionMs`) ou `stop`.
  void playerCommand(String command, int? positionMs);
  @async
  void share(String sourceId, List<String> paths);
  void startTransfer(String srcId, List<String> paths, String dstId, String dstDir, bool move);

  /// Compresse `paths` dans un ZIP `name` sous `dstDir` (même stockage) ; même créneau que les copies.
  void startCompress(String srcId, List<String> paths, String dstDir, String name);

  /// Extrait `archive` sous `dstDir` de `dstId`, dans un dossier au nom de l'archive si `intoNewFolder`.
  void startExtract(String srcId, String archive, String dstId, String dstDir, bool intoNewFolder);
  void cancelTransfer();
  @async
  SearchResultMsg search(String sourceId, String path, String query, bool showHidden);
  void cancelSearch();

  /// Analyse de l'espace sous `path` (une à la fois) ; `spaceChildren` relit un dossier de la dernière analyse.
  @async
  SpaceReportMsg analyzeSpace(String sourceId, String path);
  @async
  SpaceReportMsg spaceChildren(String sourceId, String path);
  void cancelAnalysis();

  /// Propriétés ; taille d'un dossier et SHA-256 d'un fichier calculés à la demande, arrêtés par `cancelHash`
  /// (`sha256` rend alors null ; progression par `onHashProgress`).
  @async
  DetailsMsg details(String sourceId, String path);
  @async
  FolderSizeMsg folderSize(String sourceId, String path);
  @async
  String? sha256(String sourceId, String path);
  void cancelHash();

  /// Lecteurs (phase 10) : octets d'une image (64 Mio au plus, HEIC converti en JPEG) ; texte UTF-8 de l'éditeur
  /// (2 Mio au plus), enregistré par un fichier temporaire, refusé (`changed`) s'il a changé depuis son ouverture,
  /// sauf `force`.
  /// Dossiers distants (phase 10). `saveServer` : identifiant vide pour un nouveau serveur ; mots de passe null :
  /// ceux mémorisés sont gardés. `connectServer` échoue avec le code `auth` (mot de passe requis ou refusé ; détails :
  /// vrai s'il manquait), `hostkey-unknown` (détails : l'empreinte à montrer) ou `hostkey-changed`.
  List<RemoteServerMsg> listServers();
  RemoteServerMsg saveServer(RemoteServerMsg server, Uint8List? password, Uint8List? keyPassphrase);
  void deleteServer(String serverId);
  @async
  SourceMsg connectServer(String serverId, Uint8List? password);
  void disconnectServer(String serverId);
  void trustHostKey(String serverId, String fingerprint);
  void forgetHostKey(String serverId);

  /// Clé privée OpenSSH choisie dans le sélecteur d'Android, scellée pour `serverId` ; rend sa description
  /// (« ssh-ed25519 SHA256:… »), ou null si rien n'a été choisi.
  @async
  String? importPrivateKey(String serverId);
  void forgetPrivateKey(String serverId);

  @async
  Uint8List readImage(String sourceId, String path);
  @async
  TextFileMsg readText(String sourceId, String path);
  @async
  TextSaveMsg writeText(
    String sourceId,
    String path,
    String text,
    bool bom,
    bool crlf,
    int expectedMtime,
    int expectedSize,
    bool force,
  );

  /// Filtre rapide (`photos`, `videos`, `music`, `documents`, `archives`, `recent`) : 500 fichiers au plus, du plus récent.
  @async
  List<FilterItemMsg> quickFilter(String filter);

  /// Renommage par lot : aperçu, puis tout ou rien (refusé si un nom est en conflit).
  @async
  List<RenamePreviewMsg> previewRename(String sourceId, List<String> paths, RenameRuleMsg rule);
  @async
  void applyRename(String sourceId, List<String> paths, RenameRuleMsg rule);
  @async
  Uint8List? thumbnail(String sourceId, String path, int sizePx);
  ViewPrefsMsg getViewPrefs();
  void setViewPrefs(ViewPrefsMsg prefs);
  List<FavoriteMsg> listFavorites();
  void addFavorite(String sourceId, String path, String label);
  void removeFavorite(String key, String path);

  // --- formatage (phase 8)
  /// Formate tout le disque : fs parmi fat32, exfat, ext4, ntfs ; passphrase si encrypted (mise à zéro).
  @async
  void formatDisk(String diskId, String fs, bool encrypted, String label, Uint8List? passphrase);
}

@FlutterApi()
abstract class FluLuksEventsApi {
  void onStateChanged(List<DiskMsg> disks, List<VolumeMsg> volumes);

  /// Éjection en cours (synchronisation, démontage) et dernier échec (« Éjection impossible : … »).
  void onEjectStateChanged(bool ejecting, String? error);

  /// Copie ou déplacement en cours ou terminé ; null tant qu'aucune opération n'a eu lieu.
  void onTransferChanged(TransferMsg? transfer);

  /// Pourcentage du formatage en cours ; null quand il n'y en a pas (ou plus).
  void onFormatProgress(int? pct);

  /// Pourcentage du calcul d'empreinte en cours ; null à la fin.
  void onHashProgress(int? pct);

  /// Musique en cours, ou null à l'arrêt ; `message` : raison d'un arrêt subi (« Lecture arrêtée : stockage fermé »).
  void onPlayerChanged(PlayerMsg? player, String? message);
}
