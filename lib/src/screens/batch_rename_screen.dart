import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/feedback.dart';

/// Renommage par lot de [paths] : numérotation, remplacement d'un texte ou date de prise de vue, avec l'aperçu
/// des nouveaux noms (calculé côté Kotlin, conflits signalés). « Renommer » n'est permis que sans conflit et si un
/// nom change ; l'écran se ferme en rendant true une fois le lot renommé.
class BatchRenameScreen extends StatefulWidget {
  const BatchRenameScreen({super.key, required this.state, required this.sourceId, required this.paths});

  final AppState state;
  final String sourceId;
  final List<String> paths;

  @override
  State<BatchRenameScreen> createState() => _BatchRenameScreenState();
}

class _BatchRenameScreenState extends State<BatchRenameScreen> {
  String _mode = 'number';
  final _base = TextEditingController();
  final _start = TextEditingController(text: '1');
  final _find = TextEditingController();
  final _replace = TextEditingController();
  bool _caseSensitive = false;
  List<RenamePreviewMsg> _preview = const [];
  String? _error;
  bool _busy = false;
  int _seq = 0; // aperçus en vol : seul le dernier compte

  AppState get state => widget.state;

  @override
  void initState() {
    super.initState();
    _refresh();
  }

  @override
  void dispose() {
    for (final c in [_base, _start, _find, _replace]) {
      c.dispose();
    }
    super.dispose();
  }

  RenameRuleMsg get _rule => RenameRuleMsg(
        mode: _mode,
        base: _base.text,
        start: int.tryParse(_start.text.trim()) ?? 1,
        find: _find.text,
        replace: _replace.text,
        caseSensitive: _caseSensitive,
      );

  Future<void> _refresh() async {
    final seq = ++_seq;
    try {
      final p = await state.api.previewRename(widget.sourceId, widget.paths, _rule);
      if (!mounted || seq != _seq) return;
      setState(() {
        _preview = p;
        _error = null;
      });
    } on PlatformException catch (e) {
      if (!mounted || seq != _seq) return;
      setState(() {
        _preview = const [];
        _error = e.message ?? Fr.genericError;
      });
    }
  }

  String _nameOf(String path) => path.substring(path.lastIndexOf('/') + 1);

  bool get _canApply =>
      !_busy &&
      _preview.isNotEmpty &&
      _preview.every((p) => p.conflict == null) &&
      _preview.any((p) => p.newName != _nameOf(p.path));

  Future<void> _apply() async {
    setState(() => _busy = true);
    try {
      await state.api.applyRename(widget.sourceId, widget.paths, _rule);
      if (mounted) Navigator.of(context).pop(true);
    } on PlatformException catch (e) {
      if (!mounted) return;
      setState(() => _busy = false);
      showMessage(context, e.message ?? Fr.genericError, kind: MessageKind.error);
      _refresh();
    }
  }

  Widget _field(TextEditingController c, String label, {TextInputType? keyboard}) => Padding(
        padding: const EdgeInsets.fromLTRB(16, 8, 16, 0),
        child: TextField(
          controller: c,
          keyboardType: keyboard,
          decoration: InputDecoration(labelText: label),
          onChanged: (_) => _refresh(),
        ),
      );

  List<Widget> _fields() => switch (_mode) {
        'number' => [_field(_base, Fr.baseName), _field(_start, Fr.startNumber, keyboard: TextInputType.number)],
        'replace' => [
            _field(_find, Fr.findLabel),
            _field(_replace, Fr.replaceWith),
            SwitchListTile(
              title: const Text(Fr.caseSensitive),
              value: _caseSensitive,
              onChanged: (v) {
                setState(() => _caseSensitive = v);
                _refresh();
              },
            ),
          ],
        _ => [const Padding(padding: EdgeInsets.fromLTRB(16, 8, 16, 0), child: Text(Fr.photoDateHelp))],
      };

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Scaffold(
      appBar: AppBar(title: Text(Fr.batchRenameTitle(widget.paths.length))),
      body: Column(children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 0),
          child: SegmentedButton<String>(
            segments: const [
              ButtonSegment(value: 'number', label: Text(Fr.modeNumber)),
              ButtonSegment(value: 'replace', label: Text(Fr.modeReplace)),
              ButtonSegment(value: 'photoDate', label: Text(Fr.modePhotoDate)),
            ],
            selected: {_mode},
            onSelectionChanged: (s) {
              setState(() => _mode = s.single);
              _refresh();
            },
          ),
        ),
        ..._fields(),
        const SizedBox(height: 8),
        const Divider(height: 1),
        if (_error != null) Padding(padding: const EdgeInsets.all(16), child: Text(_error!)),
        Expanded(
          child: ListView(children: [
            for (final p in _preview)
              ListTile(
                key: ValueKey(p.path),
                title: Text(p.newName, maxLines: 1, overflow: TextOverflow.ellipsis),
                subtitle: Text(
                  p.conflict ?? _nameOf(p.path),
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: p.conflict == null ? null : TextStyle(color: theme.colorScheme.error),
                ),
              ),
          ]),
        ),
        SafeArea(
          top: false,
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Align(
              alignment: Alignment.centerRight,
              child: FilledButton(onPressed: _canApply ? _apply : null, child: const Text(Fr.rename)),
            ),
          ),
        ),
      ]),
    );
  }
}
