import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../state/sorting.dart';
import '../strings_fr.dart';
import '../widgets/clipboard_bar.dart';
import '../widgets/thumb.dart';
import '../widgets/transfer_bar.dart';
import 'search_screen.dart';
import 'space_screen.dart';
import 'trash_screen.dart';

/// Ouvre l'explorateur sur [sourceId] (à la racine, ou sur [path]).
Future<void> openExplorer(BuildContext context, AppState state, String sourceId, {String path = '/'}) =>
    Navigator.of(context).push(MaterialPageRoute<void>(
      builder: (_) => ExplorerScreen(state: state, sourceId: sourceId, path: path),
    ));

String parentOf(String path) {
  final i = path.lastIndexOf('/');
  return i <= 0 ? '/' : path.substring(0, i);
}

String nameOf(String path) => path.substring(path.lastIndexOf('/') + 1);

/// Extensions des archives que le moteur sait extraire (les doubles d'abord).
const _archiveExtensions = ['.tar.gz', '.tar.xz', '.tgz', '.txz', '.zip', '.7z', '.rar', '.tar', '.gz', '.xz'];

/// Archive reconnue à l'extension : un tap propose de l'extraire.
bool isArchiveName(String name) {
  final n = name.toLowerCase();
  return _archiveExtensions.any((e) => n.length > e.length && n.endsWith(e));
}

/// Explorateur d'un stockage : liste ou grille, vignettes, tri, sélection, presse-papiers, copies, archives.
class ExplorerScreen extends StatefulWidget {
  const ExplorerScreen({super.key, required this.state, required this.sourceId, this.path = '/'});

  final AppState state;
  final String sourceId;
  final String path;

  @override
  State<ExplorerScreen> createState() => _ExplorerScreenState();
}

class _ExplorerScreenState extends State<ExplorerScreen> {
  late String _path = widget.path;
  List<EntryMsg> _entries = const [];
  String? _error;
  bool _loading = true;
  final _selected = <String>{};
  final _thumbs = ThumbCache();
  bool _closed = false;
  bool _lastHidden = false;
  int _lastFileIndex = -1;
  bool _lastFinished = false;

  AppState get state => widget.state;
  SourceMsg? get _source => state.sourceById(widget.sourceId);
  bool get _readOnly => _source?.readOnly ?? true;

  /// Un disque sans UUID de partition a une clé liée au branchement (`disk:usb#n`) : un favori ne
  /// s'y retrouverait jamais.
  bool get _canFavorite {
    final s = _source;
    return s != null && !(s.kind == 'disk' && s.stableKey.contains('#'));
  }

  /// Sur un disque chiffré, le nom du dossier resterait visible disque verrouillé : confirmation d'abord.
  Future<void> _addFavorite(String label) async {
    if (_source?.encrypted == true) {
      final ok = await showDialog<bool>(
        context: context,
        builder: (c) => AlertDialog(
          content: const Text(Fr.encryptedFavorite),
          actions: [
            TextButton(onPressed: () => Navigator.pop(c, false), child: const Text(Fr.cancel)),
            FilledButton(onPressed: () => Navigator.pop(c, true), child: const Text(Fr.add)),
          ],
        ),
      );
      if (ok != true || !mounted) return;
    }
    await state.addFavorite(widget.sourceId, _path, label);
  }

  @override
  void initState() {
    super.initState();
    _lastHidden = state.viewPrefs.showHidden;
    state.addListener(_onState);
    _load();
  }

  @override
  void dispose() {
    _thumbs.dispose();
    state.removeListener(_onState);
    super.dispose();
  }

  void _onState() {
    if (!mounted || _closed) return;
    if (_source == null) {
      // stockage disparu (éjection, débranchement, carte retirée) : retour à l'accueil
      _closed = true;
      if (!state.takeClosedNotice(widget.sourceId)) return; // un autre explorateur s'en est chargé
      final messenger = ScaffoldMessenger.maybeOf(context);
      Navigator.of(context).popUntil((r) => r.isFirst);
      messenger?.showSnackBar(const SnackBar(content: Text(Fr.volumeClosed)));
      return;
    }
    if (state.viewPrefs.showHidden != _lastHidden) {
      _lastHidden = state.viewPrefs.showHidden;
      _load();
    }
    final t = state.transfer;
    if (t != null && state.transferDestination == (widget.sourceId, _path)) {
      if (t.fileIndex != _lastFileIndex || t.finished != _lastFinished) {
        _lastFileIndex = t.fileIndex;
        _lastFinished = t.finished;
        _load(quiet: true);
      }
    }
    setState(() {});
  }

  Future<void> _load({bool quiet = false}) async {
    final path = _path;
    if (!quiet) setState(() => _loading = true);
    try {
      final l = await state.api.listDir(widget.sourceId, path, state.viewPrefs.showHidden);
      if (!mounted || path != _path) return;
      setState(() {
        _entries = l;
        _error = null;
        _loading = false;
        _selected.retainWhere((p) => l.any((e) => e.path == p)); // éléments disparus entre-temps
      });
    } on PlatformException catch (e) {
      if (!mounted || path != _path) return;
      setState(() {
        _entries = const [];
        _error = e.message ?? Fr.genericError;
        _loading = false;
      });
    }
  }

  void _go(String path) {
    setState(() {
      _path = path;
      _selected.clear();
      _entries = const [];
    });
    _load();
  }

  Future<void> _run(Future<void> Function() action) async {
    try {
      await action();
    } on PlatformException catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(e.message ?? Fr.genericError)));
      }
    }
  }

  void _setPrefs({String? sort, bool? ascending, bool? grid, bool? showHidden}) {
    final v = state.viewPrefs;
    state.setViewPrefs(ViewPrefsMsg(
      sort: sort ?? v.sort,
      ascending: ascending ?? v.ascending,
      grid: grid ?? v.grid,
      showHidden: showHidden ?? v.showHidden,
    ));
  }

  void _tap(EntryMsg e) {
    if (_selected.isNotEmpty) return _toggle(e);
    if (e.type == 'dir') return _go(e.path);
    if (isArchiveName(e.name)) {
      _archiveSheet(e);
      return;
    }
    _run(() => state.api.openWith(widget.sourceId, e.path));
  }

  /// Archive touchée : Extraire ici (nouveau dossier au nom de l'archive), Extraire vers… (dossier choisi
  /// ensuite, comme un collage), ou Ouvrir avec….
  Future<void> _archiveSheet(EntryMsg e) async {
    final choice = await showModalBottomSheet<String>(
      context: context,
      builder: (ctx) => SafeArea(
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          ListTile(
            leading: const Icon(Icons.unarchive),
            title: const Text(Fr.extractHere),
            enabled: !_readOnly,
            onTap: () => Navigator.pop(ctx, 'here'),
          ),
          ListTile(
            leading: const Icon(Icons.drive_file_move),
            title: const Text(Fr.extractTo),
            onTap: () => Navigator.pop(ctx, 'to'),
          ),
          ListTile(
            leading: const Icon(Icons.open_in_new),
            title: const Text(Fr.openWith),
            onTap: () => Navigator.pop(ctx, 'open'),
          ),
        ]),
      ),
    );
    if (!mounted) return;
    switch (choice) {
      case 'here':
        await state.startExtract(widget.sourceId, e.path, widget.sourceId, _path, intoNewFolder: true);
        _showStateError();
      case 'to':
        state.setPendingExtract(widget.sourceId, e.path);
      case 'open':
        await _run(() => state.api.openWith(widget.sourceId, e.path));
    }
  }

  /// « Extraire ici » de la barre : l'archive choisie par « Extraire vers… » va dans le dossier ouvert.
  Future<void> _extractPending() async {
    final x = state.pendingExtract;
    if (x == null) return;
    await state.startExtract(x.sourceId, x.path, widget.sourceId, _path, intoNewFolder: true);
    if (_showStateError()) return;
    state.clearPendingExtract(); // extraction lancée : la barre disparaît
  }

  /// Montre l'erreur de la dernière action de l'état (opération refusée…) ; vrai s'il y en avait une.
  bool _showStateError() {
    final err = state.lastError;
    if (err == null) return false;
    if (mounted) ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(err)));
    state.clearError();
    return true;
  }

  void _toggle(EntryMsg e) => setState(() => _selected.contains(e.path) ? _selected.remove(e.path) : _selected.add(e.path));

  Future<String?> _askName(String title, String label, {String initial = ''}) => showDialog<String>(
        context: context,
        builder: (ctx) {
          final c = TextEditingController(text: initial);
          return AlertDialog(
            title: Text(title),
            content: TextField(
              controller: c,
              autofocus: true,
              decoration: InputDecoration(labelText: label),
              onSubmitted: (v) => Navigator.pop(ctx, v),
            ),
            actions: [
              TextButton(onPressed: () => Navigator.pop(ctx), child: const Text(Fr.cancel)),
              FilledButton(onPressed: () => Navigator.pop(ctx, c.text), child: const Text(Fr.ok)),
            ],
          );
        },
      );

  Future<void> _newFolder() async {
    final name = (await _askName(Fr.newFolder, Fr.folderName))?.trim();
    if (name == null || name.isEmpty) return;
    await _run(() async {
      await state.api.createFolder(widget.sourceId, _path, name);
      await _load(quiet: true);
    });
  }

  Future<void> _rename() async {
    final path = _selected.single;
    final name = (await _askName(Fr.rename, Fr.newName, initial: nameOf(path)))?.trim();
    if (name == null || name.isEmpty || name == nameOf(path)) return;
    await _run(() async {
      await state.api.rename(widget.sourceId, path, name);
      setState(_selected.clear);
      await _load(quiet: true);
    });
  }

  /// Supprimer : à la corbeille du stockage, ou définitivement au choix ; dans la corbeille, toujours définitif.
  Future<void> _delete() async {
    final paths = _selected.toList();
    final inTrash = isInTrash(_path);
    final permanent = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(paths.length == 1 ? Fr.deleteOne(nameOf(paths.single)) : Fr.deleteMany(paths.length)),
        content: Text(inTrash ? Fr.deleteBody : Fr.deleteToTrashBody),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text(Fr.cancel)),
          if (!inTrash) TextButton(onPressed: () => Navigator.pop(ctx, true), child: const Text(Fr.deletePermanently)),
          FilledButton(onPressed: () => Navigator.pop(ctx, inTrash), child: const Text(Fr.delete)),
        ],
      ),
    );
    if (permanent == null) return;
    await _run(() async {
      await state.api.deleteEntries(widget.sourceId, paths, permanent);
      setState(_selected.clear);
      await _load(quiet: true);
    });
  }

  void _toClipboard({required bool move}) {
    state.setClipboard(widget.sourceId, _selected.toList(), move: move);
    setState(_selected.clear);
  }

  Future<void> _paste() async {
    final c = state.clipboard;
    if (c == null) return;
    await state.startTransfer(c.sourceId, c.paths, widget.sourceId, _path, move: c.move);
    if (_showStateError()) return;
    state.clearClipboard(); // copie ou déplacement lancé : « Coller ici » disparaît
  }

  /// Compresse la sélection dans un ZIP du dossier ouvert ; nom proposé : l'élément seul (sans son extension
  /// pour un fichier) ou « Archive ».
  Future<void> _compress() async {
    final paths = _selected.toList();
    var proposed = Fr.archiveDefault;
    if (paths.length == 1) {
      final n = nameOf(paths.single);
      final dot = n.lastIndexOf('.');
      final isFile = _entries.any((e) => e.path == paths.single && e.type == 'file');
      proposed = isFile && dot > 0 ? n.substring(0, dot) : n;
    }
    var name = (await _askName(Fr.compress, Fr.archiveName, initial: proposed))?.trim();
    if (name == null || name.isEmpty) return;
    if (!name.toLowerCase().endsWith('.zip')) name = '$name.zip';
    await state.startCompress(widget.sourceId, paths, _path, name);
    if (_showStateError()) return;
    setState(_selected.clear);
  }

  String _subtitle(EntryMsg e) {
    final date = e.mtime > 0 ? Fr.shortDate(e.mtime) : '';
    if (e.type != 'file') return date;
    return date.isEmpty ? Fr.humanSize(e.size) : '${Fr.humanSize(e.size)} · $date';
  }

  PreferredSizeWidget _appBar() {
    if (_selected.isNotEmpty) {
      final byPath = {for (final e in _entries) e.path: e};
      final hasDir = _selected.any((p) => byPath[p]?.type != 'file');
      return AppBar(
        leading: IconButton(icon: const Icon(Icons.close), onPressed: () => setState(_selected.clear)),
        title: Text(Fr.selected(_selected.length)),
        actions: [
          IconButton(tooltip: Fr.copy, icon: const Icon(Icons.copy), onPressed: () => _toClipboard(move: false)),
          IconButton(
              tooltip: Fr.cut, icon: const Icon(Icons.content_cut), onPressed: _readOnly ? null : () => _toClipboard(move: true)),
          IconButton(
              tooltip: Fr.compress, icon: const Icon(Icons.folder_zip), onPressed: _readOnly ? null : _compress),
          IconButton(
            tooltip: Fr.share,
            icon: const Icon(Icons.share),
            onPressed: hasDir ? null : () => _run(() => state.api.share(widget.sourceId, _selected.toList())),
          ),
          IconButton(
              tooltip: Fr.rename,
              icon: const Icon(Icons.drive_file_rename_outline),
              onPressed: _readOnly || _selected.length != 1 ? null : _rename),
          IconButton(tooltip: Fr.delete, icon: const Icon(Icons.delete), onPressed: _readOnly ? null : _delete),
        ],
      );
    }
    final v = state.viewPrefs;
    final fav = state.favoriteFor(widget.sourceId, _path);
    final label = _source?.label ?? '';
    return AppBar(
      title: Text(_path == '/' ? label : nameOf(_path)),
      actions: [
        IconButton(
          tooltip: Fr.search,
          icon: const Icon(Icons.search),
          onPressed: () => Navigator.of(context).push(MaterialPageRoute<void>(
            builder: (_) => SearchScreen(state: state, sourceId: widget.sourceId, path: _path),
          )),
        ),
        IconButton(
          tooltip: fav == null ? Fr.addFavorite : Fr.removeFavorite,
          icon: Icon(fav == null ? Icons.star_border : Icons.star),
          onPressed: fav != null
              ? () => state.removeFavorite(fav.key, fav.path)
              : _canFavorite
                  ? () => _addFavorite(_path == '/' ? label : nameOf(_path))
                  : null,
        ),
        PopupMenuButton<String>(
          tooltip: Fr.sortBy,
          onSelected: (k) => switch (k) {
            'name' || 'size' || 'date' => _setPrefs(sort: k),
            'asc' => _setPrefs(ascending: true),
            'desc' => _setPrefs(ascending: false),
            'grid' => _setPrefs(grid: !v.grid),
            'trash' => openTrash(context, state, widget.sourceId),
            'space' => openSpace(context, state, widget.sourceId),
            _ => _setPrefs(showHidden: !v.showHidden),
          },
          itemBuilder: (_) => [
            CheckedPopupMenuItem(value: 'name', checked: v.sort == 'name', child: const Text(Fr.sortName)),
            CheckedPopupMenuItem(value: 'size', checked: v.sort == 'size', child: const Text(Fr.sortSize)),
            CheckedPopupMenuItem(value: 'date', checked: v.sort == 'date', child: const Text(Fr.sortDate)),
            const PopupMenuDivider(),
            CheckedPopupMenuItem(value: 'asc', checked: v.ascending, child: const Text(Fr.ascending)),
            CheckedPopupMenuItem(value: 'desc', checked: !v.ascending, child: const Text(Fr.descending)),
            const PopupMenuDivider(),
            PopupMenuItem(value: 'grid', child: Text(v.grid ? Fr.list : Fr.grid)),
            CheckedPopupMenuItem(value: 'hidden', checked: v.showHidden, child: const Text(Fr.showHidden)),
            if (_path == '/') ...[
              const PopupMenuDivider(),
              const PopupMenuItem(value: 'trash', child: Text(Fr.trash)),
              const PopupMenuItem(value: 'space', child: Text(Fr.spaceAnalysis)),
            ],
          ],
        ),
      ],
    );
  }

  Widget _body() {
    if (_loading && _entries.isEmpty) return const Center(child: CircularProgressIndicator());
    final v = state.viewPrefs;
    final entries = sortEntries(_entries, v.sort, v.ascending);
    final Widget content;
    if (_error != null) {
      content = ListView(children: [
        Padding(
          padding: const EdgeInsets.all(32),
          child: Center(child: Text(_error!, textAlign: TextAlign.center)),
        ),
      ]);
    } else if (entries.isEmpty) {
      content = ListView(children: const [Padding(padding: EdgeInsets.all(32), child: Center(child: Text(Fr.emptyFolder)))]);
    } else if (v.grid) {
      content = GridView.builder(
        padding: const EdgeInsets.all(8),
        gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(crossAxisCount: 3, mainAxisSpacing: 8, crossAxisSpacing: 8),
        itemCount: entries.length,
        itemBuilder: (_, i) => _gridTile(entries[i]),
      );
    } else {
      content = ListView.builder(itemCount: entries.length, itemBuilder: (_, i) => _listTile(entries[i]));
    }
    return RefreshIndicator(onRefresh: () => _load(quiet: true), child: content);
  }

  Widget _listTile(EntryMsg e) => ListTile(
        key: ValueKey(e.path),
        selected: _selected.contains(e.path),
        leading: EntryThumb(state: state, cache: _thumbs, sourceId: widget.sourceId, entry: e),
        title: Text(e.name, maxLines: 1, overflow: TextOverflow.ellipsis),
        subtitle: Text(_subtitle(e)),
        trailing: _selected.contains(e.path) ? const Icon(Icons.check_circle) : null,
        onTap: () => _tap(e),
        onLongPress: () => _toggle(e),
      );

  Widget _gridTile(EntryMsg e) {
    final sel = _selected.contains(e.path);
    final theme = Theme.of(context);
    return InkWell(
      key: ValueKey(e.path),
      onTap: () => _tap(e),
      onLongPress: () => _toggle(e),
      child: Container(
        decoration: BoxDecoration(
          color: sel ? theme.colorScheme.primaryContainer : null,
          borderRadius: BorderRadius.circular(8),
        ),
        padding: const EdgeInsets.all(4),
        child: Column(children: [
          Expanded(
            child: LayoutBuilder(
              builder: (_, c) => EntryThumb(
                  state: state, cache: _thumbs, sourceId: widget.sourceId, entry: e, size: c.biggest.shortestSide),
            ),
          ),
          Text(e.name, maxLines: 1, overflow: TextOverflow.ellipsis, style: theme.textTheme.bodySmall),
        ]),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final src = _source;
    return PopScope(
      canPop: _path == '/' && _selected.isEmpty,
      onPopInvokedWithResult: (didPop, _) {
        if (didPop) return;
        if (_selected.isNotEmpty) return setState(_selected.clear);
        _go(parentOf(_path));
      },
      child: Scaffold(
        appBar: _appBar(),
        body: Column(children: [
          if (src != null && src.readOnly)
            MaterialBanner(content: Text(Fr.readOnly(src.roReason)), actions: const [SizedBox.shrink()]),
          Expanded(child: _body()),
        ]),
        // sous le bouton flottant, qui ne les masque pas
        bottomNavigationBar: Column(mainAxisSize: MainAxisSize.min, children: [
          ClipboardBar(state: state, canPaste: !_readOnly, onPaste: _paste, onExtract: _extractPending),
          TransferBar(state: state),
        ]),
        floatingActionButton: _readOnly || _selected.isNotEmpty
            ? null
            : FloatingActionButton(
                heroTag: null, // plusieurs explorateurs peuvent être empilés
                tooltip: Fr.newFolder,
                onPressed: _newFolder,
                child: const Icon(Icons.create_new_folder),
              ),
      ),
    );
  }
}
