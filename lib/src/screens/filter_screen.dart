import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/states.dart';
import '../ui/widgets.dart';
import '../widgets/entry_collection.dart';
import '../widgets/found_entries.dart';
import '../widgets/thumb.dart';
import 'explorer_screen.dart';

/// Filtres rapides de l'accueil : (identifiant côté Kotlin, libellé, icône).
const quickFilters = [
  ('photos', Fr.filterPhotos, Icons.photo_rounded),
  ('videos', Fr.filterVideos, Icons.movie_rounded),
  ('music', Fr.filterMusic, Icons.music_note_rounded),
  ('documents', Fr.filterDocuments, Icons.description_rounded),
  ('archives', Fr.filterArchives, Icons.folder_zip_rounded),
  ('recent', Fr.filterRecent, Icons.schedule_rounded),
];

/// Fichiers d'un filtre rapide (téléphone et cartes SD), du plus récent, en liste ou en grille selon le réglage
/// d'affichage : toucher ouvre le fichier dans le lecteur de Portail (ou une autre application), ⋮ ou l'appui long
/// ouvre ses actions (dont « Ouvrir le dossier »).
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
  final _thumbs = ThumbCache();

  AppState get state => widget.state;

  @override
  void initState() {
    super.initState();
    _load();
  }

  @override
  void dispose() {
    _thumbs.dispose();
    super.dispose();
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

  void _retry() {
    setState(() {
      _loading = true;
      _error = null;
    });
    _load();
  }

  Widget _body() {
    if (_loading) return const DelayedProgress(key: ValueKey('loading'));
    if (_error != null) return ErrorState(key: const ValueKey('error'), message: _error!, onRetry: _retry);
    if (_items.isEmpty) return const EmptyState(icon: Icons.search_off_rounded, title: Fr.filterEmpty, message: Fr.filterNote);
    final items = [
      for (final i in _items)
        EntryItem(
          sourceId: i.sourceId,
          entry: EntryMsg(name: i.name, path: i.path, type: 'file', size: i.size, mtime: i.mtime),
          subtitle: Fr.filterLine(state.sourceById(i.sourceId)?.label ?? i.sourceId, parentOf(i.path), i.size),
        ),
    ];
    return CustomScrollView(
      key: const ValueKey('content'),
      physics: const AlwaysScrollableScrollPhysics(),
      slivers: [
        const SliverToBoxAdapter(child: ListTile(dense: true, subtitle: Text(Fr.filterNote))),
        EntrySliver(
          state: state,
          thumbs: _thumbs,
          items: items,
          grid: state.viewPrefs.grid,
          onTap: (i) => openFoundEntry(context, state, i, items),
          onLongPress: (i) => showFoundEntrySheet(context, state, i),
          onMore: (i) => showFoundEntrySheet(context, state, i),
        ),
        const SliverNavBarGap(),
      ],
    );
  }

  @override
  Widget build(BuildContext context) => ListenableBuilder(
        listenable: state, // réglage d'affichage (liste ou grille)
        builder: (context, _) => Scaffold(
          appBar: AppBar(title: Text(widget.title), actions: [ViewModeButton(state: state)]),
          body: RefreshIndicator(onRefresh: _load, child: StateSwitcher(child: _body())),
        ),
      );
}
