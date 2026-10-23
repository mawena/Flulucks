import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../state/prefs.dart';
import '../file_types.dart';
import '../state/sorting.dart';
import '../strings_fr.dart';
import '../ui/dialogs.dart';
import '../ui/feedback.dart';
import '../ui/sheets.dart';
import '../ui/states.dart';
import '../ui/tokens.dart';
import '../ui/widgets.dart';
import '../viewers.dart';
import '../widgets/bottom_dock.dart';
import '../widgets/breadcrumbs.dart';
import '../widgets/clipboard_bar.dart';
import '../widgets/entry_collection.dart';
import '../widgets/properties_sheet.dart';
import '../widgets/thumb.dart';
import 'batch_rename_screen.dart';
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

/// Explorateur d'un stockage : liste ou grille adaptative, vignettes, fil d'Ariane, tri, sélection, menu par élément,
/// presse-papiers, copies, archives, corbeille avec « Annuler ».
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

  /// Rechargement discret (après une copie, un renommage) : barre linéaire en haut, contenu gardé.
  bool _refreshing = false;
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
      // stockage disparu (éjection, débranchement, carte retirée) : retour à l'accueil, une fois quitté l'éditeur
      // qui garde un texte non enregistré
      if (state.editorHolding) return;
      _closed = true;
      if (!state.takeClosedNotice(widget.sourceId)) return; // un autre explorateur s'en est chargé
      // message sur le ScaffoldMessenger de l'application : il reste affiché sur l'accueil
      showMessage(context, Fr.volumeClosed);
      Navigator.of(context).popUntil((r) => r.isFirst);
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
    if (!mounted) return;
    setState(() => quiet ? _refreshing = true : _loading = true);
    try {
      final l = await state.api.listDir(widget.sourceId, path, state.viewPrefs.showHidden);
      if (!mounted || path != _path) return;
      setState(() {
        _entries = l;
        _error = null;
        _loading = _refreshing = false;
        _selected.retainWhere((p) => l.any((e) => e.path == p)); // éléments disparus entre-temps
      });
    } on PlatformException catch (e) {
      if (!mounted || path != _path) return;
      setState(() {
        _entries = const [];
        _error = e.message ?? Fr.genericError;
        _loading = _refreshing = false;
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
      if (mounted) showMessage(context, e.message ?? Fr.genericError, kind: MessageKind.error);
    }
  }

  void _setPrefs({String? sort, bool? ascending, bool? grid, bool? showHidden}) {
    state.setViewPrefs(state.viewPrefs.copyWith(sort: sort, ascending: ascending, grid: grid, showHidden: showHidden));
  }

  void _tap(EntryMsg e) {
    if (_selected.isNotEmpty) return _toggle(e, haptic: false);
    if (e.type == 'dir') return _go(e.path);
    if (isArchiveName(e.name)) {
      _archiveSheet(e);
      return;
    }
    if (opensInPortail(state, e.name)) {
      final v = state.viewPrefs;
      _run(() => openViewer(context, state, widget.sourceId, e, sortEntries(_entries, v.sort, v.ascending)));
      return;
    }
    _run(() => state.api.openWith(widget.sourceId, e.path));
  }

  /// Archive touchée : Extraire ici (nouveau dossier au nom de l'archive), Extraire vers… (dossier choisi
  /// ensuite, comme un collage), ou Ouvrir avec….
  Future<void> _archiveSheet(EntryMsg e) async {
    final choice = await showActionSheet<String>(context, title: e.name, actions: [
      SheetAction('here', Icons.unarchive_rounded, Fr.extractHere, enabled: !_readOnly),
      const SheetAction('to', Icons.drive_file_move_rounded, Fr.extractTo),
      const SheetAction('open', Icons.open_in_new_rounded, Fr.openWith),
    ]);
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
    if (mounted) showMessage(context, err, kind: MessageKind.error);
    state.clearError();
    return true;
  }

  /// Ajoute [e] à la sélection ou l'en retire ; l'appui long donne un retour haptique.
  void _toggle(EntryMsg e, {bool haptic = true}) {
    if (haptic) AppHaptics.selection(state);
    setState(() => _selected.contains(e.path) ? _selected.remove(e.path) : _selected.add(e.path));
  }

  void _selectAll() {
    AppHaptics.selection(state);
    setState(() => _selected.addAll(_entries.map((e) => e.path)));
  }

  /// Nom d'un nouvel élément ; OK reste désactivé tant que le nom est refusé (vide, « / », « .. », trop long).
  Future<String?> _askName(String title, String label, {String initial = '', bool selectBaseName = false}) =>
      promptText(context,
          title: title, label: label, initial: initial, selectBaseName: selectBaseName, validator: nameError);

  Future<void> _newFolder() async {
    final name = (await _askName(Fr.newFolder, Fr.folderName))?.trim();
    if (name == null || name.isEmpty) return;
    await _run(() async {
      await state.api.createFolder(widget.sourceId, _path, name);
      await _load(quiet: true);
    });
  }

  Future<void> _rename(String path) async {
    final name = (await _askName(Fr.rename, Fr.newName, initial: nameOf(path), selectBaseName: true))?.trim();
    if (name == null || name.isEmpty || name == nameOf(path)) return;
    await _run(() async {
      await state.api.rename(widget.sourceId, path, name);
      setState(_selected.clear);
      await _load(quiet: true);
    });
  }

  /// Dossier distant : pas de corbeille, suppression toujours définitive.
  bool get _remote => _source?.kind == 'remote';

  /// Supprimer : à la corbeille du stockage, ou définitivement au choix ; dans la corbeille ou sur un dossier distant,
  /// toujours définitif. Mise à la corbeille : le message propose « Annuler ».
  Future<void> _delete(List<String> paths) async {
    final inTrash = isInTrash(_path) || _remote;
    final permanent = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(paths.length == 1 ? Fr.deleteOne(nameOf(paths.single)) : Fr.deleteMany(paths.length)),
        content: Text(_remote ? Fr.deleteRemoteBody : (inTrash ? Fr.deleteBody : Fr.deleteToTrashBody)),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text(Fr.cancel)),
          if (!inTrash)
            TextButton(
              style: TextButton.styleFrom(foregroundColor: Theme.of(ctx).colorScheme.error),
              onPressed: () => Navigator.pop(ctx, true),
              child: const Text(Fr.deletePermanently),
            ),
          FilledButton(onPressed: () => Navigator.pop(ctx, inTrash), child: const Text(Fr.delete)),
        ],
      ),
    );
    if (permanent == null) return;
    if (permanent) AppHaptics.warning(state);
    final start = DateTime.now().millisecondsSinceEpoch;
    await _run(() async {
      await state.api.deleteEntries(widget.sourceId, paths, permanent);
      if (mounted) setState(_selected.clear);
      await _load(quiet: true);
      if (!permanent && mounted) {
        showMessage(context, Fr.movedToTrash(paths.length),
            actionLabel: Fr.undo, onAction: () => _undoTrash(paths, start));
      }
    });
  }

  /// Remet de la corbeille les éléments de [paths] mis à la corbeille depuis [startMs]. La date de suppression est
  /// écrite à la seconde près (`DeletionDate`), d'où le début de l'action arrondi à la seconde.
  Future<void> _undoTrash(List<String> paths, int startMs) => _run(() async {
        final since = startMs - startMs % 1000;
        final items = await state.api.listTrash(widget.sourceId);
        final ids = [
          for (final t in items)
            if (paths.contains(t.originalPath) && t.deletedAt >= since) t.id,
        ];
        if (ids.isNotEmpty) await state.api.restoreTrash(widget.sourceId, ids);
        await _load(quiet: true);
      });

  void _toClipboard(List<String> paths, {required bool move}) {
    state.setClipboard(widget.sourceId, paths, move: move);
    setState(_selected.clear);
  }

  Future<void> _paste() async {
    final c = state.clipboard;
    if (c == null) return;
    await state.startTransfer(c.sourceId, c.paths, widget.sourceId, _path, move: c.move);
    if (_showStateError()) return;
    state.clearClipboard(); // copie ou déplacement lancé : « Coller ici » disparaît
  }

  /// Renommage par lot de la sélection, dans l'ordre affiché (la numérotation le suit).
  Future<void> _batchRename() async {
    final v = state.viewPrefs;
    final paths = [
      for (final e in sortEntries(_entries, v.sort, v.ascending))
        if (_selected.contains(e.path)) e.path,
    ];
    final done = await Navigator.of(context).push<bool>(MaterialPageRoute(
      builder: (_) => BatchRenameScreen(state: state, sourceId: widget.sourceId, paths: paths),
    ));
    if (done != true || !mounted) return;
    setState(_selected.clear);
    await _load(quiet: true);
  }

  /// Propriétés de l'élément sélectionné (la sélection est quittée).
  /// Ouvre le fichier sélectionné dans l'éditeur de texte, quelle que soit son extension.
  Future<void> _editAsText(String path) {
    final e = _entries.firstWhere((x) => x.path == path);
    setState(_selected.clear);
    return openTextEditor(context, state, widget.sourceId, e);
  }

  Future<void> _properties(String path) {
    setState(_selected.clear);
    return showProperties(context, state, widget.sourceId, path);
  }

  /// Compresse la sélection dans un ZIP du dossier ouvert ; nom proposé : l'élément seul (sans son extension
  /// pour un fichier) ou « Archive ».
  Future<void> _compress(List<String> paths) async {
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
    if (mounted) setState(_selected.clear);
  }

  Future<void> _share(List<String> paths) => _run(() => state.api.share(widget.sourceId, paths));

  /// Menu ⋮ d'un élément : les actions de la sélection, pour lui seul ; écriture désactivée en lecture seule.
  Future<void> _itemMenu(EntryMsg e) async {
    final file = e.type == 'file';
    final ro = _readOnly;
    final a = await showActionSheet<String>(context, title: e.name, actions: [
      SheetAction('openWith', Icons.open_in_new_rounded, Fr.openWith, enabled: file),
      SheetAction('share', Icons.share_rounded, Fr.share, enabled: file),
      const SheetAction('copy', Icons.copy_rounded, Fr.copy),
      SheetAction('cut', Icons.content_cut_rounded, Fr.cut, enabled: !ro),
      SheetAction('rename', Icons.drive_file_rename_outline_rounded, Fr.rename, enabled: !ro),
      SheetAction('compress', Icons.folder_zip_rounded, Fr.compress, enabled: !ro),
      const SheetAction('properties', Icons.info_outline_rounded, Fr.properties),
      SheetAction('delete', Icons.delete_rounded, Fr.delete, enabled: !ro, destructive: true),
    ]);
    if (!mounted || a == null) return;
    final one = [e.path];
    switch (a) {
      case 'openWith':
        await _run(() => state.api.openWith(widget.sourceId, e.path));
      case 'share':
        await _share(one);
      case 'copy':
        state.setClipboard(widget.sourceId, one, move: false);
      case 'cut':
        state.setClipboard(widget.sourceId, one, move: true);
      case 'rename':
        await _rename(e.path);
      case 'compress':
        await _compress(one);
      case 'properties':
        await showProperties(context, state, widget.sourceId, e.path);
      case 'delete':
        await _delete(one);
    }
  }

  PreferredSizeWidget _appBar() {
    if (_selected.isNotEmpty) {
      final byPath = {for (final e in _entries) e.path: e};
      final hasDir = _selected.any((p) => byPath[p]?.type != 'file');
      // un seul fichier sélectionné : « Ouvrir avec… » et « Modifier comme texte »
      final singleFile = _selected.length == 1 && !hasDir ? byPath[_selected.single] : null;
      final paths = _selected.toList();
      return AppBar(
        leading: IconButton(tooltip: Fr.cancel, icon: const Icon(Icons.close_rounded), onPressed: () => setState(_selected.clear)),
        titleSpacing: 0,
        title: Text(Fr.selected(_selected.length), style: numeric(Theme.of(context).textTheme.titleMedium!)),
        actions: [
          IconButton(tooltip: Fr.selectAll, icon: const Icon(Icons.select_all_rounded), onPressed: _selectAll),
          IconButton(tooltip: Fr.copy, icon: const Icon(Icons.copy_rounded), onPressed: () => _toClipboard(paths, move: false)),
          IconButton(
            tooltip: Fr.cut,
            icon: const Icon(Icons.content_cut_rounded),
            onPressed: _readOnly ? null : () => _toClipboard(paths, move: true),
          ),
          IconButton(
            tooltip: Fr.delete,
            icon: const Icon(Icons.delete_rounded),
            onPressed: _readOnly ? null : () => _delete(paths),
          ),
          // le reste dans un menu : la barre tient sur la largeur d'un téléphone
          PopupMenuButton<String>(
            tooltip: Fr.moreActions,
            onSelected: (a) => switch (a) {
              'share' => _share(paths),
              'rename' => _selected.length == 1 ? _rename(_selected.single) : _batchRename(),
              'compress' => _compress(paths),
              'openWith' => _run(() => state.api.openWith(widget.sourceId, _selected.single)),
              'editText' => _editAsText(_selected.single),
              _ => _properties(_selected.single),
            },
            itemBuilder: (_) => [
              PopupMenuItem(value: 'share', enabled: !hasDir, child: const Text(Fr.share)),
              PopupMenuItem(value: 'openWith', enabled: singleFile != null, child: const Text(Fr.openWith)),
              PopupMenuItem(value: 'editText', enabled: singleFile != null, child: const Text(Fr.editAsText)),
              PopupMenuItem(value: 'rename', enabled: !_readOnly, child: const Text(Fr.rename)),
              PopupMenuItem(value: 'compress', enabled: !_readOnly, child: const Text(Fr.compress)),
              PopupMenuItem(value: 'properties', enabled: _selected.length == 1, child: const Text(Fr.properties)),
            ],
          ),
        ],
      );
    }
    final v = state.viewPrefs;
    final fav = state.favoriteFor(widget.sourceId, _path);
    final label = _source?.label ?? '';
    return AppBar(
      // le nom du stockage ; le dossier ouvert est dans le fil d'Ariane
      title: Text(label),
      bottom: _path == '/' ? null : Breadcrumbs(rootLabel: label, path: _path, onOpen: _go),
      actions: [
        IconButton(
          tooltip: Fr.search,
          icon: const Icon(Icons.search_rounded),
          onPressed: () => Navigator.of(context).push(MaterialPageRoute<void>(
            builder: (_) => SearchScreen(state: state, sourceId: widget.sourceId, path: _path),
          )),
        ),
        ViewModeButton(state: state),
        IconButton(
          tooltip: fav == null ? Fr.addFavorite : Fr.removeFavorite,
          icon: Icon(fav == null ? Icons.star_border_rounded : Icons.star_rounded),
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
            CheckedPopupMenuItem(value: 'hidden', checked: v.showHidden, child: const Text(Fr.showHidden)),
            if (_path == '/') ...[
              const PopupMenuDivider(),
              if (!_remote) const PopupMenuItem(value: 'trash', child: Text(Fr.trash)),
              const PopupMenuItem(value: 'space', child: Text(Fr.spaceAnalysis)),
            ],
          ],
        ),
      ],
    );
  }

  Widget _body() {
    if (_error != null) {
      return ErrorState(key: const ValueKey('error'), message: _error!, onRetry: _load);
    }
    if (_loading && _entries.isEmpty) return const DelayedProgress(key: ValueKey('loading'));
    final v = state.viewPrefs;
    final entries = sortEntries(_entries, v.sort, v.ascending);
    final Widget content = entries.isEmpty
        ? const EmptyState(icon: Icons.folder_open_rounded, title: Fr.emptyFolder)
        : CustomScrollView(
            physics: const AlwaysScrollableScrollPhysics(),
            slivers: [
              EntrySliver(
                state: state,
                thumbs: _thumbs,
                items: [for (final e in entries) EntryItem(sourceId: widget.sourceId, entry: e)],
                grid: v.grid,
                isSelected: (i) => _selected.contains(i.entry.path),
                onTap: (i) => _tap(i.entry),
                onLongPress: (i) => _toggle(i.entry),
                onMore: _selected.isEmpty ? (i) => _itemMenu(i.entry) : null,
              ),
              // le bouton flottant ne cache pas le dernier élément
              const SliverToBoxAdapter(child: SizedBox(height: Space.xxl * 2 + Space.xl)),
            ],
          );
    return RefreshIndicator(
      key: ValueKey(entries.isEmpty ? 'empty' : 'content'),
      onRefresh: () => _load(quiet: true),
      child: content,
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
            Padding(
              padding: const EdgeInsets.fromLTRB(Space.l, Space.s, Space.l, 0),
              child: StatusBanner(icon: Icons.edit_off_rounded, text: Fr.readOnly(src.roReason)),
            ),
          Expanded(
            child: Stack(children: [
              Positioned.fill(child: StateSwitcher(child: _body())),
              if (_refreshing) const Positioned(top: 0, left: 0, right: 0, child: LinearProgressIndicator()),
            ]),
          ),
        ]),
        // sous le bouton flottant, qui ne les masque pas
        bottomNavigationBar: BottomDock(
          state: state,
          pending: ClipboardBar(state: state, canPaste: !_readOnly, onPaste: _paste, onExtract: _extractPending),
        ),
        floatingActionButton: _readOnly || _selected.isNotEmpty
            ? null
            : FloatingActionButton(
                heroTag: null, // plusieurs explorateurs peuvent être empilés
                tooltip: Fr.newFolder,
                onPressed: _newFolder,
                child: const Icon(Icons.create_new_folder_rounded),
              ),
      ),
    );
  }
}
