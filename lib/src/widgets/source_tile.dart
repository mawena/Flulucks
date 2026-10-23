import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../strings_fr.dart';
import '../ui/tokens.dart';
import '../ui/widgets.dart';

/// Icône d'un stockage selon son type (`phone`, `removable` : carte SD ou clé montée par Android, `disk`, `remote`).
IconData sourceIcon(String kind) => switch (kind) {
      'phone' => Icons.smartphone_rounded,
      'removable' => Icons.sd_storage_rounded,
      'remote' => Icons.cloud_outlined,
      _ => Icons.usb_rounded,
    };

/// Icône dans un cercle tonal (`secondaryContainer`), en tête des tuiles de l'accueil.
class TonalIcon extends StatelessWidget {
  const TonalIcon(this.icon, {super.key});

  final IconData icon;

  @override
  Widget build(BuildContext context) {
    final s = Theme.of(context).colorScheme;
    return Container(
      width: 40,
      height: 40,
      decoration: BoxDecoration(color: s.secondaryContainer, shape: BoxShape.circle),
      child: Icon(icon, size: 22, color: s.onSecondaryContainer),
    );
  }
}

/// Un stockage de l'accueil : libellé, place libre en chiffres tabulaires et jauge d'occupation (« Espace
/// inconnu » pour un dossier distant) ; menu « Options de … » : corbeille et analyse de l'espace, si données.
class SourceTile extends StatelessWidget {
  const SourceTile({super.key, required this.source, required this.onTap, this.onTrash, this.onSpace});

  final SourceMsg source;
  final VoidCallback? onTap;
  final VoidCallback? onTrash;
  final VoidCallback? onSpace;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final total = source.totalBytes;
    final used = total > 0 ? ((total - source.freeBytes) / total).clamp(0.0, 1.0) : 0.0;
    return Card(
      clipBehavior: Clip.antiAlias,
      child: ListTile(
        contentPadding: const EdgeInsetsDirectional.only(start: Space.l, end: Space.xs),
        leading: TonalIcon(sourceIcon(source.kind)),
        title: Text(source.label),
        subtitle: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              total < 0 ? Fr.unknownSpace : Fr.freeOf(source.freeBytes, source.totalBytes),
              style: numeric(theme.textTheme.bodyMedium!),
            ),
            if (total >= 0) ...[const SizedBox(height: Space.s), UsageBar(used: used.toDouble())],
          ],
        ),
        trailing: onTrash == null && onSpace == null
            ? const Icon(Icons.chevron_right_rounded)
            : PopupMenuButton<String>(
                tooltip: Fr.sourceMenu(source.label),
                icon: const Icon(Icons.more_vert_rounded),
                onSelected: (v) => (v == 'trash' ? onTrash : onSpace)?.call(),
                itemBuilder: (_) => [
                  if (onTrash != null)
                    const PopupMenuItem(
                      value: 'trash',
                      child: ListTile(leading: Icon(Icons.delete_outline_rounded), title: Text(Fr.trash)),
                    ),
                  if (onSpace != null)
                    const PopupMenuItem(
                      value: 'space',
                      child: ListTile(leading: Icon(Icons.pie_chart_outline_rounded), title: Text(Fr.spaceAnalysis)),
                    ),
                ],
              ),
        onTap: onTap,
      ),
    );
  }
}
