import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/dialogs.dart';
import '../ui/feedback.dart';
import '../ui/states.dart';
import '../ui/tokens.dart';
import '../ui/widgets.dart';

/// Dossier de la corbeille à la racine de chaque stockage (côté Kotlin : Trash.DIR).
const trashDir = '/.Portail-corbeille';

bool isInTrash(String path) => path == trashDir || path.startsWith('$trashDir/');

/// Ouvre la corbeille de [sourceId].
Future<void> openTrash(BuildContext context, AppState state, String sourceId) =>
    Navigator.of(context).push(MaterialPageRoute<void>(
      builder: (_) => TrashScreen(state: state, sourceId: sourceId),
    ));

/// Corbeille d'un stockage : taille totale, éléments du plus récent au plus ancien (nom et emplacement d'origine),
/// Restaurer ou Supprimer définitivement chacun, Vider la corbeille.
class TrashScreen extends StatefulWidget {
  const TrashScreen({super.key, required this.state, required this.sourceId});

  final AppState state;
  final String sourceId;

  @override
  State<TrashScreen> createState() => _TrashScreenState();
}

class _TrashScreenState extends State<TrashScreen> {
  List<TrashItemMsg> _items = const [];
  String? _error;
  bool _loading = true;
  bool _closed = false;

  AppState get state => widget.state;
  bool get _readOnly => state.sourceById(widget.sourceId)?.readOnly ?? true;

  @override
  void initState() {
    super.initState();
    state.addListener(_onState);
    _load();
  }

  @override
  void dispose() {
    state.removeListener(_onState);
    super.dispose();
  }

  /// Stockage disparu (éjection, débranchement) : l'écran se ferme.
  void _onState() {
    if (!mounted || _closed) return;
    if (state.sourceById(widget.sourceId) == null) {
      _closed = true;
      Navigator.of(context).pop();
      return;
    }
    setState(() {});
  }

  Future<void> _load() async {
    try {
      final l = await state.api.listTrash(widget.sourceId);
      if (!mounted) return;
      setState(() {
        _items = l;
        _error = null;
        _loading = false;
      });
    } on PlatformException catch (e) {
      if (!mounted) return;
      setState(() {
        _items = const [];
        _error = e.message ?? Fr.genericError;
        _loading = false;
      });
    }
  }

  void _retry() {
    setState(() {
      _loading = true;
      _error = null;
    });
    _load();
  }

  Future<void> _run(Future<void> Function() action) async {
    try {
      await action();
    } on PlatformException catch (e) {
      if (mounted) showMessage(context, e.message ?? Fr.genericError, kind: MessageKind.error);
    }
    await _load();
  }

  /// Suppression définitive : bouton rouge, retour haptique d'avertissement une fois confirmée.
  Future<bool> _confirm(String title, String body, String action) async {
    final ok = await confirmAction(context, title: title, body: body, confirmLabel: action, destructive: true);
    if (ok) AppHaptics.warning(state);
    return ok;
  }

  Future<void> _act(TrashItemMsg item, String action) async {
    if (action == 'restore') return _run(() => state.api.restoreTrash(widget.sourceId, [item.id]));
    if (!await _confirm(Fr.deleteOne(item.name), Fr.deleteBody, Fr.delete)) return;
    await _run(() => state.api.purgeTrash(widget.sourceId, [item.id]));
  }

  Future<void> _empty() async {
    if (!await _confirm(Fr.emptyTrashTitle, Fr.emptyTrashBody(_items.length), Fr.emptyAction)) return;
    await _run(() => state.api.emptyTrash(widget.sourceId));
  }

  Widget _tile(TrashItemMsg item) {
    final date = item.deletedAt > 0 ? ' · ${Fr.friendlyDate(item.deletedAt ~/ 1000)}' : '';
    return ListTile(
      key: ValueKey(item.id),
      leading: Icon(item.isDir ? Icons.folder_rounded : Icons.insert_drive_file_rounded),
      title: Text(item.name, maxLines: 1, overflow: TextOverflow.ellipsis),
      subtitle: Text('${item.originalPath}$date', maxLines: 2, overflow: TextOverflow.ellipsis),
      trailing: PopupMenuButton<String>(
        tooltip: Fr.actionsOf(item.name),
        enabled: !_readOnly,
        onSelected: (a) => _act(item, a),
        itemBuilder: (_) => const [
          PopupMenuItem(value: 'restore', child: Text(Fr.restore)),
          PopupMenuItem(value: 'purge', child: Text(Fr.deletePermanently)),
        ],
      ),
    );
  }

  Widget _body() {
    if (_loading) return const DelayedProgress();
    if (_error != null) return ErrorState(message: _error!, onRetry: _retry);
    if (_items.isEmpty) return const EmptyState(icon: Icons.delete_outline_rounded, title: Fr.trashEmpty, message: Fr.trashNote);
    final total = _items.fold<int>(0, (s, i) => s + i.size);
    return ListView(children: [
      ListTile(title: Text(Fr.trashSummary(_items.length, total)), subtitle: const Text(Fr.trashNote)),
      const Divider(height: 1),
      for (final i in _items) _tile(i),
    ]);
  }

  @override
  Widget build(BuildContext context) {
    final src = state.sourceById(widget.sourceId);
    return Scaffold(
      appBar: AppBar(
        title: const Text(Fr.trash),
        actions: [
          IconButton(
            tooltip: Fr.emptyTrash,
            icon: const Icon(Icons.delete_forever_rounded),
            onPressed: _items.isEmpty || _readOnly ? null : _empty,
          ),
        ],
      ),
      body: Column(children: [
        if (src != null && src.readOnly)
          Padding(
            padding: const EdgeInsets.fromLTRB(Space.l, Space.s, Space.l, 0),
            child: StatusBanner(icon: Icons.lock_rounded, text: Fr.readOnly(src.roReason)),
          ),
        Expanded(child: RefreshIndicator(onRefresh: _load, child: StateSwitcher(child: _body()))),
      ]),
    );
  }
}
