import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/strings_fr.dart';
import 'package:fluluks/src/ui/widgets.dart';

import 'fake_host_api.dart';

Future<FakeHostApi> pumpSettings(WidgetTester tester, FakeHostApi api) async {
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  await tester.tap(find.byTooltip('Réglages'));
  await tester.pumpAndSettle();
  return api;
}

Set<bool> selected(WidgetTester tester) =>
    tester.widget<SegmentedButton<bool>>(find.byType(SegmentedButton<bool>)).selected;

void main() {
  testWidgets('settingsGroupedSections', (tester) async {
    tester.view.physicalSize = const Size(412, 2400); // toute la liste construite
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await pumpSettings(tester, FakeHostApi());
    final headers = tester.widgetList<SectionHeader>(find.byType(SectionHeader)).map((h) => h.title).toList();
    expect(headers, [Fr.sectionAppearance, Fr.sectionFiles, Fr.sectionDisks, Fr.sectionComfort, Fr.title]);
    expect(headers, ['Apparence', 'Fichiers', 'Disques', 'Confort', 'Portail']);
    // chaque réglage sous son en-tête, dans l'ordre
    const order = [
      Fr.sectionAppearance, Fr.theme, Fr.display, Fr.showHidden, //
      Fr.sectionFiles, Fr.builtInViewers, Fr.allFilesTitle, //
      Fr.sectionDisks, Fr.readOnlySwitch, Fr.autoLock, //
      Fr.sectionComfort, Fr.haptics, //
      Fr.title, Fr.help, Fr.about,
    ];
    final ys = [for (final t in order) tester.getTopLeft(find.text(t)).dy];
    expect(ys, [...ys]..sort(), reason: '$ys');
  });

  testWidgets('hapticsSwitchCallsApi', (tester) async {
    final api = FakeHostApi()..uiPrefs = UiPrefsMsg(theme: 'dark', haptics: true, welcomeSeen: true);
    await pumpSettings(tester, api);
    await tester.scrollUntilVisible(find.text(Fr.haptics), 100);
    await tester.ensureVisible(find.text(Fr.haptics));
    await tester.pumpAndSettle();
    await tester.tap(find.text(Fr.haptics));
    await tester.pump();
    expect([api.uiPrefs.haptics, api.uiPrefs.theme, api.uiPrefs.welcomeSeen], [false, 'dark', true]);
    expect(tester.widget<SwitchListTile>(find.widgetWithText(SwitchListTile, Fr.haptics)).value, isFalse);
  });

  testWidgets('displayChoiceShowsCurrentMode', (tester) async {
    tester.view.physicalSize = const Size(412, 869); // largeur du Pixel 4 XL : pas de débordement
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final api = FakeHostApi()..viewPrefs = ViewPrefsMsg(sort: 'size', ascending: false, grid: true, showHidden: true, builtInViewers: true);
    await pumpSettings(tester, api);
    expect(find.text('Affichage des dossiers'), findsOneWidget);
    expect(selected(tester), {true});
  });

  testWidgets('displayChoiceCallsApi', (tester) async {
    final api = FakeHostApi()..viewPrefs = ViewPrefsMsg(sort: 'size', ascending: false, grid: true, showHidden: true, builtInViewers: true);
    await pumpSettings(tester, api);
    await tester.tap(find.text('Liste'));
    await tester.pump();
    final v = api.viewPrefs;
    expect([v.grid, v.sort, v.ascending, v.showHidden], [false, 'size', false, true]);
    expect(selected(tester), {false});
  });

  testWidgets('builtInViewersSwitch', (tester) async {
    final api = FakeHostApi()..viewPrefs = ViewPrefsMsg(sort: 'size', ascending: false, grid: true, showHidden: true, builtInViewers: true);
    await pumpSettings(tester, api);
    await tester.scrollUntilVisible(find.text('Ouvrir dans Portail'), 100);
    await tester.tap(find.text('Ouvrir dans Portail'));
    await tester.pump();
    final v = api.viewPrefs;
    expect([v.builtInViewers, v.grid, v.sort, v.ascending, v.showHidden], [false, true, 'size', false, true]);
  });

  testWidgets('explorerOpensInChosenMode', (tester) async {
    final api = FakeHostApi()
      ..sources = [
        SourceMsg(
            id: 'phone',
            stableKey: 'phone',
            kind: 'phone',
            label: 'Téléphone',
            totalBytes: 1000,
            freeBytes: 500,
            readOnly: false,
            roReason: '',
            encrypted: false),
      ]
      ..dirs['phone'] = {
        '/': [EntryMsg(name: 'DCIM', path: '/DCIM', type: 'dir', size: 0, mtime: 1700000000)],
      };
    await pumpSettings(tester, api);
    await tester.tap(find.text('Grille'));
    await tester.pump();
    await tester.tap(find.byTooltip('Retour'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Téléphone'));
    await tester.pumpAndSettle();
    expect(find.byType(SliverGrid), findsOneWidget);
  });

  testWidgets('autoLockShowsCurrentValue', (tester) async {
    tester.view.physicalSize = const Size(412, 869); // largeur du Pixel 4 XL : pas de débordement
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await pumpSettings(tester, FakeHostApi()..autoLockMinutes = 15);
    await tester.scrollUntilVisible(find.text('15 min'), 100); // section Disques, sous Apparence et Fichiers
    expect(find.text('Verrouillage automatique'), findsOneWidget);
    expect(find.text('Disques chiffrés, après ce délai sans activité'), findsOneWidget);
    expect(find.text('15 min'), findsOneWidget);
  });

  testWidgets('autoLockCallsApi', (tester) async {
    final api = await pumpSettings(tester, FakeHostApi());
    await tester.scrollUntilVisible(find.text('Jamais'), 100);
    await tester.ensureVisible(find.text('Jamais'));
    await tester.pumpAndSettle();
    expect(find.text('Jamais'), findsOneWidget); // désactivé par défaut
    await tester.tap(find.text('Jamais'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('1 h').last);
    await tester.pumpAndSettle();
    expect(api.autoLockMinutes, 60);
    expect(find.text('1 h'), findsOneWidget);
  });
}
