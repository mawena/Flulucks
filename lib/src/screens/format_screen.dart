import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';

/// Système de fichiers proposé : identifiant passé au moteur, nom affiché, ligne d'aide.
typedef _Fs = ({String id, String name, String help});

const List<_Fs> _choices = [
  (id: 'exfat', name: 'exFAT', help: Fr.fsHelpExfat),
  (id: 'fat32', name: 'FAT32', help: Fr.fsHelpFat32),
  (id: 'ext4', name: 'ext4', help: Fr.fsHelpExt4),
  (id: 'ntfs', name: 'NTFS', help: Fr.fsHelpNtfs),
];

/// Limite de l'étiquette, affichée sous le champ (mêmes règles que le moteur, format/label.c).
String _labelLimit(String fs) => switch (fs) {
      'fat32' => Fr.labelLimitFat32,
      'ext4' => Fr.labelLimitBytes(16),
      'ntfs' => Fr.labelLimitChars(32),
      _ => Fr.labelLimitChars(11),
    };

/// Vrai si l'étiquette convient au système de fichiers (vide accepté).
bool labelOk(String fs, String label) {
  if (label.runes.any((c) => c < 0x20 || c == 0x7F)) return false;
  return switch (fs) {
    'fat32' => label.length <= 11 &&
        label.codeUnits.every((c) => c >= 0x20 && c <= 0x7E && !r'"*+,./:;<=>?[\]|'.contains(String.fromCharCode(c))),
    'ext4' => utf8.encode(label).length <= 16,
    'ntfs' => label.length <= 32, // unités UTF-16, comme Windows
    _ => label.length <= 11,
  };
}

/// Formatage d'un disque USB (phase 8) : système de fichiers, chiffrement, étiquette, puis confirmation en
/// tapant le nom du disque. Rien n'est envoyé au moteur avant la confirmation.
class FormatScreen extends StatefulWidget {
  const FormatScreen({super.key, required this.state, required this.disk});

  final AppState state;
  final DiskMsg disk;

  @override
  State<FormatScreen> createState() => _FormatScreenState();
}

class _FormatScreenState extends State<FormatScreen> {
  String _fs = 'exfat';
  bool _encrypted = false;
  final _label = TextEditingController();
  final _pass = TextEditingController();
  final _confirm = TextEditingController();
  String? _passError;
  String? _error;
  bool _busy = false;

  @override
  void initState() {
    super.initState();
    _label.addListener(() => setState(() {}));
  }

  @override
  void dispose() {
    _label.dispose();
    _pass.dispose();
    _confirm.dispose();
    super.dispose();
  }

  Future<void> _submit() async {
    setState(() => _error = null);
    if (!labelOk(_fs, _label.text)) return;
    if (_encrypted) {
      final err = _pass.text.isEmpty ? Fr.passEmpty : (_pass.text != _confirm.text ? Fr.passMismatch : null);
      setState(() => _passError = err);
      if (err != null) return;
    }
    final size = Fr.humanSize(widget.disk.sizeBytes);
    final ok = await showDialog<bool>(
      context: context,
      builder: (_) => _ConfirmDialog(name: widget.disk.name, size: size),
    );
    if (ok != true || !mounted) return;
    final pass = _encrypted ? Uint8List.fromList(utf8.encode(_pass.text)) : null;
    setState(() => _busy = true);
    final messenger = ScaffoldMessenger.of(context);
    final navigator = Navigator.of(context);
    final err = await widget.state.formatDisk(widget.disk.id, _fs, _encrypted, _label.text, pass);
    _pass.clear();
    _confirm.clear();
    if (!mounted) return;
    if (err == null) {
      navigator.pop();
      messenger.showSnackBar(const SnackBar(content: Text(Fr.formatted)));
    } else {
      setState(() {
        _busy = false;
        _error = err;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final labelValid = labelOk(_fs, _label.text);
    return PopScope(
      canPop: !_busy,
      child: Scaffold(
        appBar: AppBar(title: const Text(Fr.formatTitle)),
        body: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            Text(widget.disk.name, style: theme.textTheme.titleMedium),
            Text(Fr.humanSize(widget.disk.sizeBytes), style: theme.textTheme.bodySmall),
            const SizedBox(height: 8),
            RadioGroup<String>(
              groupValue: _fs,
              onChanged: (v) {
                if (!_busy && v != null) setState(() => _fs = v);
              },
              child: Column(children: [
                for (final c in _choices)
                  RadioListTile<String>(
                    contentPadding: EdgeInsets.zero,
                    value: c.id,
                    enabled: !_busy,
                    title: Text(c.name),
                    subtitle: Text(c.help),
                  ),
              ]),
            ),
            TextField(
              controller: _label,
              enabled: !_busy,
              decoration: InputDecoration(
                labelText: Fr.formatLabel,
                helperText: labelValid ? _labelLimit(_fs) : null,
                errorText: labelValid ? null : Fr.labelInvalid,
              ),
            ),
            const SizedBox(height: 8),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text(Fr.encryptSwitch),
              subtitle: const Text(Fr.encryptHelp),
              value: _encrypted,
              onChanged: _busy ? null : (v) => setState(() => _encrypted = v),
            ),
            if (_encrypted) ...[
              TextField(
                controller: _pass,
                enabled: !_busy,
                obscureText: true,
                enableSuggestions: false,
                autocorrect: false,
                decoration: const InputDecoration(labelText: Fr.passphrase),
              ),
              TextField(
                controller: _confirm,
                enabled: !_busy,
                obscureText: true,
                enableSuggestions: false,
                autocorrect: false,
                decoration: InputDecoration(labelText: Fr.passConfirm, errorText: _passError),
              ),
            ],
            const SizedBox(height: 16),
            if (_error != null)
              Padding(
                padding: const EdgeInsets.only(bottom: 8),
                child: Text(_error!, style: TextStyle(color: theme.colorScheme.error)),
              ),
            if (_busy)
              ListenableBuilder(
                listenable: widget.state,
                builder: (context, _) {
                  final pct = widget.state.formatProgress ?? 0;
                  return Column(crossAxisAlignment: CrossAxisAlignment.start, spacing: 8, children: [
                    LinearProgressIndicator(value: pct / 100),
                    Text(Fr.formatting(pct)),
                  ]);
                },
              )
            else
              FilledButton(onPressed: _submit, child: const Text(Fr.formatButton)),
          ],
        ),
      ),
    );
  }
}

/// « Tapez le nom du disque pour confirmer » : le bouton n'est actif que pour le nom exact.
class _ConfirmDialog extends StatefulWidget {
  const _ConfirmDialog({required this.name, required this.size});

  final String name;
  final String size;

  @override
  State<_ConfirmDialog> createState() => _ConfirmDialogState();
}

class _ConfirmDialogState extends State<_ConfirmDialog> {
  final _typed = TextEditingController();

  @override
  void dispose() {
    _typed.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final match = _typed.text == widget.name;
    return AlertDialog(
      title: const Text(Fr.formatTitle),
      content: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(Fr.formatConfirm(widget.name, widget.size)),
        TextField(
          controller: _typed,
          autofocus: true,
          decoration: const InputDecoration(labelText: Fr.formatConfirmHint),
          onChanged: (_) => setState(() {}),
        ),
      ]),
      actions: [
        TextButton(onPressed: () => Navigator.of(context).pop(false), child: const Text(Fr.cancel)),
        FilledButton(
          style: FilledButton.styleFrom(backgroundColor: Theme.of(context).colorScheme.error),
          onPressed: match ? () => Navigator.of(context).pop(true) : null,
          child: const Text(Fr.formatButton),
        ),
      ],
    );
  }
}
