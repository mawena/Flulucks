import 'dart:async';

import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/feedback.dart';
import '../ui/tokens.dart';

/// Barre de l'opération en cours (copie, déplacement, compression, extraction) : progression et Annuler ; à la fin,
/// le résultat pendant 4 s. Une seule pour toute l'application : le cadre (`PlayerFrame`) la pose dans le dock du
/// bas, sous tous les écrans.
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
          if (!t.cancelled && t.error == null) AppHaptics.success(widget.state); // copie terminée
          _timer?.cancel();
          _timer = Timer(const Duration(seconds: 4), () {
            if (identical(widget.state.transfer, t)) widget.state.transferResultSeen();
          });
        }
        final theme = Theme.of(context);
        final pct = t.totalBytes > 0 ? (t.doneBytes * 100 ~/ t.totalBytes).clamp(0, 100) : 0;
        final String text;
        // résultat : icône de réussite ou d'échec, comme les messages
        final IconData? icon = !t.finished
            ? null
            : t.error != null
                ? Icons.error_rounded
                : t.cancelled
                    ? Icons.cancel_rounded
                    : Icons.check_circle_rounded;
        if (!t.finished) {
          text = Fr.transferLine(t.kind, t.currentName, t.fileIndex, t.fileCount, pct);
        } else if (t.cancelled) {
          text = Fr.transferCancelled;
        } else {
          text = t.error ?? Fr.transferDoneRate(t.fileIndex, t.doneBytes, t.elapsedMs);
        }
        return Padding(
          padding: const EdgeInsets.fromLTRB(Space.l, Space.s, Space.s, Space.s),
          child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start, children: [
            Row(children: [
              if (icon != null) ...[
                ExcludeSemantics(
                  child: Icon(icon, color: t.error != null ? theme.colorScheme.error : theme.colorScheme.primary),
                ),
                const SizedBox(width: Space.m),
              ],
              Expanded(
                child: Text(
                  text,
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                  style: numeric(theme.textTheme.bodyMedium!),
                ),
              ),
              if (!t.finished) TextButton(onPressed: widget.state.cancelTransfer, child: const Text(Fr.cancel)),
            ]),
            if (!t.finished) LinearProgressIndicator(value: t.totalBytes > 0 ? pct / 100 : null),
          ]),
        );
      },
    );
  }
}
