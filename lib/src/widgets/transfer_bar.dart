import 'dart:async';

import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';

/// Barre de la copie en cours : progression et Annuler ; à la fin, le résultat pendant 4 s, une seule fois
/// pour toute l'application (accueil et explorateurs).
class TransferBar extends StatefulWidget {
  const TransferBar({super.key, required this.state});

  final AppState state;

  @override
  State<TransferBar> createState() => _TransferBarState();
}

class _TransferBarState extends State<TransferBar> {
  TransferMsg? _timed;
  Timer? _timer;

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: widget.state,
      builder: (context, _) {
        final t = widget.state.transfer;
        if (t == null) return const SizedBox.shrink();
        if (t.finished && !identical(t, _timed)) {
          _timed = t;
          _timer?.cancel();
          _timer = Timer(const Duration(seconds: 4), () {
            if (identical(widget.state.transfer, t)) widget.state.transferResultSeen();
          });
        }
        final theme = Theme.of(context);
        final pct = t.totalBytes > 0 ? (t.doneBytes * 100 ~/ t.totalBytes).clamp(0, 100) : 0;
        final String text;
        if (!t.finished) {
          text = Fr.transferLine(t.move, t.currentName, t.fileIndex, t.fileCount, pct);
        } else if (t.cancelled) {
          text = Fr.transferCancelled;
        } else {
          text = t.error ?? Fr.transferDoneRate(t.fileIndex, t.doneBytes, t.elapsedMs);
        }
        return Material(
          color: theme.colorScheme.surfaceContainerHigh,
          child: SafeArea(
            top: false,
            child: Padding(
              padding: const EdgeInsets.fromLTRB(16, 8, 8, 8),
              child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start, children: [
                Row(children: [
                  Expanded(child: Text(text, maxLines: 2, overflow: TextOverflow.ellipsis)),
                  if (!t.finished) TextButton(onPressed: widget.state.cancelTransfer, child: const Text(Fr.cancel)),
                ]),
                if (!t.finished) LinearProgressIndicator(value: t.totalBytes > 0 ? pct / 100 : null),
              ]),
            ),
          ),
        );
      },
    );
  }
}
