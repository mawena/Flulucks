
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
  List<SourceMsg> _sources = const [];
  bool _allFilesAccess = false;
  TransferMsg? _transfer;
  ViewPrefsMsg _viewPrefs = ViewPrefsMsg(sort: 'name', ascending: true, grid: false, showHidden: false);
  List<FavoriteMsg> _favorites = const [];
  ClipboardItems? _clipboard;
  (String, String)? _transferDestination;

  List<DiskMsg> get disks => _disks;
  List<VolumeMsg> get volumes => _volumes;

  /// Stockages du gestionnaire de fichiers : téléphone, cartes SD, volumes ouverts.
  List<SourceMsg> get sources => _sources;

  /// Tri, grille et fichiers cachés (persistés côté Kotlin).
  ViewPrefsMsg get viewPrefs => _viewPrefs;

  /// Favoris, résolus vers les stockages ouverts (`sourceId` null sinon).
  List<FavoriteMsg> get favorites => _favorites;

  FavoriteMsg? favoriteFor(String sourceId, String path) {
    final key = sourceById(sourceId)?.stableKey;
    for (final f in _favorites) {
      if (f.key == key && f.path == path) return f;
    }
    return null;
  }

  /// Éléments copiés ou coupés, en attente de « Coller ici » ; null si vide.
  ClipboardItems? get clipboard => _clipboard;

  void setClipboard(String sourceId, List<String> paths, {required bool move}) {
    _clipboard = ClipboardItems(sourceId, List.unmodifiable(paths), move);
    notifyListeners();
  }

  void clearClipboard() {
    _clipboard = null;
    notifyListeners();
  }

  /// (source, dossier) où écrit la dernière copie lancée : l'explorateur qui l'affiche se rafraîchit.
  (String, String)? get transferDestination => _transferDestination;

  /// Copie ou déplacement en cours, ou le dernier terminé ; null si aucun.
  TransferMsg? get transfer => _transfer;

  /// Accès à tous les fichiers du téléphone accordé.
  bool get allFilesAccess => _allFilesAccess;

  SourceMsg? sourceById(String id) {
    for (final s in _sources) {
      if (s.id == id) return s;
    }
    return null;
  }

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
      _sources = await api.listSources();
      _allFilesAccess = await api.hasAllFilesAccess();
      _viewPrefs = await api.getViewPrefs();
      _favorites = await api.listFavorites();
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
    refreshSources(); // un volume ouvert ou fermé ajoute ou retire un stockage
  }

  /// Relit les stockages, l'accès aux fichiers et les favoris (changement d'état, retour au premier plan).
  Future<void> refreshSources() async {
    try {
      _sources = await api.listSources();
      _allFilesAccess = await api.hasAllFilesAccess();
      _favorites = await api.listFavorites();
      notifyListeners();
    } on PlatformException catch (e) {
      _fail(e);
    }
  }

  Future<void> requestAllFilesAccess() => _guard(() => api.requestAllFilesAccess());

  @override
  void onEjectStateChanged(bool ejecting, String? error) {
    _ejecting = ejecting;
    _ejectError = error;
    notifyListeners();
  }

  @override
  void onTransferChanged(TransferMsg? transfer) {
    _transfer = transfer;
    notifyListeners();
  }

  Future<void> startTransfer(String srcId, List<String> paths, String dstId, String dstDir, {required bool move}) =>
      _guard(() async {
        await api.startTransfer(srcId, paths, dstId, dstDir, move);
        _transferDestination = (dstId, dstDir);
      });

  Future<void> cancelTransfer() => _guard(() => api.cancelTransfer());

  Future<void> setViewPrefs(ViewPrefsMsg prefs) => _guard(() async {
        await api.setViewPrefs(prefs);
        _viewPrefs = prefs;
        notifyListeners();
      });

  Future<void> addFavorite(String sourceId, String path, String label) => _guard(() async {
        await api.addFavorite(sourceId, path, label);
        _favorites = await api.listFavorites();
        notifyListeners();
      });

  Future<void> removeFavorite(String key, String path) => _guard(() async {
        await api.removeFavorite(key, path);
        _favorites = await api.listFavorites();
        notifyListeners();
      });

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

/// Contenu du presse-papiers de l'explorateur.
class ClipboardItems {
  const ClipboardItems(this.sourceId, this.paths, this.move);

  final String sourceId;
  final List<String> paths;
  final bool move;
}
