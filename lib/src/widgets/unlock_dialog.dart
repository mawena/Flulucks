import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/feedback.dart';
import '../ui/tokens.dart';
import '../ui/widgets.dart';

/// Dialogue de saisie de la phrase de passe. Les octets UTF-8 envoyés sont mis à zéro après l'appel
/// (réussi ou non) et le champ est vidé ; la phrase n'est jamais conservée ailleurs. Afficher ou masquer la phrase
/// ne change que son rendu (aucune copie). La case « Ouvrir en lecture seule » fixe l'écriture demandée.
class UnlockDialog extends StatefulWidget {
  const UnlockDialog({
    super.key,
    required this.state,
    required this.diskId,
    required this.partIndex,
    required this.partitionName,
    this.writable = true,
  });

  final AppState state;
  final String diskId;
  final int partIndex;
  final String partitionName;

  /// Écriture demandée au départ (faux si « Ouvrir les disques en lecture seule » est actif) : la case
  /// « Ouvrir en lecture seule » en part, et l'utilisateur peut la changer.
  final bool writable;

  @override
  State<UnlockDialog> createState() => _UnlockDialogState();
}

class _UnlockDialogState extends State<UnlockDialog> {
  final _controller = TextEditingController();
  bool _busy = false;
  String? _error;
  bool _obscure = true;
  late bool _readOnly = !widget.writable;

  @override
  void dispose() {
    _controller.clear();
    _controller.dispose();
    super.dispose();
  }

  Future<void> _submit() async {
    if (_busy || _controller.text.isEmpty) return;
    final Uint8List pass = utf8.encode(
      _controller.text,
    ); // un seul tampon, celui qui sera mis à zéro
    setState(() {
      _busy = true;
      _error = null;
    });
    String? error;
    var ok = false;
    try {
      await widget.state.unlock(widget.diskId, widget.partIndex, pass, writable: !_readOnly);
      ok = true;
    } on PlatformException catch (e) {
      error = e.message ?? Fr.genericError;
    } catch (_) {
      error = Fr.genericError; // jamais le contenu de l'exception
    } finally {
      pass.fillRange(0, pass.length, 0);
      if (mounted) _controller.clear();
    }
    if (!mounted) return;
    if (ok) {
      AppHaptics.success(widget.state);
      Navigator.of(context).pop(true);
    } else {
      AppHaptics.warning(widget.state);
      setState(() {
        _busy = false;
        _error = error;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return PopScope(
      canPop: !_busy,
      child: AlertDialog(
        icon: const Icon(Icons.lock_rounded),
        title: Text(Fr.unlockTitle(widget.partitionName)),
        scrollable: true, // texte à 200 % : le contenu défile, les boutons restent visibles
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            TextField(
              controller: _controller,
              obscureText: _obscure,
              // affichée, elle n'est plus un champ de mot de passe pour le clavier : ni suggestions, ni apprentissage
              keyboardType: _obscure ? null : TextInputType.visiblePassword,
              autocorrect: false,
              enableSuggestions: false,
              enableIMEPersonalizedLearning: false,
              enabled: !_busy,
              autofocus: true,
              decoration: InputDecoration(
                labelText: Fr.passphrase,
                suffixIcon: IconButton(
                  tooltip: _obscure ? Fr.showPassphrase : Fr.hidePassphrase,
                  icon: Icon(_obscure ? Icons.visibility_rounded : Icons.visibility_off_rounded),
                  onPressed: () => setState(() => _obscure = !_obscure),
                ),
              ),
              onSubmitted: (_) => _submit(),
            ),
            const SizedBox(height: Space.s),
            CheckboxListTile(
              contentPadding: EdgeInsets.zero,
              controlAffinity: ListTileControlAffinity.leading,
              title: const Text(Fr.openReadOnly),
              value: _readOnly,
              onChanged: _busy ? null : (v) => setState(() => _readOnly = v ?? _readOnly),
            ),
            if (_busy) ...[
              const SizedBox(height: Space.s),
              const LinearProgressIndicator(),
              const SizedBox(height: Space.s),
              const Text(Fr.unlocking),
            ],
            if (_error != null) ...[
              const SizedBox(height: Space.s),
              StatusBanner(icon: Icons.error_rounded, text: _error!, tone: ChipTone.danger),
            ],
          ],
        ),
        actions: [
          TextButton(
            onPressed: _busy ? null : () => Navigator.of(context).pop(false),
            child: const Text(Fr.cancel),
          ),
          FilledButton(
            onPressed: _busy ? null : _submit,
            child: const Text(Fr.unlock),
          ),
        ],
      ),
    );
  }
}
