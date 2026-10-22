import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../screens/format_screen.dart';
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
            Row(children: [
              Expanded(child: Text(disk.name, style: theme.textTheme.titleMedium)),
              if (disk.hasPermission) _menu(context),
            ]),
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

  /// « Formater… », désactivé avec la raison tant qu'un volume du disque est ouvert.
  Widget _menu(BuildContext context) {
    final open = state.volumes.any((v) => v.diskId == disk.id);
    return PopupMenuButton<String>(
      tooltip: Fr.diskMenu,
      onSelected: (_) => Navigator.of(context)
          .push(MaterialPageRoute<void>(builder: (_) => FormatScreen(state: state, disk: disk))),
      itemBuilder: (_) => [
        PopupMenuItem<String>(
          value: 'format',
          enabled: !open,
          child: ListTile(
            contentPadding: EdgeInsets.zero,
            enabled: !open,
            leading: const Icon(Icons.cleaning_services_outlined),
            title: const Text(Fr.formatMenu),
            subtitle: open ? const Text(Fr.formatEjectFirst) : null,
          ),
        ),
      ],
    );
  }
}
