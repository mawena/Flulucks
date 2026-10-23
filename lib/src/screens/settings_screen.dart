import 'package:flutter/material.dart';

import '../state/app_state.dart';
import '../state/prefs.dart';
import '../strings_fr.dart';
import '../ui/tokens.dart';
import '../ui/widgets.dart';
import 'about_screen.dart';
import 'help_screen.dart';

/// Délais proposés (minutes ; 0 = jamais), les mêmes que AutoLock.CHOICES côté Kotlin.
const autoLockChoices = [0, 5, 15, 30, 60];

/// Ouvre l'aide.
void openHelp(BuildContext context) =>
    Navigator.of(context).push(MaterialPageRoute<void>(builder: (_) => const HelpScreen()));

/// Ouvre « À propos ».
void openAbout(BuildContext context, AppState state) =>
    Navigator.of(context).push(MaterialPageRoute<void>(builder: (_) => AboutScreen(state: state)));

/// Réglages, en sections : Apparence (thème, liste ou grille, fichiers cachés), Fichiers (lecteurs de Portail,
/// accès aux fichiers du téléphone), Disques (lecture seule, verrouillage automatique), Confort (retours
/// haptiques) et Portail (aide, à propos).
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
            const SectionHeader(Fr.sectionAppearance),
            ListTile(
              title: const Text(Fr.theme),
              // sous le titre : trois segments ne tiennent pas à droite sur 412 dp
              subtitle: Padding(
                padding: const EdgeInsets.only(top: Space.s),
                child: SegmentedButton<String>(
                  segments: const [
                    ButtonSegment(value: 'system', icon: Icon(Icons.brightness_auto_rounded), label: Text(Fr.themeSystem)),
                    ButtonSegment(value: 'light', icon: Icon(Icons.light_mode_rounded), label: Text(Fr.themeLight)),
                    ButtonSegment(value: 'dark', icon: Icon(Icons.dark_mode_rounded), label: Text(Fr.themeDark)),
                  ],
                  selected: {state.uiPrefs.theme},
                  showSelectedIcon: false,
                  onSelectionChanged: (s) => state.setUiPrefs(state.uiPrefs.copyWith(theme: s.first)),
                ),
              ),
            ),
            ListTile(
              title: const Text(Fr.display),
              // sous le titre, comme le thème : à droite, les segments débordent quand le texte est agrandi
              subtitle: Padding(
                padding: const EdgeInsets.only(top: Space.s),
                child: SegmentedButton<bool>(
                  segments: const [
                    ButtonSegment(value: false, icon: Icon(Icons.view_list_rounded), label: Text(Fr.list)),
                    ButtonSegment(value: true, icon: Icon(Icons.grid_view_rounded), label: Text(Fr.grid)),
                  ],
                  selected: {v.grid},
                  showSelectedIcon: false,
                  onSelectionChanged: (s) => state.setViewPrefs(v.copyWith(grid: s.first)),
                ),
              ),
            ),
            SwitchListTile(
              title: const Text(Fr.showHidden),
              value: v.showHidden,
              onChanged: (on) => state.setViewPrefs(v.copyWith(showHidden: on)),
            ),
            const SectionHeader(Fr.sectionFiles),
            SwitchListTile(
              title: const Text(Fr.builtInViewers),
              subtitle: const Text(Fr.builtInViewersHelp),
              value: v.builtInViewers,
              onChanged: (on) => state.setViewPrefs(v.copyWith(builtInViewers: on)),
            ),
            ListTile(
              title: const Text(Fr.allFilesTitle),
              trailing: state.allFilesAccess
                  ? const Text(Fr.allFilesGranted)
                  : FilledButton(onPressed: state.requestAllFilesAccess, child: const Text(Fr.allFilesButton)),
            ),
            const SectionHeader(Fr.sectionDisks),
            SwitchListTile(
              title: const Text(Fr.readOnlySwitch),
              subtitle: const Text(Fr.readOnlyHelp),
              value: state.readOnlyPreference,
              onChanged: state.setReadOnlyPreference,
            ),
            ListTile(
              title: const Text(Fr.autoLock),
              subtitle: const Text(Fr.autoLockHelp),
              trailing: DropdownButton<int>(
                value: autoLockChoices.contains(state.autoLockMinutes) ? state.autoLockMinutes : 0,
                items: [
                  for (final m in autoLockChoices)
                    DropdownMenuItem(value: m, child: Text(m == 0 ? Fr.never : Fr.minutes(m))),
                ],
                onChanged: (m) {
                  if (m != null) state.setAutoLockMinutes(m);
                },
              ),
            ),
            const SectionHeader(Fr.sectionComfort),
            SwitchListTile(
              title: const Text(Fr.haptics),
              subtitle: const Text(Fr.hapticsHelp),
              value: state.uiPrefs.haptics,
              onChanged: (on) => state.setUiPrefs(state.uiPrefs.copyWith(haptics: on)),
            ),
            const SectionHeader(Fr.title),
            ListTile(
              leading: const Icon(Icons.help_rounded),
              title: const Text(Fr.help),
              subtitle: const Text(Fr.helpSubtitle),
              onTap: () => openHelp(context),
            ),
            ListTile(
              leading: const Icon(Icons.info_rounded),
              title: const Text(Fr.about),
              subtitle: const Text(Fr.aboutSubtitle),
              onTap: () => openAbout(context, state),
            ),
            const SizedBox(height: Space.l),
          ]);
        },
      ),
    );
  }
}
