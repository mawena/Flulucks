import 'dart:typed_data';

import '../bridge/fluluks_api.g.dart';

/// Interface de l'API hôte, substituable dans les tests.
abstract class HostApi {
  Future<List<DiskMsg>> listDisks();
  Future<List<VolumeMsg>> listVolumes();
  Future<void> requestPermission(String diskId);
  Future<VolumeMsg> unlock(String diskId, int partIndex, Uint8List passphrase);
  Future<VolumeMsg> mountPlain(String diskId, int partIndex);
  Future<void> eject(String volumeId);
  Future<void> openInFiles(String volumeId);
}

/// Implémentation réelle : délègue au client Pigeon généré.
class PigeonHostApi implements HostApi {
  PigeonHostApi([FluLuksHostApi? api]) : _api = api ?? FluLuksHostApi();
  final FluLuksHostApi _api;

  @override
  Future<List<DiskMsg>> listDisks() => _api.listDisks();
  @override
  Future<List<VolumeMsg>> listVolumes() => _api.listVolumes();
  @override
  Future<void> requestPermission(String diskId) => _api.requestPermission(diskId);
  @override
  Future<VolumeMsg> unlock(String diskId, int partIndex, Uint8List passphrase) =>
      _api.unlock(diskId, partIndex, passphrase);
  @override
  Future<VolumeMsg> mountPlain(String diskId, int partIndex) => _api.mountPlain(diskId, partIndex);
  @override
  Future<void> eject(String volumeId) => _api.eject(volumeId);
  @override
  Future<void> openInFiles(String volumeId) => _api.openInFiles(volumeId);
}
