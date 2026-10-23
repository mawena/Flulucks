import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../screens/format_screen.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/tokens.dart';
import '../ui/widgets.dart';
import 'partition_tile.dart';
import 'source_tile.dart';

/// Carte d'un disque USB : en-tête (icône, nom, taille, menu), accès, analyse, puis ses partitions.
class DiskCard extends StatelessWidget {
  const DiskCard({super.key, required this.state, required this.disk});

  final AppState state;
  final DiskMsg disk;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(Space.l, Space.m, Space.xs, Space.l),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(children: [
              const TonalIcon(Icons.usb_rounded),
              const SizedBox(width: Space.l),
              Expanded(
                child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                  Text(disk.name, style: theme.textTheme.titleMedium),
                  Text(Fr.humanSize(disk.sizeBytes), style: numeric(theme.textTheme.bodySmall!)),
                ]),
              ),
              if (disk.hasPermission) _menu(context) else const SizedBox(height: Layout.minTouch),
            ]),
            Padding(
              padding: const EdgeInsets.only(right: Space.m),
              child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
                if (disk.error != null)
                  Padding(
                    padding: const EdgeInsets.only(top: Space.m),
                    child: StatusBanner(icon: Icons.error_rounded, text: disk.error!, tone: ChipTone.danger),
                  ),
                // Une erreur fixe (UAS seul, interface occupée…) ne se règle pas en accordant l'accès.
                if (!disk.hasPermission && disk.error == null)
                  Padding(
                    padding: const EdgeInsets.only(top: Space.m),
                    child: Align(
                      alignment: AlignmentDirectional.centerStart,
                      child: FilledButton(
                        onPressed: () => state.requestPermission(disk.id),
                        child: const Text(Fr.grantAccess),
                      ),
                    ),
                  )
                else if (disk.partitions.isEmpty && disk.error == null)
                  const Padding(padding: EdgeInsets.only(top: Space.m), child: Text(Fr.scanning))
                else
                  for (final (i, p) in disk.partitions.indexed) ...[
                    if (i > 0) const Divider(height: Space.l),
                    PartitionTile(state: state, disk: disk, partition: p),
                  ],
              ]),
            ),
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
      icon: const Icon(Icons.more_vert_rounded),
      onSelected: (_) => Navigator.of(context)
          .push(MaterialPageRoute<void>(builder: (_) => FormatScreen(state: state, disk: disk))),
      itemBuilder: (_) => [
        PopupMenuItem<String>(
          value: 'format',
          enabled: !open,
          child: ListTile(
            contentPadding: EdgeInsets.zero,
            enabled: !open,
            leading: const Icon(Icons.cleaning_services_rounded),
            title: const Text(Fr.formatMenu),
            subtitle: open ? const Text(Fr.formatEjectFirst) : null,
          ),
        ),
      ],
    );
  }
}
