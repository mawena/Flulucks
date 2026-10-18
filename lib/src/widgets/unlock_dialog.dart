import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../state/app_state.dart';
import '../strings_fr.dart';

/// Dialogue de saisie de la phrase de passe. Les octets UTF-8 envoyés sont mis à zéro après l'appel
/// (réussi ou non) et le champ est vidé ; la phrase n'est jamais conservée ailleurs.
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

  /// Écriture demandée (faux si « Ouvrir les disques en lecture seule » est actif).
  final bool writable;

  @override
  State<UnlockDialog> createState() => _UnlockDialogState();
}

class _UnlockDialogState extends State<UnlockDialog> {
  final _controller = TextEditingController();
  bool _busy = false;
  String? _error;

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
      await widget.state.unlock(widget.diskId, widget.partIndex, pass, writable: widget.writable);
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
      Navigator.of(context).pop(true);
    } else {
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
        title: Text(Fr.unlockTitle(widget.partitionName)),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            TextField(
              controller: _controller,
              obscureText: true,
              autocorrect: false,
              enableSuggestions: false,
              enabled: !_busy,
              autofocus: true,
              decoration: const InputDecoration(labelText: Fr.passphrase),
              onSubmitted: (_) => _submit(),
            ),
            if (_busy) ...[
              const SizedBox(height: 16),
              const Row(
                children: [
                  SizedBox(
                    width: 20,
                    height: 20,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  ),
                  SizedBox(width: 12),
                  Expanded(child: Text(Fr.unlocking)),
                ],
              ),
            ],
            if (_error != null) ...[
              const SizedBox(height: 16),
              Text(
                _error!,
                style: TextStyle(color: Theme.of(context).colorScheme.error),
              ),
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
