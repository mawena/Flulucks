import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/feedback.dart';
import '../ui/states.dart';
import '../ui/tokens.dart';
import '../ui/widgets.dart';
import '../widgets/entry_collection.dart';
import '../widgets/found_entries.dart';
import '../widgets/thumb.dart';
import 'explorer_screen.dart';

/// Stockages fouillés par la recherche globale, dans l'ordre de l'accueil : téléphone et cartes SD, puis disques
/// USB ouverts. Les dossiers distants sont exclus : trop lents, et chaque appel ouvrirait des connexions.
List<SourceMsg> searchableSources(AppState s) => [
      for (final x in s.sources)
        if (x.kind == 'phone' || x.kind == 'removable') x,
      for (final x in s.sources)
        if (x.kind == 'disk') x,
    ];

/// Résultats d'un stockage.
class _Group {
  _Group(this.source, this.result);

  final SourceMsg source;
  final SearchResultMsg result;
}

/// Recherche par nom dans tous les stockages ouverts, lancée 400 ms après la dernière frappe. Les stockages sont
/// fouillés un à un ; une frappe plus récente abandonne la recherche en cours, et quitter l'écran l'annule.
class GlobalSearchScreen extends StatefulWidget {
  const GlobalSearchScreen({super.key, required this.state, this.initialQuery = ''});

  final AppState state;
  final String initialQuery;

  @override
  State<GlobalSearchScreen> createState() => _GlobalSearchScreenState();
}

class _GlobalSearchScreenState extends State<GlobalSearchScreen> {
  static const debounce = Duration(milliseconds: 400);

  late final _text = TextEditingController(text: widget.initialQuery);
  final _thumbs = ThumbCache();
  Timer? _timer;
  int _gen = 0;
  bool _running = false;
  String _query = '';
  String? _current; // libellé du stockage fouillé
  final _groups = <_Group>[];
  String? _error;

  AppState get state => widget.state;

  @override
  void initState() {
    super.initState();
    state.addListener(_onState);
    if (widget.initialQuery.trim().isNotEmpty) _search(widget.initialQuery.trim());
  }

  void _onState() {
    if (!mounted) return;
    // stockage disparu (verrouillage, éjection, débranchement) : plus rien de lui à l'écran, ni ses résultats ni une
    // visionneuse ouverte depuis eux ; l'éditeur qui garde un texte non enregistré passe d'abord, comme dans
    // l'explorateur
    final open = {for (final s in state.sources) s.id};
    final gone = {
      for (final g in _groups)
        if (!open.contains(g.source.id)) g.source.id,
    };
    if (gone.isNotEmpty && !state.editorHolding) {
      _groups.removeWhere((g) => gone.contains(g.source.id));
      if ([for (final id in gone) state.takeClosedNotice(id)].contains(true)) showMessage(context, Fr.volumeClosed);
      final route = ModalRoute.of(context);
      if (route != null && route.isActive && !route.isCurrent) Navigator.of(context).popUntil((r) => r == route);
    }
    setState(() {}); // et réglage d'affichage (liste ou grille)
  }

  @override
  void dispose() {
    state.removeListener(_onState);
    _timer?.cancel();
    _gen++;
    if (_running) state.api.cancelSearch();
    _thumbs.dispose();
    _text.dispose();
    super.dispose();
  }

  void _changed(String q) {
    _timer?.cancel();
    _timer = Timer(debounce, () => _search(q.trim()));
  }

  Future<void> _search(String q) async {
    final gen = ++_gen;
    if (_running) await state.api.cancelSearch();
    if (!mounted || gen != _gen) return;
    setState(() {
      _query = q;
      _groups.clear();
      _error = null;
      _running = q.isNotEmpty;
    });
    if (q.isEmpty) return;
    for (final s in searchableSources(state)) {
      setState(() => _current = s.label);
      try {
        final r = await state.api.search(s.id, '/', q, state.viewPrefs.showHidden);
        if (!mounted || gen != _gen) return; // dépassée par une frappe plus récente
        if (r.entries.isNotEmpty) setState(() => _groups.add(_Group(s, r)));
      } on PlatformException catch (e) {
        if (!mounted || gen != _gen) return;
        setState(() => _error = e.message ?? Fr.genericError);
      }
    }
    if (mounted && gen == _gen) {
      setState(() {
        _running = false;
        _current = null;
      });
    }
  }

  List<Widget> _group(_Group g) {
    final items = [
      for (final e in g.result.entries) EntryItem(sourceId: g.source.id, entry: e, subtitle: parentOf(e.path)),
    ];
    return [
      SliverToBoxAdapter(child: SectionHeader('${g.source.label} · ${g.result.entries.length}')),
      if (g.result.truncated)
        const SliverToBoxAdapter(
          child: Padding(
            padding: EdgeInsets.fromLTRB(Space.l, 0, Space.l, Space.s),
            child: StatusBanner(icon: Icons.info_outline_rounded, text: Fr.tooManyResults, tone: ChipTone.neutral),
          ),
        ),
      EntrySliver(
        state: state,
        thumbs: _thumbs,
        items: items,
        grid: state.viewPrefs.grid,
        onTap: (i) => openFoundEntry(context, state, i, items),
        onLongPress: (i) => showFoundEntrySheet(context, state, i),
        onMore: (i) => showFoundEntrySheet(context, state, i),
      ),
    ];
  }

  Widget _body() {
    if (_query.isEmpty) {
      return const EmptyState(key: ValueKey('hint'), icon: Icons.search_rounded, title: Fr.globalSearchHint);
    }
    if (_groups.isEmpty && !_running) {
      if (_error != null) return ErrorState(key: const ValueKey('error'), message: _error!, onRetry: () => _search(_query));
      return const EmptyState(key: ValueKey('none'), icon: Icons.search_off_rounded, title: Fr.noResult);
    }
    return CustomScrollView(key: const ValueKey('results'), slivers: [
      for (final g in _groups) ..._group(g),
      const SliverNavBarGap(),
    ]);
  }

  @override
  Widget build(BuildContext context) {
    final current = _current;
    return Scaffold(
      appBar: AppBar(
        title: TextField(
          controller: _text,
          autofocus: widget.initialQuery.isEmpty,
          decoration: const InputDecoration(hintText: Fr.searchHint, border: InputBorder.none),
          onChanged: _changed,
          onSubmitted: (q) {
            _timer?.cancel();
            _search(q.trim());
          },
        ),
        actions: [ViewModeButton(state: state)],
      ),
      body: Column(children: [
        if (_running) ...[
          const LinearProgressIndicator(),
          if (current != null)
            Padding(
              padding: const EdgeInsets.fromLTRB(Space.l, Space.s, Space.l, 0),
              child: Text(Fr.searchingIn(current), style: Theme.of(context).textTheme.bodySmall),
            ),
        ],
        Expanded(child: StateSwitcher(child: _body())),
      ]),
    );
  }
}
