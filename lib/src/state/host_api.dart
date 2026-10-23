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
  Future<int> getAutoLockMinutes();
  Future<void> setAutoLockMinutes(int minutes);
  Future<List<SourceMsg>> listSources();
  Future<bool> hasAllFilesAccess();
  Future<void> requestAllFilesAccess();
  Future<List<EntryMsg>> listDir(String sourceId, String path, bool showHidden);
  Future<String> createFolder(String sourceId, String parent, String name);
  Future<String> rename(String sourceId, String path, String newName);
  Future<void> deleteEntries(String sourceId, List<String> paths, bool permanent);
  Future<List<TrashItemMsg>> listTrash(String sourceId);
  Future<void> restoreTrash(String sourceId, List<String> ids);
  Future<void> purgeTrash(String sourceId, List<String> ids);
  Future<void> emptyTrash(String sourceId);
  Future<void> openWith(String sourceId, String path);
  Future<void> playVideo(String sourceId, String path);
  Future<void> share(String sourceId, List<String> paths);
  Future<void> startTransfer(String srcId, List<String> paths, String dstId, String dstDir, bool move);
  Future<void> startCompress(String srcId, List<String> paths, String dstDir, String name);
  Future<void> startExtract(String srcId, String archive, String dstId, String dstDir, bool intoNewFolder);
  Future<void> cancelTransfer();
  Future<SearchResultMsg> search(String sourceId, String path, String query, bool showHidden);
  Future<void> cancelSearch();
  Future<SpaceReportMsg> analyzeSpace(String sourceId, String path);
  Future<SpaceReportMsg> spaceChildren(String sourceId, String path);
  Future<void> cancelAnalysis();
  Future<DetailsMsg> details(String sourceId, String path);
  Future<FolderSizeMsg> folderSize(String sourceId, String path);
  Future<String?> sha256(String sourceId, String path);
  Future<void> cancelHash();
  Future<List<RemoteServerMsg>> listServers();
  Future<RemoteServerMsg> saveServer(RemoteServerMsg server, Uint8List? password, Uint8List? keyPassphrase);
  Future<void> deleteServer(String serverId);
  Future<SourceMsg> connectServer(String serverId, Uint8List? password);
  Future<void> disconnectServer(String serverId);
  Future<void> trustHostKey(String serverId, String fingerprint);
  Future<void> forgetHostKey(String serverId);
  Future<Uint8List> readImage(String sourceId, String path);
  Future<TextFileMsg> readText(String sourceId, String path);
  Future<TextSaveMsg> writeText(String sourceId, String path, String text, bool bom, bool crlf, int expectedMtime,
      int expectedSize, bool force);
  Future<List<FilterItemMsg>> quickFilter(String filter);
  Future<List<RenamePreviewMsg>> previewRename(String sourceId, List<String> paths, RenameRuleMsg rule);
  Future<void> applyRename(String sourceId, List<String> paths, RenameRuleMsg rule);
  Future<Uint8List?> thumbnail(String sourceId, String path, int sizePx);
  Future<ViewPrefsMsg> getViewPrefs();
  Future<void> setViewPrefs(ViewPrefsMsg prefs);
  Future<List<FavoriteMsg>> listFavorites();
  Future<void> addFavorite(String sourceId, String path, String label);
  Future<void> removeFavorite(String key, String path);
  Future<void> formatDisk(String diskId, String fs, bool encrypted, String label, Uint8List? passphrase);
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
  Future<int> getAutoLockMinutes() => _api.getAutoLockMinutes();
  @override
  Future<void> setAutoLockMinutes(int minutes) => _api.setAutoLockMinutes(minutes);
  @override
  Future<List<SourceMsg>> listSources() => _api.listSources();
  @override
  Future<bool> hasAllFilesAccess() => _api.hasAllFilesAccess();
  @override
  Future<void> requestAllFilesAccess() => _api.requestAllFilesAccess();
  @override
  Future<List<EntryMsg>> listDir(String sourceId, String path, bool showHidden) =>
      _api.listDir(sourceId, path, showHidden);
  @override
  Future<String> createFolder(String sourceId, String parent, String name) =>
      _api.createFolder(sourceId, parent, name);
  @override
  Future<String> rename(String sourceId, String path, String newName) => _api.rename(sourceId, path, newName);
  @override
  Future<void> deleteEntries(String sourceId, List<String> paths, bool permanent) =>
      _api.deleteEntries(sourceId, paths, permanent);
  @override
  Future<List<TrashItemMsg>> listTrash(String sourceId) => _api.listTrash(sourceId);
  @override
  Future<void> restoreTrash(String sourceId, List<String> ids) => _api.restoreTrash(sourceId, ids);
  @override
  Future<void> purgeTrash(String sourceId, List<String> ids) => _api.purgeTrash(sourceId, ids);
  @override
  Future<void> emptyTrash(String sourceId) => _api.emptyTrash(sourceId);
  @override
  Future<void> openWith(String sourceId, String path) => _api.openWith(sourceId, path);
  @override
  Future<void> playVideo(String sourceId, String path) => _api.playVideo(sourceId, path);
  @override
  Future<void> share(String sourceId, List<String> paths) => _api.share(sourceId, paths);
  @override
  Future<void> startTransfer(String srcId, List<String> paths, String dstId, String dstDir, bool move) =>
      _api.startTransfer(srcId, paths, dstId, dstDir, move);
  @override
  Future<void> startCompress(String srcId, List<String> paths, String dstDir, String name) =>
      _api.startCompress(srcId, paths, dstDir, name);
  @override
  Future<void> startExtract(String srcId, String archive, String dstId, String dstDir, bool intoNewFolder) =>
      _api.startExtract(srcId, archive, dstId, dstDir, intoNewFolder);
  @override
  Future<void> cancelTransfer() => _api.cancelTransfer();
  @override
  Future<SearchResultMsg> search(String sourceId, String path, String query, bool showHidden) =>
      _api.search(sourceId, path, query, showHidden);
  @override
  Future<void> cancelSearch() => _api.cancelSearch();
  @override
  Future<SpaceReportMsg> analyzeSpace(String sourceId, String path) => _api.analyzeSpace(sourceId, path);
  @override
  Future<SpaceReportMsg> spaceChildren(String sourceId, String path) => _api.spaceChildren(sourceId, path);
  @override
  Future<void> cancelAnalysis() => _api.cancelAnalysis();
  @override
  Future<DetailsMsg> details(String sourceId, String path) => _api.details(sourceId, path);
  @override
  Future<FolderSizeMsg> folderSize(String sourceId, String path) => _api.folderSize(sourceId, path);
  @override
  Future<String?> sha256(String sourceId, String path) => _api.sha256(sourceId, path);
  @override
  Future<void> cancelHash() => _api.cancelHash();
  @override
  Future<List<RemoteServerMsg>> listServers() => _api.listServers();
  @override
  Future<RemoteServerMsg> saveServer(RemoteServerMsg server, Uint8List? password, Uint8List? keyPassphrase) =>
      _api.saveServer(server, password, keyPassphrase);
  @override
  Future<void> deleteServer(String serverId) => _api.deleteServer(serverId);
  @override
  Future<SourceMsg> connectServer(String serverId, Uint8List? password) => _api.connectServer(serverId, password);
  @override
  Future<void> disconnectServer(String serverId) => _api.disconnectServer(serverId);
  @override
  Future<void> trustHostKey(String serverId, String fingerprint) => _api.trustHostKey(serverId, fingerprint);
  @override
  Future<void> forgetHostKey(String serverId) => _api.forgetHostKey(serverId);
  @override
  Future<Uint8List> readImage(String sourceId, String path) => _api.readImage(sourceId, path);
  @override
  Future<TextFileMsg> readText(String sourceId, String path) => _api.readText(sourceId, path);
  @override
  Future<TextSaveMsg> writeText(String sourceId, String path, String text, bool bom, bool crlf, int expectedMtime,
          int expectedSize, bool force) =>
      _api.writeText(sourceId, path, text, bom, crlf, expectedMtime, expectedSize, force);
  @override
  Future<List<FilterItemMsg>> quickFilter(String filter) => _api.quickFilter(filter);
  @override
  Future<List<RenamePreviewMsg>> previewRename(String sourceId, List<String> paths, RenameRuleMsg rule) =>
      _api.previewRename(sourceId, paths, rule);
  @override
  Future<void> applyRename(String sourceId, List<String> paths, RenameRuleMsg rule) =>
      _api.applyRename(sourceId, paths, rule);
  @override
  Future<Uint8List?> thumbnail(String sourceId, String path, int sizePx) => _api.thumbnail(sourceId, path, sizePx);
  @override
  Future<ViewPrefsMsg> getViewPrefs() => _api.getViewPrefs();
  @override
  Future<void> setViewPrefs(ViewPrefsMsg prefs) => _api.setViewPrefs(prefs);
  @override
  Future<List<FavoriteMsg>> listFavorites() => _api.listFavorites();
  @override
  Future<void> addFavorite(String sourceId, String path, String label) => _api.addFavorite(sourceId, path, label);
  @override
  Future<void> removeFavorite(String key, String path) => _api.removeFavorite(key, path);
  @override
  Future<void> formatDisk(String diskId, String fs, bool encrypted, String label, Uint8List? passphrase) =>
      _api.formatDisk(diskId, fs, encrypted, label, passphrase);
}
