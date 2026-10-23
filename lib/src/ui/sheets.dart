import 'package:flutter/material.dart';

import 'tokens.dart';

/// Action d'une feuille du bas : valeur rendue, icône, libellé ; [destructive] : couleur danger.
class SheetAction<T> {
  const SheetAction(this.value, this.icon, this.label, {this.enabled = true, this.subtitle, this.destructive = false});

  final T value;
  final IconData icon;
  final String label;
  final bool enabled;
  final String? subtitle;
  final bool destructive;
}

/// Feuille d'actions (poignée, titre facultatif) ; rend la valeur de l'action touchée, ou null si fermée.
/// Défilable : une longue liste ou un texte à 200 % tiennent dans l'écran.
Future<T?> showActionSheet<T>(BuildContext context, {String? title, required List<SheetAction<T>> actions}) =>
    showModalBottomSheet<T>(
      context: context,
      isScrollControlled: true,
      useSafeArea: true,
      builder: (ctx) {
        final theme = Theme.of(ctx);
        final s = theme.colorScheme;
        return SafeArea(
          child: ListView(shrinkWrap: true, padding: const EdgeInsets.only(bottom: Space.s), children: [
            if (title != null)
              Padding(
                padding: const EdgeInsets.fromLTRB(Space.xl, 0, Space.xl, Space.s),
                child: Text(
                  title,
                  style: theme.textTheme.titleMedium,
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                ),
              ),
            for (final a in actions)
              ListTile(
                enabled: a.enabled,
                contentPadding: const EdgeInsets.symmetric(horizontal: Space.xl),
                leading: Icon(a.icon),
                iconColor: a.destructive ? s.error : null,
                textColor: a.destructive ? s.error : null,
                title: Text(a.label),
                subtitle: a.subtitle == null ? null : Text(a.subtitle!),
                onTap: () => Navigator.pop(ctx, a.value),
              ),
          ]),
        );
      },
    );
