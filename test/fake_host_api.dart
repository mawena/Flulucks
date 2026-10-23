import 'dart:async';
import 'dart:convert';

import 'package:flutter/services.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/state/host_api.dart';

PartitionMsg part({
  int index = 1,
  int size = 2000000000,
  String content = 'LUKS2',
  String label = '',
  bool supported = true,
  bool encrypted = true,
  String? volumeId,
}) =>
    PartitionMsg(
      index: index,
      sizeBytes: size,
      content: content,
      label: label,
      supported: supported,
      encrypted: encrypted,
      volumeId: volumeId,
    );

DiskMsg disk(List<PartitionMsg> parts, {String id = 'd1'}) => DiskMsg(
      id: id,
      name: 'Disque USB',
      sizeBytes: 4000000000,
      hasPermission: true,
      partitions: parts,
    );

VolumeMsg volume({String id = 'v1', bool readOnly = false, String roReason = ''}) => VolumeMsg(
      id: id,
      diskId: 'd1',
      partIndex: 1,
      title: 'Données',
      totalBytes: 1000,
      freeBytes: 500,
      needsRecovery: false,
      roReason: roReason,
      readOnly: readOnly,
    );

/// Faux pont : enregistre les appels. Les octets de phrase de passe sont copiés à l'appel, et la
/// référence d'origine est gardée pour vérifier qu'elle est mise à zéro ensuite.
class FakeHostApi implements HostApi {
  List<DiskMsg> disks = [];
  List<VolumeMsg> volumes = [];
  final ejected = <String>[];
  final mounted = <(String, int, bool)>[];
  final unlockWritable = <bool>[];
  bool readOnlyPreference = false;
  final permissionRequests = <String>[];
  Uint8List? passphraseRef;
  List<int>? passphraseCopy;

  /// Formatages demandés : (disque, fs, chiffré, étiquette, phrase copiée).
  final formats = <(String, String, bool, String, String?)>[];
  Uint8List? formatPassRef;
  String? formatError;
  Completer<void>? formatGate;

  @override
  Future<void> formatDisk(String diskId, String fs, bool encrypted, String label, Uint8List? passphrase) async {
    formatPassRef = passphrase;
    formats.add((diskId, fs, encrypted, label, passphrase == null ? null : utf8.decode(List<int>.of(passphrase))));
    if (formatGate != null) await formatGate!.future;
    if (formatError != null) throw PlatformException(code: '-14', message: formatError);
  }

  /// Si non nul, unlock attend ce completer (test de l'état « en cours »).
  Completer<void>? gate;
  String correctPassphrase = 'bonne';

  @override
  Future<List<DiskMsg>> listDisks() async => disks;

  @override
  Future<List<VolumeMsg>> listVolumes() async => volumes;

  @override
  Future<void> requestPermission(String diskId) async => permissionRequests.add(diskId);

  @override
  Future<VolumeMsg> unlock(String diskId, int partIndex, Uint8List passphrase, bool writable) async {
    unlockWritable.add(writable);
    passphraseRef = passphrase;
    passphraseCopy = List<int>.of(passphrase);
    if (gate != null) await gate!.future;
    if (utf8.decode(passphraseCopy!) != correctPassphrase) {
      throw PlatformException(code: '-5', message: 'phrase de passe incorrecte');
    }
    return volume();
  }

  @override
  Future<VolumeMsg> mountPlain(String diskId, int partIndex, bool writable) async {
    mounted.add((diskId, partIndex, writable));
    return volume();
  }

  @override
  Future<void> eject(String volumeId) async => ejected.add(volumeId);

  @override
  Future<void> openInFiles(String volumeId) async {}

  @override
  Future<bool> getReadOnlyPreference() async => readOnlyPreference;

  @override
  Future<void> setReadOnlyPreference(bool value) async => readOnlyPreference = value;

  int autoLockMinutes = 0;
  @override
  Future<int> getAutoLockMinutes() async => autoLockMinutes;
  @override
  Future<void> setAutoLockMinutes(int minutes) async => autoLockMinutes = minutes;

  List<SourceMsg> sources = [];
  bool allFilesAccess = true;
  int accessRequests = 0;

  @override
  Future<List<SourceMsg>> listSources() async => sources;

  @override
  Future<bool> hasAllFilesAccess() async => allFilesAccess;

  @override
  Future<void> requestAllFilesAccess() async => accessRequests++;

  /// Contenu des dossiers, par `sourceId` puis chemin ; un chemin dans [protectedDirs] est refusé.
  final dirs = <String, Map<String, List<EntryMsg>>>{};
  final protectedDirs = <String>{};
  final calls = <String>[];

  @override
  Future<List<EntryMsg>> listDir(String sourceId, String path, bool showHidden) async {
    calls.add('listDir $sourceId $path $showHidden');
    if (protectedDirs.contains(path)) {
      throw PlatformException(code: '-1', message: 'Dossier protégé par Android');
    }
    final l = dirs[sourceId]?[path] ?? const <EntryMsg>[];
    return showHidden ? l : l.where((e) => !e.name.startsWith('.')).toList();
  }

  @override
  Future<String> createFolder(String sourceId, String parent, String name) async {
    calls.add('createFolder $sourceId $parent $name');
    return parent == '/' ? '/$name' : '$parent/$name';
  }

  @override
  Future<String> rename(String sourceId, String path, String newName) async {
    calls.add('rename $sourceId $path $newName');
    return '${path.substring(0, path.lastIndexOf('/'))}/$newName';
  }

  /// « delete … » : définitif ; « trash … » : mis à la corbeille.
  @override
  Future<void> deleteEntries(String sourceId, List<String> paths, bool permanent) async =>
      calls.add('${permanent ? 'delete' : 'trash'} $sourceId ${paths.join(',')}');

  /// Analyses de l'espace par dossier ; [spaceFuture] remplace la réponse de analyzeSpace (analyse en cours).
  final space = <String, SpaceReportMsg>{};
  Future<SpaceReportMsg>? spaceFuture;

  @override
  Future<SpaceReportMsg> analyzeSpace(String sourceId, String path) async {
    calls.add('analyzeSpace $sourceId $path');
    return spaceFuture ?? space[path]!;
  }

  @override
  Future<SpaceReportMsg> spaceChildren(String sourceId, String path) async {
    calls.add('spaceChildren $sourceId $path');
    return space[path]!;
  }

  @override
  Future<void> cancelAnalysis() async => calls.add('cancelAnalysis');

  /// Propriétés par chemin ; taille des dossiers ; réponse de sha256 (null : annulé).
  final detailsByPath = <String, DetailsMsg>{};
  final folderSizes = <String, FolderSizeMsg>{};
  Future<String?>? sha256Future;
  int hashCancels = 0;

  @override
  Future<DetailsMsg> details(String sourceId, String path) async {
    calls.add('details $sourceId $path');
    return detailsByPath[path]!;
  }

  @override
  Future<FolderSizeMsg> folderSize(String sourceId, String path) async {
    calls.add('folderSize $sourceId $path');
    return folderSizes[path]!;
  }

  @override
  Future<String?> sha256(String sourceId, String path) async {
    calls.add('sha256 $sourceId $path');
    return sha256Future ?? 'f' * 64;
  }

  @override
  Future<void> cancelHash() async => hashCancels++;

  /// Corbeille de chaque stockage.
  final trash = <String, List<TrashItemMsg>>{};

  @override
  Future<List<TrashItemMsg>> listTrash(String sourceId) async {
    calls.add('listTrash $sourceId');
    return List.of(trash[sourceId] ?? const <TrashItemMsg>[]);
  }

  @override
  Future<void> restoreTrash(String sourceId, List<String> ids) async {
    calls.add('restoreTrash $sourceId ${ids.join(',')}');
    trash[sourceId]?.removeWhere((t) => ids.contains(t.id));
  }

  @override
  Future<void> purgeTrash(String sourceId, List<String> ids) async {
    calls.add('purgeTrash $sourceId ${ids.join(',')}');
    trash[sourceId]?.removeWhere((t) => ids.contains(t.id));
  }

  @override
  Future<void> emptyTrash(String sourceId) async {
    calls.add('emptyTrash $sourceId');
    trash[sourceId]?.clear();
  }

  @override
  Future<void> openWith(String sourceId, String path) async => calls.add('openWith $sourceId $path');

  @override
  Future<void> share(String sourceId, List<String> paths) async => calls.add('share $sourceId ${paths.join(',')}');

  final transfers = <(String, List<String>, String, String, bool)>[];
  int transferCancels = 0;

  @override
  Future<void> startTransfer(String srcId, List<String> paths, String dstId, String dstDir, bool move) async =>
      transfers.add((srcId, paths, dstId, dstDir, move));

  @override
  Future<void> cancelTransfer() async => transferCancels++;

  final compresses = <(String, List<String>, String, String)>[];
  final extracts = <(String, String, String, String, bool)>[];

  @override
  Future<void> startCompress(String srcId, List<String> paths, String dstDir, String name) async =>
      compresses.add((srcId, paths, dstDir, name));

  @override
  Future<void> startExtract(String srcId, String archive, String dstId, String dstDir, bool intoNewFolder) async =>
      extracts.add((srcId, archive, dstId, dstDir, intoNewFolder));

  /// Réponse de [search] selon la requête ; un completer dans [searchGates] la retarde.
  SearchResultMsg Function(String query) searchResult = (_) => SearchResultMsg(entries: [], truncated: false);
  final searchGates = <String, Completer<void>>{};
  int searchCancels = 0;

  @override
  Future<SearchResultMsg> search(String sourceId, String path, String query, bool showHidden) async {
    calls.add('search $sourceId $path $query');
    final gate = searchGates[query];
    if (gate != null) await gate.future;
    return searchResult(query);
  }

  @override
  Future<void> cancelSearch() async => searchCancels++;

  /// Octets rendus par [thumbnail] selon le chemin (null : pas de vignette).
  final thumbs = <String, Uint8List>{};
  final thumbRequests = <String>[];

  @override
  Future<Uint8List?> thumbnail(String sourceId, String path, int sizePx) async {
    thumbRequests.add(path);
    return thumbs[path];
  }

  ViewPrefsMsg viewPrefs = ViewPrefsMsg(sort: 'name', ascending: true, grid: false, showHidden: false);
  List<FavoriteMsg> favorites = [];

  @override
  Future<ViewPrefsMsg> getViewPrefs() async => viewPrefs;

  @override
  Future<void> setViewPrefs(ViewPrefsMsg prefs) async => viewPrefs = prefs;

  @override
  Future<List<FavoriteMsg>> listFavorites() async => favorites;

  @override
  Future<void> addFavorite(String sourceId, String path, String label) async {
    final key = sources.firstWhere((s) => s.id == sourceId).stableKey;
    if (favorites.any((f) => f.key == key && f.path == path)) return;
    favorites = [...favorites, FavoriteMsg(key: key, path: path, label: label, sourceId: sourceId)];
  }

  @override
  Future<void> removeFavorite(String key, String path) async =>
      favorites = favorites.where((f) => !(f.key == key && f.path == path)).toList();
}
