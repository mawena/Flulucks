import 'dart:convert';

import 'package:flutter/material.dart';

import '../strings_fr.dart';

/// Demande une confirmation ; vrai seulement si [confirmLabel] est touché. [destructive] : bouton couleur danger.
Future<bool> confirmAction(
  BuildContext context, {
  required String title,
  String? body,
  required String confirmLabel,
  bool destructive = false,
}) async {
  final s = Theme.of(context).colorScheme;
  final ok = await showDialog<bool>(
    context: context,
    builder: (ctx) => AlertDialog(
      title: Text(title),
      content: body == null ? null : Text(body),
      actions: [
        TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text(Fr.cancel)),
        FilledButton(
          style: destructive ? FilledButton.styleFrom(backgroundColor: s.error, foregroundColor: s.onError) : null,
          onPressed: () => Navigator.pop(ctx, true),
          child: Text(confirmLabel),
        ),
      ],
    ),
  );
  return ok == true;
}

/// Raison pour laquelle [name] ne peut pas nommer un fichier ou un dossier, ou null s'il convient : vide, « . »,
/// « .. », « / », caractère NUL, plus de 255 octets en UTF-8 (limite des systèmes de fichiers).
String? nameError(String name) {
  if (name.trim().isEmpty) return Fr.nameEmpty;
  if (name == '.' || name == '..') return Fr.nameReserved;
  if (name.contains('/')) return Fr.nameSlash;
  if (name.contains('\u0000')) return Fr.nameBadChar;
  if (utf8.encode(name).length > 255) return Fr.nameTooLong;
  return null;
}

/// Demande un texte (nom de fichier, de dossier, d'archive) ; null si annulé. [selectBaseName] : seul le nom avant
/// l'extension est sélectionné (« rapport » dans « rapport.pdf »). [validator] : message d'erreur, qui désactive OK.
Future<String?> promptText(
  BuildContext context, {
  required String title,
  required String label,
  String initial = '',
  bool selectBaseName = false,
  String? Function(String)? validator,
}) =>
    showDialog<String>(
      context: context,
      builder: (_) => _PromptDialog(
        title: title,
        label: label,
        initial: initial,
        selectBaseName: selectBaseName,
        validator: validator,
      ),
    );

class _PromptDialog extends StatefulWidget {
  const _PromptDialog({
    required this.title,
    required this.label,
    required this.initial,
    required this.selectBaseName,
    required this.validator,
  });

  final String title;
  final String label;
  final String initial;
  final bool selectBaseName;
  final String? Function(String)? validator;

  @override
  State<_PromptDialog> createState() => _PromptDialogState();
}

class _PromptDialogState extends State<_PromptDialog> {
  late final TextEditingController _c;
  String? _error;

  @override
  void initState() {
    super.initState();
    final text = widget.initial;
    final dot = text.lastIndexOf('.');
    final end = widget.selectBaseName && dot > 0 ? dot : text.length;
    _c = TextEditingController.fromValue(
      TextEditingValue(text: text, selection: TextSelection(baseOffset: 0, extentOffset: end)),
    );
    _error = widget.validator?.call(text);
  }

  @override
  void dispose() {
    _c.dispose();
    super.dispose();
  }

  void _submit() {
    // revalidé ici : Entrée peut arriver avant que le bouton ne reflète la dernière frappe
    final error = widget.validator?.call(_c.text);
    if (error != null) {
      setState(() => _error = error);
      return;
    }
    Navigator.pop(context, _c.text);
  }

  @override
  Widget build(BuildContext context) {
    // erreur affichée seulement après une saisie : un champ vide à l'ouverture n'est pas une faute
    final shown = _c.text == widget.initial && widget.initial.isEmpty ? null : _error;
    return AlertDialog(
      title: Text(widget.title),
      content: TextField(
        controller: _c,
        autofocus: true,
        decoration: InputDecoration(labelText: widget.label, errorText: shown, errorMaxLines: 2),
        onChanged: (v) => setState(() => _error = widget.validator?.call(v)),
        onSubmitted: (_) => _submit(),
      ),
      actions: [
        TextButton(onPressed: () => Navigator.pop(context), child: const Text(Fr.cancel)),
        FilledButton(onPressed: _error == null ? _submit : null, child: const Text(Fr.ok)),
      ],
    );
  }
}
