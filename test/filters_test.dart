import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:fluluks/src/screens/filter_screen.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/widgets/shortcut_grid.dart';

import 'fake_host_api.dart';

SourceMsg src(String id, String label) => SourceMsg(
    id: id, stableKey: id, kind: id == 'phone' ? 'phone' : 'removable', label: label, totalBytes: 1000, freeBytes: 500,
    readOnly: false, roReason: '', encrypted: false);

FilterItemMsg item(String sourceId, String path, {int size = 3000000}) =>
    FilterItemMsg(sourceId: sourceId, path: path, name: path.split('/').last, size: size, mtime: 1700000000);

FakeHostApi api0() => FakeHostApi()
  ..sources = [src('phone', 'Téléphone'), src('sd:1', 'Carte SD')]
  ..dirs['phone'] = {
    '/': <EntryMsg>[],
    '/Download': [EntryMsg(name: 'rapport.pdf', path: '/Download/rapport.pdf', type: 'file', size: 3000000, mtime: 1700000000)],
  }
  ..filters['documents'] = [item('phone', '/Download/rapport.pdf'), item('sd:1', '/Docs/notes.txt', size: 2000000)];

Future<AppState> pumpHome(WidgetTester tester, FakeHostApi api) async {
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  await tester.pumpAndSettle();
  return state;
}

void main() {
  testWidgets('shortcutsOpenFilter', (tester) async {
    tester.view.physicalSize = const Size(412, 869); // Pixel 4 XL : 3 colonnes
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final api = api0();
    await pumpHome(tester, api);
    final grid = find.byType(ShortcutGrid);
    for (final c in ['Photos', 'Vidéos', 'Musique', 'Documents', 'Archives', 'Récents']) {
      expect(find.descendant(of: grid, matching: find.text(c)), findsOneWidget, reason: c);
    }
    // 3 colonnes : Photos, Vidéos, Musique sur la première ligne, Documents sur la seconde
    expect(tester.getTopLeft(find.text('Documents')).dy, greaterThan(tester.getTopLeft(find.text('Photos')).dy));
    expect(tester.getTopLeft(find.text('Musique')).dy, tester.getTopLeft(find.text('Photos')).dy);
    await tester.tap(find.descendant(of: grid, matching: find.text('Documents')));
    await tester.pumpAndSettle();
    expect(find.byType(FilterScreen), findsOneWidget);
    expect(api.calls, contains('quickFilter documents'));
    expect(find.text('rapport.pdf'), findsOneWidget);
    expect(find.text('Téléphone · /Download · 3,0 Mo'), findsOneWidget);
    expect(find.text('Carte SD · /Docs · 2,0 Mo'), findsOneWidget);
  });

  testWidgets('noShortcutsWithoutFileAccess', (tester) async {
    final api = api0()..allFilesAccess = false;
    await pumpHome(tester, api);
    expect(find.byType(ShortcutGrid), findsNothing);
  });

  testWidgets('filterScreenOpensFile', (tester) async {
    final api = api0();
    final state = await pumpHome(tester, api);
    tester.state<NavigatorState>(find.byType(Navigator)).push(MaterialPageRoute<void>(
        builder: (_) => FilterScreen(state: state, filter: 'documents', title: 'Documents')));
    await tester.pumpAndSettle();
    await tester.tap(find.text('rapport.pdf'));
    await tester.pump();
    expect(api.calls, contains('openWith phone /Download/rapport.pdf'));
    // ouvrir le dossier du fichier
    await tester.tap(find.byTooltip('Ouvrir le dossier').first);
    await tester.pumpAndSettle();
    expect(find.byType(ExplorerScreen), findsOneWidget);
    expect(api.calls, contains('listDir phone /Download false'));
  });

  testWidgets('emptyFilterMessage', (tester) async {
    final api = api0();
    final state = await pumpHome(tester, api);
    tester.state<NavigatorState>(find.byType(Navigator)).push(MaterialPageRoute<void>(
        builder: (_) => FilterScreen(state: state, filter: 'music', title: 'Musique')));
    await tester.pumpAndSettle();
    expect(find.text('Aucun fichier'), findsOneWidget);
  });
}
