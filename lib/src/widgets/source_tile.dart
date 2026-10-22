import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../strings_fr.dart';

/// Icône d'un stockage selon son type (`phone`, `removable` : carte SD ou clé montée par Android, `disk`).
IconData sourceIcon(String kind) => switch (kind) {
      'phone' => Icons.smartphone,
      'removable' => Icons.sd_storage,
      _ => Icons.usb,
    };

/// Un stockage de l'accueil : libellé, place libre et barre d'occupation.
class SourceTile extends StatelessWidget {
  const SourceTile({super.key, required this.source, required this.onTap});

  final SourceMsg source;
  final VoidCallback? onTap;

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
            Text(Fr.freeOf(source.freeBytes, source.totalBytes)),
            const SizedBox(height: 6),
            LinearProgressIndicator(value: used.toDouble()),
          ],
        ),
        trailing: const Icon(Icons.chevron_right),
        onTap: onTap,
      ),
    );
  }
}
