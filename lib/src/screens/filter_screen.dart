import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import 'explorer_screen.dart';

/// Filtres rapides de l'accueil : (identifiant côté Kotlin, libellé, icône).
const quickFilters = [
  ('photos', Fr.filterPhotos, Icons.photo),
  ('videos', Fr.filterVideos, Icons.movie),
  ('music', Fr.filterMusic, Icons.music_note),
  ('documents', Fr.filterDocuments, Icons.description),
  ('archives', Fr.filterArchives, Icons.folder_zip),
  ('recent', Fr.filterRecent, Icons.schedule),
];

/// Fichiers d'un filtre rapide (téléphone et cartes SD), du plus récent : toucher ouvre le fichier, l'icône de
/// dossier ouvre l'explorateur à son emplacement.
class FilterScreen extends StatefulWidget {
  const FilterScreen({super.key, required this.state, required this.filter, required this.title});

  final AppState state;
  final String filter;
  final String title;

  @override
  State<FilterScreen> createState() => _FilterScreenState();
}

class _FilterScreenState extends State<FilterScreen> {
  List<FilterItemMsg> _items = const [];
  bool _loading = true;
  String? _error;

  AppState get state => widget.state;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    try {
      final l = await state.api.quickFilter(widget.filter);
      if (!mounted) return;
      setState(() {
        _items = l;
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

  Future<void> _open(FilterItemMsg i) async {
    try {
      await state.api.openWith(i.sourceId, i.path);
    } on PlatformException catch (e) {
      if (mounted) ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(e.message ?? Fr.genericError)));
    }
  }

  Widget _body() {
    if (_loading) return const Center(child: CircularProgressIndicator());
    if (_error != null) return Center(child: Padding(padding: const EdgeInsets.all(32), child: Text(_error!)));
    if (_items.isEmpty) return const Center(child: Text(Fr.filterEmpty));
    return ListView(children: [
      const ListTile(dense: true, subtitle: Text(Fr.filterNote)),
      for (final i in _items)
        ListTile(
          key: ValueKey('${i.sourceId}:${i.path}'),
          title: Text(i.name, maxLines: 1, overflow: TextOverflow.ellipsis),
          subtitle: Text(
            Fr.filterLine(state.sourceById(i.sourceId)?.label ?? i.sourceId, parentOf(i.path), i.size),
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
          ),
          onTap: () => _open(i),
          trailing: IconButton(
            tooltip: Fr.openFolder,
            icon: const Icon(Icons.folder_open),
            onPressed: () => openExplorer(context, state, i.sourceId, path: parentOf(i.path)),
          ),
        ),
    ]);
  }

  @override
  Widget build(BuildContext context) => Scaffold(
        appBar: AppBar(title: Text(widget.title)),
        body: RefreshIndicator(onRefresh: _load, child: _body()),
      );
}
