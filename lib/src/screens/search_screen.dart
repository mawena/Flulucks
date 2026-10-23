import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/states.dart';
import '../ui/tokens.dart';
import '../ui/widgets.dart';
import '../widgets/entry_collection.dart';
import '../widgets/found_entries.dart';
import '../widgets/thumb.dart';
import 'explorer_screen.dart';
import 'global_search_screen.dart';

/// Recherche par nom sous [path] : lancée 400 ms après la dernière frappe, la précédente annulée. Résultats en
/// liste ou en grille selon le réglage d'affichage, ouverts dans les lecteurs de Portail.
class SearchScreen extends StatefulWidget {
  const SearchScreen({super.key, required this.state, required this.sourceId, required this.path});

  final AppState state;
  final String sourceId;
  final String path;

  @override
  State<SearchScreen> createState() => _SearchScreenState();
}

class _SearchScreenState extends State<SearchScreen> {
  static const debounce = Duration(milliseconds: 400);

  Timer? _timer;
  int _gen = 0;
  bool _running = false;
  String _query = '';
  SearchResultMsg? _result;
  String? _error;
  final _thumbs = ThumbCache();
  final _text = TextEditingController();

  AppState get state => widget.state;

  @override
  void initState() {
    super.initState();
    state.addListener(_onState);
  }

  void _onState() {
    if (mounted) setState(() {}); // réglage d'affichage (liste ou grille)
  }

  @override
  void dispose() {
    state.removeListener(_onState);
    _thumbs.dispose();
    _text.dispose();
    _timer?.cancel();
    if (_running) widget.state.api.cancelSearch();
    super.dispose();
  }

  void _changed(String q) {
    _timer?.cancel();
    _timer = Timer(debounce, () => _search(q.trim()));
  }

  Future<void> _search(String q) async {
    final gen = ++_gen;
    if (_running) await widget.state.api.cancelSearch();
    if (q.isEmpty) {
      setState(() {
        _query = '';
        _result = null;
        _running = false;
      });
      return;
    }
    setState(() {
      _query = q;
      _running = true;
      _error = null;
    });
    try {
      final r = await widget.state.api.search(widget.sourceId, widget.path, q, widget.state.viewPrefs.showHidden);
      if (!mounted || gen != _gen) return; // dépassée par une frappe plus récente
      setState(() {
        _result = r;
        _running = false;
      });
    } on PlatformException catch (e) {
      if (!mounted || gen != _gen) return;
      setState(() {
        _error = e.message ?? Fr.genericError;
        _running = false;
      });
    }
  }

  Widget _results(List<EntryMsg> entries) {
    final items = [
      for (final e in entries) EntryItem(sourceId: widget.sourceId, entry: e, subtitle: parentOf(e.path)),
    ];
    return CustomScrollView(slivers: [
      EntrySliver(
        state: state,
        thumbs: _thumbs,
        items: items,
        grid: state.viewPrefs.grid,
        onTap: (i) => openFoundEntry(context, state, i, items),
        onLongPress: (i) => showFoundEntrySheet(context, state, i),
        onMore: (i) => showFoundEntrySheet(context, state, i),
      ),
    ]);
  }

  @override
  Widget build(BuildContext context) {
    final r = _result;
    return Scaffold(
      appBar: AppBar(
        title: TextField(
          controller: _text,
          autofocus: true,
          decoration: const InputDecoration(hintText: Fr.searchHint, border: InputBorder.none),
          onChanged: _changed,
          onSubmitted: (q) {
            _timer?.cancel();
            _search(q.trim());
          },
        ),
        actions: [ViewModeButton(state: state)],
        // « Ici » : ce dossier ; « Partout » : tous les stockages, avec la même saisie
        bottom: PreferredSize(
          preferredSize: const Size.fromHeight(Layout.minTouch),
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: Space.l),
            child: Row(spacing: Space.s, children: [
              ChoiceChip(label: const Text(Fr.searchHere), selected: true, onSelected: (_) {}),
              ChoiceChip(
                label: const Text(Fr.searchAll),
                selected: false,
                onSelected: (_) => Navigator.of(context).pushReplacement(MaterialPageRoute<void>(
                  builder: (_) => GlobalSearchScreen(state: state, initialQuery: _text.text.trim()),
                )),
              ),
            ]),
          ),
        ),
      ),
      body: Column(children: [
        if (_running) const LinearProgressIndicator(),
        if (r != null && r.truncated)
          const Padding(
            padding: EdgeInsets.fromLTRB(Space.l, Space.s, Space.l, 0),
            child: StatusBanner(icon: Icons.info_outline_rounded, text: Fr.tooManyResults, tone: ChipTone.neutral),
          ),
        Expanded(
          child: _error != null
              ? ErrorState(message: _error!, onRetry: () => _search(_query))
              : r == null
                  ? const SizedBox.shrink()
                  : r.entries.isEmpty && !_running && _query.isNotEmpty
                      ? const EmptyState(icon: Icons.search_off_rounded, title: Fr.noResult)
                      : _results(r.entries),
        ),
      ]),
    );
  }
}
