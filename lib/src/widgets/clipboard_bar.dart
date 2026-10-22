import 'package:flutter/material.dart';

import '../state/app_state.dart';
import '../strings_fr.dart';

/// Presse-papiers non vide : « n élément(s) à copier », Coller ici (désactivé en lecture seule), Annuler.
class ClipboardBar extends StatelessWidget {
  const ClipboardBar({super.key, required this.state, required this.canPaste, required this.onPaste});

  final AppState state;
  final bool canPaste;
  final VoidCallback onPaste;

  @override
  Widget build(BuildContext context) {
    final c = state.clipboard;
    if (c == null) return const SizedBox.shrink();
    return Material(
      color: Theme.of(context).colorScheme.secondaryContainer,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 4, 8, 4),
        child: Row(children: [
          Expanded(child: Text(Fr.clipboard(c.paths.length, c.move))),
          TextButton(onPressed: state.clearClipboard, child: const Text(Fr.cancel)),
          FilledButton(onPressed: canPaste ? onPaste : null, child: const Text(Fr.pasteHere)),
        ]),
      ),
    );
  }
}
