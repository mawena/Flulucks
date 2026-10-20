import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';

/// Réglages : lecture seule des disques, accès aux fichiers, fichiers cachés par défaut.
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
