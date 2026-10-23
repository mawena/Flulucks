import 'package:flutter/material.dart';

import '../screens/filter_screen.dart';
import '../ui/tokens.dart';

/// Raccourcis de l'accueil (les 6 filtres rapides) en tuiles : icône dans un cercle tonal, libellé dessous.
/// 6, 3 ou 2 colonnes selon la largeur **disponible** (l'accueil en deux colonnes n'en donne que la moitié) et la
/// taille du texte : chaque tuile garde au moins [minTile] dp par point de taille de texte, pour qu'aucun libellé
/// (« Documents ») ne se coupe au milieu d'un mot.
class ShortcutGrid extends StatelessWidget {
  const ShortcutGrid({super.key, required this.onOpen});

  final void Function(String id, String label) onOpen;

  static const minTile = 96.0;

  @override
  Widget build(BuildContext context) {
    final scale = MediaQuery.textScalerOf(context).scale(1);
    return LayoutBuilder(builder: (context, c) {
      final fit = c.maxWidth / (minTile * scale);
      final columns = fit >= 6 ? 6 : fit >= 3 ? 3 : 2;
      final width = (c.maxWidth - Space.s * (columns - 1)) / columns;
      return Wrap(spacing: Space.s, runSpacing: Space.s, children: [
        for (final (id, label, icon) in quickFilters)
          SizedBox(width: width, child: _ShortcutTile(icon: icon, label: label, onTap: () => onOpen(id, label))),
      ]);
    });
  }
}

class _ShortcutTile extends StatelessWidget {
  const _ShortcutTile({required this.icon, required this.label, required this.onTap});

  final IconData icon;
  final String label;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final s = theme.colorScheme;
    return Semantics(
      button: true,
      child: InkWell(
        borderRadius: BorderRadius.circular(Radii.l),
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: Space.s, horizontal: Space.xs),
          child: Column(mainAxisSize: MainAxisSize.min, children: [
            Container(
              width: 56,
              height: 56,
              decoration: BoxDecoration(color: s.secondaryContainer, shape: BoxShape.circle),
              child: Icon(icon, color: s.onSecondaryContainer),
            ),
            const SizedBox(height: Space.s),
            Text(label, style: theme.textTheme.labelLarge, textAlign: TextAlign.center, maxLines: 2),
          ]),
        ),
      ),
    );
  }
}
