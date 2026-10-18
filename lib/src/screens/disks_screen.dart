import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../widgets/partition_tile.dart';

class DisksScreen extends StatelessWidget {
  const DisksScreen({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text(Fr.title)),
      body: ListenableBuilder(
        listenable: state,
        builder: (context, _) {
          final err = state.lastError;
          final ejectErr = state.ejectError;
          final theme = Theme.of(context);
          return Column(children: [
            if (err != null)
              MaterialBanner(
                content: Text(err),
                actions: [TextButton(onPressed: state.clearError, child: const Text('OK'))],
              ),
            SwitchListTile(
              title: const Text(Fr.readOnlySwitch),
              value: state.readOnlyPreference,
              onChanged: state.setReadOnlyPreference,
            ),
            if (state.ejecting)
              const Padding(
                padding: EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                child: Row(children: [
                  SizedBox(width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2)),
                  SizedBox(width: 12),
                  Expanded(child: Text(Fr.ejecting)),
                ]),
              ),
            if (ejectErr != null && !state.ejecting)
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                child: Text(ejectErr, style: TextStyle(color: theme.colorScheme.error)),
              ),
            Expanded(
              child: state.disks.isEmpty
                  ? const Center(child: Text(Fr.emptyList))
                  : ListView(
                      padding: const EdgeInsets.all(16),
                      children: [for (final d in state.disks) _DiskCard(state: state, disk: d)],
                    ),
            ),
          ]);
        },
      ),
    );
  }
}

class _DiskCard extends StatelessWidget {
  const _DiskCard({required this.state, required this.disk});

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
