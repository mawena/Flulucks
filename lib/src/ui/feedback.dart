import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../state/app_state.dart';
import 'tokens.dart';

enum MessageKind { info, success, error }

/// Message flottant en bas de l'écran, avec une icône selon sa nature ; il remplace le message affiché au lieu
/// d'attendre derrière lui. Sans `ScaffoldMessenger` (écran déjà fermé), rien n'est affiché.
void showMessage(
  BuildContext context,
  String text, {
  MessageKind kind = MessageKind.info,
  String? actionLabel,
  VoidCallback? onAction,
}) {
  final messenger = ScaffoldMessenger.maybeOf(context);
  if (messenger == null) return;
  final s = Theme.of(context).colorScheme;
  final (icon, color) = switch (kind) {
    MessageKind.success => (Icons.check_circle_rounded, s.inversePrimary),
    MessageKind.error => (Icons.error_rounded, s.errorContainer),
    MessageKind.info => (Icons.info_rounded, s.onInverseSurface),
  };
  messenger
    ..hideCurrentSnackBar()
    ..showSnackBar(SnackBar(
      behavior: SnackBarBehavior.floating,
      content: Row(children: [
        ExcludeSemantics(child: Icon(icon, color: color)),
        const SizedBox(width: Space.m),
        Expanded(child: Text(text)),
      ]),
      action: actionLabel == null ? null : SnackBarAction(label: actionLabel, onPressed: onAction ?? () {}),
    ));
}

/// Retours haptiques, selon la préférence de l'utilisateur (Réglages › Retours haptiques).
abstract final class AppHaptics {
  /// Sélection : appui long, « Tout sélectionner ».
  static Future<void> selection(AppState s) => s.uiPrefs.haptics ? HapticFeedback.selectionClick() : Future.value();

  /// Réussite : disque déverrouillé, copie terminée.
  static Future<void> success(AppState s) => s.uiPrefs.haptics ? HapticFeedback.lightImpact() : Future.value();

  /// Échec ou confirmation destructive : phrase de passe refusée, suppression définitive.
  static Future<void> warning(AppState s) => s.uiPrefs.haptics ? HapticFeedback.mediumImpact() : Future.value();
}
