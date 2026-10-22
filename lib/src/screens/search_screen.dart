import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../widgets/thumb.dart';
import 'explorer_screen.dart';

/// Recherche par nom sous [path] : lancée 400 ms après la dernière frappe, la précédente annulée.
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

  @override
  void dispose() {
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

  void _open(EntryMsg e) {
    if (e.type == 'dir') {
      openExplorer(context, widget.state, widget.sourceId, path: e.path);
    } else {
      widget.state.api.openWith(widget.sourceId, e.path).catchError((Object err) {
        if (mounted && err is PlatformException) {
          ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(err.message ?? Fr.genericError)));
        }
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final r = _result;
    return Scaffold(
      appBar: AppBar(
        title: TextField(
          autofocus: true,
          decoration: const InputDecoration(hintText: Fr.searchHint, border: InputBorder.none),
          onChanged: _changed,
          onSubmitted: (q) {
            _timer?.cancel();
            _search(q.trim());
          },
        ),
      ),
      body: Column(children: [
        if (_running) const LinearProgressIndicator(),
        if (r != null && r.truncated)
          MaterialBanner(content: const Text(Fr.tooManyResults), actions: const [SizedBox.shrink()]),
        Expanded(
          child: _error != null
              ? Center(child: Text(_error!))
              : r == null
                  ? const SizedBox.shrink()
                  : r.entries.isEmpty && !_running && _query.isNotEmpty
                      ? const Center(child: Text(Fr.noResult))
                      : ListView.builder(
                          itemCount: r.entries.length,
                          itemBuilder: (_, i) {
                            final e = r.entries[i];
                            return ListTile(
                              leading: Icon(entryIcon(e)),
                              title: Text(e.name),
                              subtitle: Text(parentOf(e.path)),
                              onTap: () => _open(e),
                            );
                          },
                        ),
        ),
      ]),
    );
  }
}
