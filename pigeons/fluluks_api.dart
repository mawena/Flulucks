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
  });
  String id;
  String stableKey;
  String kind;
  String label;
  int totalBytes;
  int freeBytes;
  bool readOnly;
  String roReason;
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

/// Avancement d'une copie ou d'un déplacement ; `error` peut accompagner `finished`.
class TransferMsg {
  TransferMsg({
    required this.move,
    required this.currentName,
    required this.fileIndex,
    required this.fileCount,
    required this.doneBytes,
    required this.totalBytes,
    required this.finished,
    required this.cancelled,
    this.error,
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
}

/// Résultats d'une recherche ; `truncated` au-delà de 500.
class SearchResultMsg {
  SearchResultMsg({required this.entries, required this.truncated});
  List<EntryMsg> entries;
  bool truncated;
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
  @async
  void deleteEntries(String sourceId, List<String> paths);
  void openWith(String sourceId, String path);
  void share(String sourceId, List<String> paths);
  void startTransfer(String srcId, List<String> paths, String dstId, String dstDir, bool move);
  void cancelTransfer();
  @async
  SearchResultMsg search(String sourceId, String path, String query, bool showHidden);
  void cancelSearch();
}

@FlutterApi()
abstract class FluLuksEventsApi {
  void onStateChanged(List<DiskMsg> disks, List<VolumeMsg> volumes);

  /// Éjection en cours (synchronisation, démontage) et dernier échec (« Éjection impossible : … »).
  void onEjectStateChanged(bool ejecting, String? error);

  /// Copie ou déplacement en cours ou terminé ; null tant qu'aucune opération n'a eu lieu.
  void onTransferChanged(TransferMsg? transfer);
}
