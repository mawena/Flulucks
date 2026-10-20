import 'dart:typed_data';

import '../bridge/fluluks_api.g.dart';

/// Interface de l'API hôte, substituable dans les tests.
abstract class HostApi {
  Future<List<DiskMsg>> listDisks();
  Future<List<VolumeMsg>> listVolumes();
  Future<void> requestPermission(String diskId);
  Future<VolumeMsg> unlock(String diskId, int partIndex, Uint8List passphrase, bool writable);
  Future<VolumeMsg> mountPlain(String diskId, int partIndex, bool writable);
  Future<void> eject(String volumeId);
  Future<void> openInFiles(String volumeId);
  Future<bool> getReadOnlyPreference();
  Future<void> setReadOnlyPreference(bool value);
  Future<List<SourceMsg>> listSources();
  Future<bool> hasAllFilesAccess();
  Future<void> requestAllFilesAccess();
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
  Future<VolumeMsg> unlock(String diskId, int partIndex, Uint8List passphrase, bool writable) =>
      _api.unlock(diskId, partIndex, passphrase, writable);
  @override
  Future<VolumeMsg> mountPlain(String diskId, int partIndex, bool writable) =>
      _api.mountPlain(diskId, partIndex, writable);
  @override
  Future<void> eject(String volumeId) => _api.eject(volumeId);
  @override
  Future<void> openInFiles(String volumeId) => _api.openInFiles(volumeId);
  @override
  Future<bool> getReadOnlyPreference() => _api.getReadOnlyPreference();
  @override
  Future<void> setReadOnlyPreference(bool value) => _api.setReadOnlyPreference(value);
  @override
  Future<List<SourceMsg>> listSources() => _api.listSources();
  @override
  Future<bool> hasAllFilesAccess() => _api.hasAllFilesAccess();
  @override
  Future<void> requestAllFilesAccess() => _api.requestAllFilesAccess();
}
