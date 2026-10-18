
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../strings_fr.dart';
import 'host_api.dart';

/// État de l'interface : copie de ce que le côté Kotlin publie. Ne contient jamais de phrase de passe.
class AppState extends ChangeNotifier implements FluLuksEventsApi {
  /// [api] : l'API hôte (ou un faux dans les tests).
  AppState(this.api);

  final HostApi api;

  List<DiskMsg> _disks = const [];
  List<VolumeMsg> _volumes = const [];
  String? _lastError;
  bool _readOnlyPreference = false;
  bool _ejecting = false;
  String? _ejectError;

  List<DiskMsg> get disks => _disks;
  List<VolumeMsg> get volumes => _volumes;

  /// « Ouvrir les disques en lecture seule » : tout nouveau montage se fait alors sans écriture.
  bool get readOnlyPreference => _readOnlyPreference;

  /// Éjection en cours (données en attente écrites, démontage) : les boutons sont désactivés.
  bool get ejecting => _ejecting;

  /// « Éjection impossible : … », ou null.
  String? get ejectError => _ejectError;

  /// Dernière erreur d'action (affichée en bandeau), ou null.
  String? get lastError => _lastError;

  void clearError() {
    _lastError = null;
    notifyListeners();
  }

  VolumeMsg? volumeById(String id) {
    for (final v in _volumes) {
      if (v.id == id) return v;
    }
    return null;
  }

  /// Chargement initial (Kotlin pousse ensuite chaque changement via [onStateChanged]).
  Future<void> refresh() async {
    try {
      _disks = await api.listDisks();
      _volumes = await api.listVolumes();
      _readOnlyPreference = await api.getReadOnlyPreference();
      notifyListeners();
    } on PlatformException catch (e) {
      _fail(e);
    }
  }

  @override
  void onStateChanged(List<DiskMsg> disks, List<VolumeMsg> volumes) {
    _disks = disks;
    _volumes = volumes;
    notifyListeners();
  }

  @override
  void onEjectStateChanged(bool ejecting, String? error) {
    _ejecting = ejecting;
    _ejectError = error;
    notifyListeners();
  }

  Future<void> setReadOnlyPreference(bool value) => _guard(() async {
        await api.setReadOnlyPreference(value);
        _readOnlyPreference = value;
        notifyListeners();
      });

  Future<void> requestPermission(String diskId) => _guard(() => api.requestPermission(diskId));

  Future<void> mountPlain(String diskId, int partIndex) =>
      _guard(() async {
        await api.mountPlain(diskId, partIndex, !_readOnlyPreference);
        await refresh();
      });

  Future<void> eject(String volumeId) => _guard(() => api.eject(volumeId));

  Future<void> openInFiles(String volumeId) => _guard(() => api.openInFiles(volumeId));

  /// Lève [PlatformException] en cas d'échec (message français, jamais de phrase de passe).
  /// L'appelant garde la responsabilité de mettre [passphrase] à zéro. [writable] : écriture demandée
  /// (le moteur peut la refuser : le volume dit alors pourquoi il est en lecture seule).
  Future<void> unlock(String diskId, int partIndex, Uint8List passphrase, {required bool writable}) async {
    await api.unlock(diskId, partIndex, passphrase, writable);
    await refresh();
  }

  Future<void> _guard(Future<void> Function() action) async {
    try {
      _lastError = null;
      await action();
    } on PlatformException catch (e) {
      _fail(e);
    }
  }

  void _fail(PlatformException e) {
    _lastError = e.message ?? Fr.genericError;
    notifyListeners();
  }
}
