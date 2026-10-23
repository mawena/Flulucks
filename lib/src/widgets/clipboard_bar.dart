import 'package:flutter/material.dart';

import '../state/app_state.dart';
import '../strings_fr.dart';

/// Action en attente d'un dossier : presse-papiers (« n élément(s) à copier », Coller ici) ou archive choisie
/// par « Extraire vers… » (Extraire ici) ; bouton désactivé en lecture seule, Annuler.
class ClipboardBar extends StatelessWidget {
  const ClipboardBar(
      {super.key, required this.state, required this.canPaste, required this.onPaste, required this.onExtract});

  final AppState state;
  final bool canPaste;
  final VoidCallback onPaste;
  final VoidCallback onExtract;

  @override
  Widget build(BuildContext context) {
    final c = state.clipboard;
    final x = state.pendingExtract;
    if (c == null && x == null) return const SizedBox.shrink();
    final paste = c != null;
    final text = c != null ? Fr.clipboard(c.paths.length, c.move) : Fr.toExtract(x!.path.substring(x.path.lastIndexOf('/') + 1));
    return Material(
      color: Theme.of(context).colorScheme.secondaryContainer,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 4, 8, 4),
        child: Row(children: [
          Expanded(child: Text(text)),
          TextButton(onPressed: paste ? state.clearClipboard : state.clearPendingExtract, child: const Text(Fr.cancel)),
          FilledButton(
            onPressed: canPaste ? (paste ? onPaste : onExtract) : null,
            child: Text(paste ? Fr.pasteHere : Fr.extractHere),
          ),
        ]),
      ),
    );
  }
}
