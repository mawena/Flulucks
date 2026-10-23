import 'package:flutter/material.dart';

import '../state/app_state.dart';
import '../strings_fr.dart';
import '../widgets/disk_card.dart';
import '../widgets/source_tile.dart';
import '../widgets/transfer_bar.dart';
import 'explorer_screen.dart';
import 'filter_screen.dart';
import 'settings_screen.dart';
import 'trash_screen.dart';

/// Accueil : stockages du téléphone, disques USB et favoris.
class HomeScreen extends StatelessWidget {
  const HomeScreen({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text(Fr.title),
        actions: [
          IconButton(
            tooltip: Fr.settings,
            icon: const Icon(Icons.settings),
            onPressed: () => Navigator.of(context)
                .push(MaterialPageRoute<void>(builder: (_) => SettingsScreen(state: state))),
          ),
        ],
      ),
      bottomNavigationBar: TransferBar(state: state), // copie en cours ou interrompue, même hors de l'explorateur
      body: ListenableBuilder(
        listenable: state,
        builder: (context, _) {
          final theme = Theme.of(context);
          final err = state.lastError;
          final ejectErr = state.ejectError;
          final local = state.sources.where((s) => s.kind != 'disk').toList();
          return ListView(
            padding: const EdgeInsets.all(16),
            children: [
              if (err != null)
                MaterialBanner(
                  content: Text(err),
                  actions: [TextButton(onPressed: state.clearError, child: const Text(Fr.ok))],
                ),
              if (!state.allFilesAccess)
                Card(
                  color: theme.colorScheme.secondaryContainer,
                  margin: const EdgeInsets.only(bottom: 12),
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(crossAxisAlignment: CrossAxisAlignment.start, spacing: 8, children: [
                      Text(Fr.allFilesTitle, style: theme.textTheme.titleMedium),
                      const Text(Fr.allFilesBody),
                      FilledButton(onPressed: state.requestAllFilesAccess, child: const Text(Fr.allFilesButton)),
                    ]),
                  ),
                ),
              if (local.isNotEmpty && state.allFilesAccess) ...[
                _Section(Fr.shortcuts),
                Wrap(spacing: 8, runSpacing: 4, children: [
                  for (final (id, label, icon) in quickFilters)
                    ActionChip(
                      avatar: Icon(icon, size: 18),
                      label: Text(label),
                      onPressed: () => Navigator.of(context).push(MaterialPageRoute<void>(
                        builder: (_) => FilterScreen(state: state, filter: id, title: label),
                      )),
                    ),
                ]),
              ],
              if (local.isNotEmpty) ...[
                _Section(Fr.storage),
                for (final s in local)
                  SourceTile(
                    source: s,
                    onTap: state.allFilesAccess ? () => openExplorer(context, state, s.id) : null,
                    onTrash: state.allFilesAccess ? () => openTrash(context, state, s.id) : null,
                  ),
              ],
              _Section(Fr.usbDisks),
              if (state.ejecting)
                const Padding(
                  padding: EdgeInsets.symmetric(vertical: 8),
                  child: Row(children: [
                    SizedBox(width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2)),
                    SizedBox(width: 12),
                    Expanded(child: Text(Fr.ejecting)),
                  ]),
                ),
              if (ejectErr != null && !state.ejecting)
                Padding(
                  padding: const EdgeInsets.symmetric(vertical: 8),
                  child: Text(ejectErr, style: TextStyle(color: theme.colorScheme.error)),
                ),
              if (state.disks.isEmpty)
                const Padding(padding: EdgeInsets.symmetric(vertical: 16), child: Center(child: Text(Fr.emptyList)))
              else
                for (final d in state.disks) DiskCard(state: state, disk: d),
              if (state.favorites.isNotEmpty) ...[
                _Section(Fr.favorites),
                for (final f in state.favorites)
                  ListTile(
                    leading: const Icon(Icons.star),
                    title: Text(f.label),
                    subtitle: Text(f.sourceId == null ? Fr.favoriteUnavailable : f.path),
                    enabled: f.sourceId != null,
                    onTap: f.sourceId == null ? null : () => openExplorer(context, state, f.sourceId!, path: f.path),
                  ),
              ],
            ],
          );
        },
      ),
    );
  }
}

class _Section extends StatelessWidget {
  const _Section(this.title);

  final String title;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.only(top: 8, bottom: 8),
        child: Text(title, style: Theme.of(context).textTheme.titleSmall),
      );
}
