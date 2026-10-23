import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../screens/explorer_screen.dart';
import '../screens/space_screen.dart';
import '../screens/trash_screen.dart';
import '../strings_fr.dart';
import '../ui/tokens.dart';
import '../ui/widgets.dart';
import 'unlock_dialog.dart';

/// Une partition d'un disque : cadenas fermé ou ouvert, puces d'état (système de fichiers, Chiffré, Verrouillé,
/// Ouvert, Lecture seule, non démonté proprement), puis l'action principale : Déverrouiller, Ouvrir, ou Parcourir
/// et Éjecter. Ouverte, son menu propose Ouvrir dans Fichiers, Corbeille et Analyse de l'espace.
class PartitionTile extends StatelessWidget {
  const PartitionTile({super.key, required this.state, required this.disk, required this.partition});

  final AppState state;
  final DiskMsg disk;
  final PartitionMsg partition;

  String get _name => partition.label.isEmpty ? Fr.partitionName(partition.index) : partition.label;

  @override
  Widget build(BuildContext context) {
    final volumeId = partition.volumeId;
    final volume = volumeId == null ? null : state.volumeById(volumeId);
    final line = Fr.partitionLine(partition.label, partition.index, partition.sizeBytes, partition.content);
    final theme = Theme.of(context);
    final s = theme.colorScheme;
    final open = volumeId != null;
    // Ouverte : jamais de « Déverrouiller » (la phrase de passe ne serait pas revérifiée).
    final readOnly = volume?.readOnly ?? true;

    final icon = partition.encrypted
        ? (open ? Icons.lock_open_rounded : Icons.lock_rounded)
        : Icons.storage_rounded;
    final children = <Widget>[
      Row(children: [
        ExcludeSemantics(child: Icon(icon, color: open ? s.primary : s.onSurfaceVariant)),
        const SizedBox(width: Space.m),
        Expanded(child: Text(line, style: numeric(theme.textTheme.bodyLarge!))),
        if (open) _menu(context, volumeId) else const SizedBox(height: Layout.minTouch),
      ]),
      Wrap(spacing: Space.s, runSpacing: Space.xs, children: [
        StatusChip(label: partition.content),
        if (partition.encrypted) const StatusChip(label: Fr.chipEncrypted, icon: Icons.key_rounded),
        if (partition.encrypted && !open) const StatusChip(label: Fr.chipLocked),
        if (open) const StatusChip(label: Fr.chipOpen, icon: Icons.check_rounded, tone: ChipTone.good),
        if (open && readOnly) const StatusChip(label: Fr.chipReadOnly, icon: Icons.edit_off_rounded, tone: ChipTone.warning),
        if (volume != null && volume.needsRecovery)
          const StatusChip(label: Fr.chipDirty, icon: Icons.warning_rounded, tone: ChipTone.danger),
      ]),
    ];
    if (open) {
      children.add(Text(readOnly ? Fr.openedInFiles : Fr.openedInFilesRw, style: theme.textTheme.bodyMedium));
      if (volume != null && volume.readOnly && volume.roReason.isNotEmpty) {
        children.add(Text(Fr.readOnly(volume.roReason), style: theme.textTheme.bodyMedium));
      }
      if (volume != null && volume.needsRecovery) {
        children.add(Text(Fr.needsRecovery, style: theme.textTheme.bodyMedium?.copyWith(color: s.error)));
      }
      children.add(
        Wrap(spacing: Space.s, runSpacing: Space.xs, children: [
          FilledButton.icon(
            onPressed: state.ejecting ? null : () => openExplorer(context, state, volumeId),
            icon: const Icon(Icons.folder_open_rounded),
            label: const Text(Fr.browse),
          ),
          OutlinedButton.icon(
            onPressed: state.ejecting ? null : () => state.eject(volumeId),
            icon: const Icon(Icons.eject_rounded),
            label: const Text(Fr.eject),
          ),
        ]),
      );
    } else if (!partition.supported) {
      children.add(Text(Fr.unsupported(partition.content), style: theme.textTheme.bodyMedium));
    } else if (partition.encrypted) {
      children.add(
        Align(
          alignment: AlignmentDirectional.centerStart,
          child: FilledButton.icon(
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
                      partitionName: _name,
                    ),
                  ),
            icon: const Icon(Icons.lock_open_rounded),
            label: const Text(Fr.unlock),
          ),
        ),
      );
    } else {
      children.add(
        Align(
          alignment: AlignmentDirectional.centerStart,
          child: FilledButton.icon(
            onPressed: state.ejecting ? null : () => state.mountPlain(disk.id, partition.index),
            icon: const Icon(Icons.folder_open_rounded),
            label: const Text(Fr.open),
          ),
        ),
      );
    }

    return Padding(
      padding: const EdgeInsets.only(top: Space.s),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, spacing: Space.s, children: children),
    );
  }

  /// Menu d'une partition ouverte : Ouvrir dans Fichiers, Corbeille, Analyse de l'espace.
  Widget _menu(BuildContext context, String volumeId) => PopupMenuButton<String>(
        tooltip: Fr.sourceMenu(_name),
        icon: const Icon(Icons.more_vert_rounded),
        enabled: !state.ejecting,
        onSelected: (v) => switch (v) {
          'files' => state.openInFiles(volumeId),
          'trash' => openTrash(context, state, volumeId),
          _ => openSpace(context, state, volumeId),
        },
        itemBuilder: (_) => const [
          PopupMenuItem(
            value: 'files',
            child: ListTile(leading: Icon(Icons.folder_shared_rounded), title: Text(Fr.openInFiles)),
          ),
          PopupMenuItem(
            value: 'trash',
            child: ListTile(leading: Icon(Icons.delete_outline_rounded), title: Text(Fr.trash)),
          ),
          PopupMenuItem(
            value: 'space',
            child: ListTile(leading: Icon(Icons.pie_chart_outline_rounded), title: Text(Fr.spaceAnalysis)),
          ),
        ],
      );
}
