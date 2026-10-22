import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../screens/explorer_screen.dart';
import '../strings_fr.dart';
import 'unlock_dialog.dart';

class PartitionTile extends StatelessWidget {
  const PartitionTile({super.key, required this.state, required this.disk, required this.partition});

  final AppState state;
  final DiskMsg disk;
  final PartitionMsg partition;

  @override
  Widget build(BuildContext context) {
    final volumeId = partition.volumeId;
    final volume = volumeId == null ? null : state.volumeById(volumeId);
    final line = Fr.partitionLine(partition.label, partition.index, partition.sizeBytes, partition.content);
    final theme = Theme.of(context);

    final children = <Widget>[Text(line, style: theme.textTheme.bodyLarge)];
    if (volumeId != null) {
      // Ouverte : jamais de « Déverrouiller » (la phrase de passe ne serait pas revérifiée).
      final readOnly = volume?.readOnly ?? true;
      children.add(Text(readOnly ? Fr.openedInFiles : Fr.openedInFilesRw));
      if (volume != null && volume.readOnly && volume.roReason.isNotEmpty) {
        children.add(Text(Fr.readOnly(volume.roReason)));
      }
      if (volume != null && volume.needsRecovery) {
        children.add(Text(Fr.needsRecovery, style: TextStyle(color: theme.colorScheme.error)));
      }
      children.add(
        Wrap(
          spacing: 8,
          children: [
            FilledButton(
              onPressed: state.ejecting ? null : () => openExplorer(context, state, volumeId),
              child: const Text(Fr.browse),
            ),
            FilledButton.tonal(
              onPressed: state.ejecting ? null : () => state.openInFiles(volumeId),
              child: const Text(Fr.openInFiles),
            ),
            OutlinedButton(onPressed: state.ejecting ? null : () => state.eject(volumeId), child: const Text(Fr.eject)),
          ],
        ),
      );
    } else if (!partition.supported) {
      children.add(Text(Fr.unsupported(partition.content)));
    } else if (partition.encrypted) {
      children.add(
        Align(
          alignment: Alignment.centerLeft,
          child: FilledButton(
            onPressed: state.ejecting
                ? null
                : () => showDialog<bool>(
                    context: context,
                    barrierDismissible: false,
                    builder: (_) => UnlockDialog(
                      state: state,
                      diskId: disk.id,
                      partIndex: partition.index,
                      writable: !state.readOnlyPreference,
                      partitionName: partition.label.isEmpty ? Fr.partitionName(partition.index) : partition.label,
                    ),
                  ),
            child: const Text(Fr.unlock),
          ),
        ),
      );
    } else {
      children.add(
        Align(
          alignment: Alignment.centerLeft,
          child: FilledButton(
            onPressed: state.ejecting ? null : () => state.mountPlain(disk.id, partition.index),
            child: const Text(Fr.open),
          ),
        ),
      );
    }

    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, spacing: 4, children: children),
    );
  }
}
