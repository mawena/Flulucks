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

  @override
  Future<void> deleteEntries(String sourceId, List<String> paths) async =>
      calls.add('delete $sourceId ${paths.join(',')}');

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
}
