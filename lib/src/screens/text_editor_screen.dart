import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/feedback.dart';
import '../ui/states.dart';
import '../ui/tokens.dart';
import '../ui/widgets.dart';

/// Éditeur de texte (phase 10) : fichier UTF-8 de 2 Mio au plus, BOM et fins de ligne Windows rendus à
/// l'enregistrement. Kotlin écrit un fichier temporaire qui remplace l'original, et refuse si le fichier a changé
/// depuis son ouverture : l'éditeur propose alors de le remplacer. Quitter avec des modifications demande d'abord.
/// En bas, une ligne d'état : encodage, fins de ligne, nombre de lignes, et « Lecture seule » le cas échéant.
class TextEditorScreen extends StatefulWidget {
  const TextEditorScreen({super.key, required this.state, required this.sourceId, required this.path, required this.name});

  final AppState state;
  final String sourceId;
  final String path;
  final String name;

  @override
  State<TextEditorScreen> createState() => _TextEditorScreenState();
}

class _TextEditorScreenState extends State<TextEditorScreen> {
  final _ctrl = TextEditingController();
  TextFileMsg? _doc;
  String? _error;
  bool _saving = false;

  /// Texte tel qu'il est sur le stockage (lu ou enregistré).
  String _saved = '';

  bool get _dirty => _doc != null && _ctrl.text != _saved;
  bool get _readOnly => _doc?.readOnlyReason != null;

  /// Stockage disparu (éjection, verrouillage automatique, câble arraché) : le texte reste, sans enregistrement.
  bool get _gone => widget.state.sourceById(widget.sourceId) == null;

  /// Retient le retour à l'accueil des explorateurs tant qu'il y a des modifications ([AppState.holdForEditor]).
  bool _holding = false;

  @override
  void initState() {
    super.initState();
    _ctrl.addListener(_changed);
    widget.state.addListener(_onState);
    _load();
  }

  @override
  void dispose() {
    widget.state.removeListener(_onState);
    _ctrl.dispose();
    // après le démontage : l'explorateur d'un stockage disparu peut alors revenir à l'accueil
    if (_holding) Future.microtask(widget.state.releaseEditor);
    super.dispose();
  }

  void _changed() {
    if (_dirty && !_holding) {
      _holding = true;
      widget.state.holdForEditor();
    } else if (!_dirty && _holding && !_gone) {
      _holding = false;
      widget.state.releaseEditor();
    }
    setState(() {});
  }

  void _onState() {
    if (mounted) setState(() {});
  }

  Future<void> _load() async {
    if (_error != null) setState(() => _error = null); // Réessayer : de nouveau en chargement
    try {
      final d = await widget.state.api.readText(widget.sourceId, widget.path);
      if (!mounted) return;
      setState(() {
        _doc = d;
        _saved = d.text;
        _ctrl.text = d.text;
      });
    } on PlatformException catch (e) {
      if (mounted) setState(() => _error = e.message ?? Fr.genericError);
    }
  }

  /// Enregistre ; vrai si c'est fait. Fichier changé entre-temps : demande avant de le remplacer.
  Future<bool> _save({bool force = false}) async {
    final d = _doc!;
    final text = _ctrl.text;
    setState(() => _saving = true);
    try {
      final r = await widget.state.api.writeText(widget.sourceId, widget.path, text, d.bom, d.crlf, d.mtime, d.size, force);
      if (!mounted) return false;
      if (!r.saved) {
        if (!r.changed || !await _confirmReplace()) return false;
        return _save(force: true);
      }
      setState(() {
        _doc = TextFileMsg(text: text, bom: d.bom, crlf: d.crlf, mtime: r.mtime, size: r.size);
        _saved = text;
      });
      showMessage(context, Fr.saved, kind: MessageKind.success);
      return true;
    } on PlatformException catch (e) {
      if (mounted) showMessage(context, e.message ?? Fr.genericError, kind: MessageKind.error);
      return false;
    } finally {
      if (mounted) setState(() => _saving = false);
    }
  }

  Future<bool> _confirmReplace() async =>
      await showDialog<bool>(
        context: context,
        builder: (ctx) => AlertDialog(
          title: const Text(Fr.textChangedTitle),
          content: const Text(Fr.textChangedBody),
          actions: [
            TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text(Fr.cancel)),
            TextButton(onPressed: () => Navigator.pop(ctx, true), child: const Text(Fr.replace)),
          ],
        ),
      ) ??
      false;

  /// Retour avec des modifications : Enregistrer, Ne pas enregistrer ou Annuler.
  Future<void> _leave() async {
    final choice = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text(Fr.unsavedTitle),
        content: const Text(Fr.unsavedBody),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text(Fr.cancel)),
          TextButton(onPressed: () => Navigator.pop(ctx, 'discard'), child: const Text(Fr.dontSave)),
          if (!_gone) TextButton(onPressed: () => Navigator.pop(ctx, 'save'), child: const Text(Fr.save)),
        ],
      ),
    );
    if (!mounted || choice == null) return;
    if (choice == 'save' && !await _save()) return;
    if (mounted) Navigator.of(context).pop();
  }

  /// Lignes du texte : « a\nb » et « a\nb\n » en ont deux, un texte vide aucune.
  /// Recompté à chaque frappe : une simple boucle, sans objet par ligne (2 Mio au plus).
  static int lineCount(String t) {
    var breaks = 0;
    for (var i = 0; i < t.length; i++) {
      if (t.codeUnitAt(i) == 0x0A) breaks++;
    }
    return t.isEmpty || t.endsWith('\n') ? breaks : breaks + 1;
  }

  Widget _statusLine() {
    final theme = Theme.of(context);
    final d = _doc!;
    return Material(
      color: theme.colorScheme.surfaceContainerHigh,
      child: SafeArea(
        top: false,
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: Space.l, vertical: Space.xs),
          child: Wrap(
            spacing: Space.m,
            runSpacing: Space.xs,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              if (_readOnly)
                StatusChip(
                  label: Fr.readOnlyText(d.readOnlyReason!),
                  icon: Icons.lock_rounded,
                  tone: ChipTone.warning,
                ),
              Text(
                Fr.editorStatus(bom: d.bom, crlf: d.crlf, lines: lineCount(_ctrl.text)),
                style: numeric(theme.textTheme.labelSmall!).copyWith(color: theme.colorScheme.onSurfaceVariant),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _body() {
    if (_error != null) return ErrorState(message: _error!, onRetry: _load);
    if (_doc == null) return const DelayedProgress();
    return Column(children: [
      if (_gone)
        Padding(
          padding: const EdgeInsets.fromLTRB(Space.l, Space.s, Space.l, 0),
          child: StatusBanner(
            icon: Icons.warning_rounded,
            text: Fr.storageGoneKeepText,
            actions: [
              TextButton(
                onPressed: () => Clipboard.setData(ClipboardData(text: _ctrl.text)),
                child: const Text(Fr.copyText),
              ),
            ],
          ),
        ),
      Expanded(
        child: TextField(
          controller: _ctrl,
          readOnly: _readOnly,
          maxLines: null,
          expands: true,
          keyboardType: TextInputType.multiline,
          // texte d'un disque chiffré : le clavier ne l'apprend pas et ne le corrige pas
          enableIMEPersonalizedLearning: false,
          enableSuggestions: false,
          autocorrect: false,
          textAlignVertical: TextAlignVertical.top,
          style: const TextStyle(fontFamily: 'monospace', fontSize: 14),
          // pas de cadre : la page entière est le champ
          decoration: const InputDecoration(border: InputBorder.none, contentPadding: EdgeInsets.all(Space.m)),
        ),
      ),
      _statusLine(),
    ]);
  }

  @override
  Widget build(BuildContext context) => PopScope(
        canPop: !_dirty,
        onPopInvokedWithResult: (didPop, _) {
          if (!didPop) _leave();
        },
        child: Scaffold(
          appBar: AppBar(
            title: Text(_dirty ? '• ${widget.name}' : widget.name, overflow: TextOverflow.ellipsis),
            actions: [
              if (_doc != null && !_readOnly)
                IconButton(
                  tooltip: Fr.save,
                  icon: const Icon(Icons.save_rounded),
                  onPressed: _dirty && !_saving && !_gone ? () => _save() : null,
                ),
            ],
          ),
          body: _body(),
        ),
      );
}
