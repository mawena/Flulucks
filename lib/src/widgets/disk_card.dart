import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import 'partition_tile.dart';

/// Carte d'un disque USB : accès, analyse, puis ses partitions.
class DiskCard extends StatelessWidget {
  const DiskCard({super.key, required this.state, required this.disk});

  final AppState state;
  final DiskMsg disk;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Card(
      margin: const EdgeInsets.only(bottom: 12),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(disk.name, style: theme.textTheme.titleMedium),
            Text(Fr.humanSize(disk.sizeBytes), style: theme.textTheme.bodySmall),
            if (disk.error != null)
              Text(disk.error!, style: TextStyle(color: theme.colorScheme.error)),
            // Une erreur fixe (UAS seul, interface occupée…) ne se règle pas en accordant l'accès.
            if (!disk.hasPermission && disk.error == null)
              Padding(
                padding: const EdgeInsets.only(top: 8),
                child: FilledButton(
                  onPressed: () => state.requestPermission(disk.id),
                  child: const Text(Fr.grantAccess),
                ),
              )
            else if (disk.partitions.isEmpty && disk.error == null)
              const Padding(padding: EdgeInsets.only(top: 8), child: Text(Fr.scanning))
            else
              for (final p in disk.partitions)
                PartitionTile(state: state, disk: disk, partition: p),
          ],
        ),
      ),
    );
  }
}
