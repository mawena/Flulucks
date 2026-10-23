import 'package:flutter/material.dart';

import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/states.dart';
import '../ui/tokens.dart';
import '../ui/widgets.dart';
import '../widgets/brand_mark.dart';
import '../widgets/disk_card.dart';
import '../widgets/server_tile.dart';
import '../widgets/shortcut_grid.dart';
import '../widgets/source_tile.dart';
import '../widgets/transfer_bar.dart';
import 'explorer_screen.dart';
import 'filter_screen.dart';
import 'server_form_screen.dart';
import 'settings_screen.dart';
import 'space_screen.dart';
import 'trash_screen.dart';

/// Accueil : en-tête au logo, puis accès aux fichiers, Raccourcis, Stockage, Disques USB, Dossiers distants et
/// Favoris. En largeur étendue, deux colonnes : à gauche ce qui est sur le téléphone, à droite les disques USB et
/// les dossiers distants.
class HomeScreen extends StatelessWidget {
  const HomeScreen({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Scaffold(
      appBar: AppBar(
        titleSpacing: Space.l,
        title: Row(children: [
          const BrandMark(),
          const SizedBox(width: Space.m),
          Flexible(child: Text(Fr.title, style: theme.textTheme.headlineSmall)),
        ]),
        actions: [
          IconButton(
            tooltip: Fr.settings,
            icon: const Icon(Icons.settings_rounded),
            onPressed: () => Navigator.of(context)
                .push(MaterialPageRoute<void>(builder: (_) => SettingsScreen(state: state))),
          ),
          PopupMenuButton<void Function()>(
            tooltip: Fr.moreOptions,
            icon: const Icon(Icons.more_vert_rounded),
            onSelected: (open) => open(),
            itemBuilder: (context) => [
              PopupMenuItem(value: () => openHelp(context), child: const Text(Fr.help)),
              PopupMenuItem(value: () => openAbout(context, state), child: const Text(Fr.about)),
            ],
          ),
        ],
      ),
      bottomNavigationBar: TransferBar(state: state), // copie en cours ou interrompue, même hors de l'explorateur
      body: ListenableBuilder(
        listenable: state,
        builder: (context, _) {
          final top = _top(context);
          final phone = [..._shortcuts(context), ..._storage(context)];
          final away = [..._disks(context), ..._remote(context)];
          final favorites = _favorites(context);
          if (windowClassOf(context) == WindowClass.expanded) {
            return ListView(padding: const EdgeInsets.all(Space.l), children: [
              ...top,
              Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
                Expanded(child: _column([...phone, ...favorites])),
                const SizedBox(width: Space.xl),
                Expanded(child: _column(away)),
              ]),
            ]);
          }
          return ListView(
            padding: const EdgeInsets.all(Space.l),
            children: [...top, ...phone, ...away, ...favorites],
          );
        },
      ),
    );
  }

  Widget _column(List<Widget> children) =>
      Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: children);

  /// Les cartes, séparées comme les autres éléments de la liste.
  Widget _gap(Widget child) => Padding(padding: const EdgeInsets.only(bottom: Space.s), child: child);

  /// Erreur d'action et carte d'accès aux fichiers, au-dessus des sections.
  List<Widget> _top(BuildContext context) {
    final err = state.lastError;
    return [
      if (err != null)
        _gap(StatusBanner(
          icon: Icons.error_rounded,
          text: err,
          tone: ChipTone.danger,
          actions: [TextButton(onPressed: state.clearError, child: const Text(Fr.ok))],
        )),
      if (!state.allFilesAccess)
        _gap(StatusBanner(
          icon: Icons.folder_rounded,
          title: Fr.allFilesTitle,
          text: Fr.allFilesBody,
          tone: ChipTone.neutral,
          actions: [FilledButton(onPressed: state.requestAllFilesAccess, child: const Text(Fr.allFilesButton))],
        )),
    ];
  }

  List<Widget> _shortcuts(BuildContext context) {
    if (!_hasLocal || !state.allFilesAccess) return const [];
    return [
      const SectionHeader(Fr.shortcuts),
      ShortcutGrid(
        onOpen: (id, label) => Navigator.of(context).push(MaterialPageRoute<void>(
          builder: (_) => FilterScreen(state: state, filter: id, title: label),
        )),
      ),
    ];
  }

  /// Stockages du téléphone : mémoire interne, cartes SD (les disques USB et dossiers distants ont leur section).
  bool _isLocal(String kind) => kind != 'disk' && kind != 'remote';

  bool get _hasLocal => state.sources.any((s) => _isLocal(s.kind));

  List<Widget> _storage(BuildContext context) {
    final access = state.allFilesAccess;
    return [
      if (_hasLocal) const SectionHeader(Fr.storage),
      for (final s in state.sources.where((s) => _isLocal(s.kind)))
        _gap(SourceTile(
          source: s,
          onTap: access ? () => openExplorer(context, state, s.id) : null,
          onTrash: access ? () => openTrash(context, state, s.id) : null,
          onSpace: access ? () => openSpace(context, state, s.id) : null,
        )),
    ];
  }

  List<Widget> _disks(BuildContext context) {
    final theme = Theme.of(context);
    final ejectErr = state.ejectError;
    return [
      const SectionHeader(Fr.usbDisks),
      if (state.ejecting)
        const Padding(
          padding: EdgeInsets.only(bottom: Space.m),
          child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, spacing: Space.s, children: [
            LinearProgressIndicator(),
            Text(Fr.ejecting),
          ]),
        ),
      if (ejectErr != null && !state.ejecting)
        _gap(StatusBanner(icon: Icons.error_rounded, text: ejectErr, tone: ChipTone.danger)),
      if (state.disks.isEmpty)
        Card(
          color: theme.colorScheme.surfaceContainerLow,
          child: const EmptyState(icon: Icons.usb_rounded, title: Fr.noDiskTitle, message: Fr.noDiskBody),
        )
      else
        for (final d in state.disks) _gap(DiskCard(state: state, disk: d)),
    ];
  }

  List<Widget> _remote(BuildContext context) => [
        const SectionHeader(Fr.remoteFolders),
        for (final s in state.servers) _gap(ServerTile(state: state, server: s)),
        Align(
          alignment: AlignmentDirectional.centerStart,
          child: TextButton.icon(
            icon: const Icon(Icons.add_rounded),
            label: const Text(Fr.addServer),
            onPressed: () => Navigator.of(context)
                .push(MaterialPageRoute<void>(builder: (_) => ServerFormScreen(state: state))),
          ),
        ),
      ];

  List<Widget> _favorites(BuildContext context) {
    if (state.favorites.isEmpty) return const [];
    return [
      const SectionHeader(Fr.favorites),
      for (final f in state.favorites)
        _gap(Card(
          clipBehavior: Clip.antiAlias,
          child: ListTile(
            leading: const TonalIcon(Icons.star_rounded),
            title: Text(f.label),
            subtitle: Text(f.sourceId == null ? Fr.favoriteUnavailable : f.path),
            enabled: f.sourceId != null,
            onTap: f.sourceId == null ? null : () => openExplorer(context, state, f.sourceId!, path: f.path),
          ),
        )),
    ];
  }
}
