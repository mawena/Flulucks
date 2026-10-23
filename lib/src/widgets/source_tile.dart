import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../strings_fr.dart';

/// Icône d'un stockage selon son type (`phone`, `removable` : carte SD ou clé montée par Android, `disk`, `remote`).
IconData sourceIcon(String kind) => switch (kind) {
      'phone' => Icons.smartphone,
      'removable' => Icons.sd_storage,
      'remote' => Icons.cloud_outlined,
      _ => Icons.usb,
    };

/// Un stockage de l'accueil : libellé, place libre et barre d'occupation (« Espace inconnu » pour un dossier distant) ;
/// accès à sa corbeille si [onTrash].
class SourceTile extends StatelessWidget {
  const SourceTile({super.key, required this.source, required this.onTap, this.onTrash});

  final SourceMsg source;
  final VoidCallback? onTap;
  final VoidCallback? onTrash;

  @override
  Widget build(BuildContext context) {
    final total = source.totalBytes;
    final used = total > 0 ? ((total - source.freeBytes) / total).clamp(0.0, 1.0) : 0.0;
    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      child: ListTile(
        leading: Icon(sourceIcon(source.kind)),
        title: Text(source.label),
        subtitle: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(total < 0 ? Fr.unknownSpace : Fr.freeOf(source.freeBytes, source.totalBytes)),
            if (total >= 0) ...[
              const SizedBox(height: 6),
              LinearProgressIndicator(value: used.toDouble()),
            ],
          ],
        ),
        trailing: onTrash == null
            ? const Icon(Icons.chevron_right)
            : IconButton(tooltip: Fr.trash, icon: const Icon(Icons.delete_outline), onPressed: onTrash),
        onTap: onTap,
      ),
    );
  }
}
