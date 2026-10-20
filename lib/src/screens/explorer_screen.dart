import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';

/// Ouvre l'explorateur sur [sourceId] (à la racine, ou sur [path]).
Future<void> openExplorer(BuildContext context, AppState state, String sourceId, {String path = '/'}) =>
    Navigator.of(context).push(MaterialPageRoute<void>(
      builder: (_) => ExplorerScreen(state: state, sourceId: sourceId, path: path),
    ));

/// Explorateur d'un stockage (complété en T9).
class ExplorerScreen extends StatefulWidget {
  const ExplorerScreen({super.key, required this.state, required this.sourceId, this.path = '/'});

  final AppState state;
  final String sourceId;
  final String path;

  @override
  State<ExplorerScreen> createState() => _ExplorerScreenState();
}

class _ExplorerScreenState extends State<ExplorerScreen> {
  List<EntryMsg> _entries = const [];

  @override
  void initState() {
    super.initState();
    widget.state.api.listDir(widget.sourceId, widget.path, false).then((l) {
      if (mounted) setState(() => _entries = l);
    });
  }

  @override
  Widget build(BuildContext context) {
    final label = widget.state.sourceById(widget.sourceId)?.label ?? '';
    return Scaffold(
      appBar: AppBar(title: Text(widget.path == '/' ? label : widget.path.split('/').last)),
      body: ListView(children: [for (final e in _entries) ListTile(title: Text(e.name))]),
    );
  }
}
