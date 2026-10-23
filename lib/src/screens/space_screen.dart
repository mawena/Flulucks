import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';

/// Ouvre l'analyse de l'espace de [sourceId], depuis sa racine.
Future<void> openSpace(BuildContext context, AppState state, String sourceId) =>
    Navigator.of(context).push(MaterialPageRoute<void>(
      builder: (_) => SpaceScreen(state: state, sourceId: sourceId),
    ));

String _parentOf(String path) {
  final i = path.lastIndexOf('/');
  return i <= 0 ? '/' : path.substring(0, i);
}

/// Analyse de l'espace : dossiers et fichiers du plus gros au plus petit, avec une barre proportionnelle et leur
/// pourcentage ; toucher un dossier y descend (lu dans l'analyse en mémoire), Retour remonte. Second onglet : les
/// plus gros fichiers. L'analyse se fait une fois, annulable.
class SpaceScreen extends StatefulWidget {
  const SpaceScreen({super.key, required this.state, required this.sourceId});

  final AppState state;
  final String sourceId;

  @override
  State<SpaceScreen> createState() => _SpaceScreenState();
}

class _SpaceScreenState extends State<SpaceScreen> {
  final _stack = <SpaceReportMsg>[];
  bool _loading = true;
  String? _error;

  AppState get state => widget.state;

  @override
  void initState() {
    super.initState();
    _show(() => state.api.analyzeSpace(widget.sourceId, '/'));
  }

  @override
  void dispose() {
    if (_loading) state.api.cancelAnalysis(); // écran quitté pendant l'analyse
    super.dispose();
  }

  Future<void> _show(Future<SpaceReportMsg> Function() load) async {
    setState(() => _loading = true);
    try {
      final r = await load();
      if (!mounted) return;
      setState(() {
        _stack.add(r);
        _error = null;
        _loading = false;
      });
    } on PlatformException catch (e) {
      if (!mounted) return;
      setState(() {
        _error = e.message ?? Fr.genericError;
        _loading = false;
      });
    }
  }

  void _open(SpaceItemMsg item) {
    if (!item.isDir || _loading) return;
    _show(() => state.api.spaceChildren(widget.sourceId, item.path));
  }

  int _pct(int size, int total) => total > 0 ? (size * 100 / total).round() : 0;

  Widget _progress() => Center(
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          const CircularProgressIndicator(),
          const SizedBox(height: 16),
          const Text(Fr.analyzing),
          TextButton(onPressed: state.api.cancelAnalysis, child: const Text(Fr.cancel)),
        ]),
      );

  Widget _folders(SpaceReportMsg r) => ListView(children: [
        ListTile(title: Text(Fr.spaceTotal(r.totalBytes, r.fileCount)), subtitle: Text(r.path)),
        if (r.truncated) const ListTile(leading: Icon(Icons.warning_amber), title: Text(Fr.spaceTruncated)),
        const Divider(height: 1),
        for (final c in r.children)
          ListTile(
            key: ValueKey(c.path),
            leading: Icon(c.isDir ? Icons.folder : Icons.insert_drive_file),
            title: Text(c.name, maxLines: 1, overflow: TextOverflow.ellipsis),
            subtitle: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text(Fr.humanSize(c.size)),
              const SizedBox(height: 4),
              LinearProgressIndicator(value: r.totalBytes > 0 ? c.size / r.totalBytes : 0),
            ]),
            trailing: Text(Fr.percent(_pct(c.size, r.totalBytes))),
            onTap: c.isDir ? () => _open(c) : null,
          ),
        if (r.otherCount > 0)
          ListTile(title: Text(Fr.spaceOthers(r.otherCount)), trailing: Text(Fr.humanSize(r.otherBytes))),
      ]);

  Widget _biggest(SpaceReportMsg r) => ListView(children: [
        for (final e in r.biggest)
          ListTile(
            key: ValueKey(e.path),
            leading: const Icon(Icons.insert_drive_file),
            title: Text(e.name, maxLines: 1, overflow: TextOverflow.ellipsis),
            subtitle: Text(_parentOf(e.path), maxLines: 1, overflow: TextOverflow.ellipsis),
            trailing: Text(Fr.humanSize(e.size)),
          ),
      ]);

  @override
  Widget build(BuildContext context) {
    final r = _stack.isEmpty ? null : _stack.last;
    final Widget body;
    if (_loading && r == null) {
      body = _progress();
    } else if (_error != null && r == null) {
      body = Center(child: Padding(padding: const EdgeInsets.all(32), child: Text(_error!)));
    } else {
      body = TabBarView(children: [_folders(r!), _biggest(r)]);
    }
    return PopScope(
      canPop: _stack.length <= 1,
      onPopInvokedWithResult: (didPop, _) {
        if (!didPop) setState(_stack.removeLast);
      },
      child: DefaultTabController(
        length: 2,
        child: Scaffold(
          appBar: AppBar(
            title: const Text(Fr.spaceAnalysis),
            bottom: const TabBar(tabs: [Tab(text: Fr.spaceFolders), Tab(text: Fr.biggestFiles)]),
          ),
          body: body,
        ),
      ),
    );
  }
}
