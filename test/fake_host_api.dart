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

VolumeMsg volume({String id = 'v1'}) => VolumeMsg(
      id: id,
      diskId: 'd1',
      partIndex: 1,
      title: 'Données',
      totalBytes: 1000,
      freeBytes: 500,
      needsRecovery: false,
      roReason: '',
    );

/// Faux pont : enregistre les appels. Les octets de phrase de passe sont copiés à l'appel, et la
/// référence d'origine est gardée pour vérifier qu'elle est mise à zéro ensuite.
class FakeHostApi implements HostApi {
  List<DiskMsg> disks = [];
  List<VolumeMsg> volumes = [];
  final ejected = <String>[];
  final mounted = <(String, int)>[];
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
  Future<VolumeMsg> unlock(String diskId, int partIndex, Uint8List passphrase) async {
    passphraseRef = passphrase;
    passphraseCopy = List<int>.of(passphrase);
    if (gate != null) await gate!.future;
    if (utf8.decode(passphraseCopy!) != correctPassphrase) {
      throw PlatformException(code: '-5', message: 'phrase de passe incorrecte');
    }
    return volume();
  }

  @override
  Future<VolumeMsg> mountPlain(String diskId, int partIndex) async {
    mounted.add((diskId, partIndex));
    return volume();
  }

  @override
  Future<void> eject(String volumeId) async => ejected.add(volumeId);

  @override
  Future<void> openInFiles(String volumeId) async {}
}
