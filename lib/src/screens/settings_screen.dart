import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';

/// Réglages : lecture seule des disques, accès aux fichiers, affichage en liste ou en grille,
/// fichiers cachés par défaut.
class SettingsScreen extends StatelessWidget {
  const SettingsScreen({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text(Fr.settings)),
      body: ListenableBuilder(
        listenable: state,
        builder: (context, _) {
          final v = state.viewPrefs;
          return ListView(children: [
            SwitchListTile(
              title: const Text(Fr.readOnlySwitch),
              subtitle: const Text(Fr.readOnlyHelp),
              value: state.readOnlyPreference,
              onChanged: state.setReadOnlyPreference,
            ),
            ListTile(
              title: const Text(Fr.allFilesTitle),
              trailing: state.allFilesAccess
                  ? const Text(Fr.allFilesGranted)
                  : FilledButton(onPressed: state.requestAllFilesAccess, child: const Text(Fr.allFilesButton)),
            ),
            ListTile(
              title: const Text(Fr.display),
              trailing: SegmentedButton<bool>(
                segments: const [
                  ButtonSegment(value: false, icon: Icon(Icons.view_list), label: Text(Fr.list)),
                  ButtonSegment(value: true, icon: Icon(Icons.grid_view), label: Text(Fr.grid)),
                ],
                selected: {v.grid},
                showSelectedIcon: false,
                onSelectionChanged: (s) => state.setViewPrefs(
                    ViewPrefsMsg(sort: v.sort, ascending: v.ascending, grid: s.first, showHidden: v.showHidden)),
              ),
            ),
            SwitchListTile(
              title: const Text(Fr.showHidden),
              value: v.showHidden,
              onChanged: (on) => state.setViewPrefs(
                  ViewPrefsMsg(sort: v.sort, ascending: v.ascending, grid: v.grid, showHidden: on)),
            ),
          ]);
        },
      ),
    );
  }
}
