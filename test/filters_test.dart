import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:fluluks/src/screens/filter_screen.dart';
import 'package:fluluks/src/screens/image_viewer_screen.dart';
import 'package:fluluks/src/screens/space_screen.dart';
import 'package:fluluks/src/screens/trash_screen.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/strings_fr.dart';
import 'package:fluluks/src/widgets/shortcut_grid.dart';

import 'explorer_screen_test.dart' show png;
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
    // ouvrir le dossier du fichier, par le menu de l'élément
    await tester.tap(find.byTooltip(Fr.actionsOf('rapport.pdf')));
    await tester.pumpAndSettle();
    await tester.tap(find.text(Fr.openFolder));
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

  // phase 11, T7 : liste ou grille selon le réglage, vignettes et lecteurs de Portail

  final gridPrefs = ViewPrefsMsg(sort: 'name', ascending: true, grid: true, showHidden: false, builtInViewers: true);

  Future<AppState> pushFilter(WidgetTester tester, FakeHostApi api, String filter) async {
    final state = await pumpHome(tester, api);
    tester.state<NavigatorState>(find.byType(Navigator)).push(MaterialPageRoute<void>(
        builder: (_) => FilterScreen(state: state, filter: filter, title: filter)));
    await tester.pumpAndSettle();
    return state;
  }

  testWidgets('filterFollowsGridPreference', (tester) async {
    final api = api0()
      ..viewPrefs = gridPrefs
      ..filters['photos'] = [item('phone', '/DCIM/a.jpg'), item('phone', '/DCIM/b.jpg')];
    await pushFilter(tester, api, 'photos');
    expect(find.byType(SliverGrid), findsOneWidget);
    expect(find.widgetWithText(ListTile, 'a.jpg'), findsNothing);
    expect(find.text('a.jpg'), findsOneWidget);
    expect(api.thumbRequests, containsAll(['/DCIM/a.jpg', '/DCIM/b.jpg']));
  });

  testWidgets('filterListWhenListPreference', (tester) async {
    final api = api0()..filters['photos'] = [item('phone', '/DCIM/a.jpg')];
    await pushFilter(tester, api, 'photos');
    expect(find.byType(SliverGrid), findsNothing);
    expect(find.widgetWithText(ListTile, 'a.jpg'), findsOneWidget);
    expect(find.text(Fr.filterLine('Téléphone', '/DCIM', 3000000)), findsOneWidget);
  });

  testWidgets('filterGridThumbsPerSource', (tester) async {
    final api = api0()
      ..viewPrefs = gridPrefs
      ..filters['photos'] = [item('phone', '/DCIM/a.jpg'), item('sd:1', '/DCIM/a.jpg')];
    await pushFilter(tester, api, 'photos');
    expect(api.thumbSources, containsAll(['phone|/DCIM/a.jpg', 'sd:1|/DCIM/a.jpg']));
  });

  testWidgets('viewToggleInFilterChangesPreference', (tester) async {
    final api = api0()..filters['photos'] = [item('phone', '/DCIM/a.jpg')];
    final state = await pushFilter(tester, api, 'photos');
    await tester.tap(find.byTooltip(Fr.showGrid));
    await tester.pumpAndSettle();
    expect(api.viewPrefs.grid, isTrue);
    expect(find.byType(SliverGrid), findsOneWidget);
    tester.state<NavigatorState>(find.byType(Navigator).first).push(
        MaterialPageRoute<void>(builder: (_) => ExplorerScreen(state: state, sourceId: 'phone', path: '/Download')));
    await tester.pumpAndSettle();
    expect(find.descendant(of: find.byType(ExplorerScreen), matching: find.byType(SliverGrid)), findsOneWidget);
  });

  testWidgets('filterOpensInPortailViewer', (tester) async {
    final api = api0()
      ..filters['photos'] = [item('phone', '/DCIM/a.jpg'), item('sd:1', '/DCIM/c.jpg'), item('phone', '/DCIM/b.jpg')]
      ..images['/DCIM/a.jpg'] = png
      ..images['/DCIM/b.jpg'] = png;
    await pushFilter(tester, api, 'photos');
    await tester.tap(find.text('b.jpg'));
    await tester.pumpAndSettle();
    final viewer = tester.widget<ImageViewerScreen>(find.byType(ImageViewerScreen));
    expect(viewer.sourceId, 'phone');
    expect([for (final e in viewer.images) e.path], ['/DCIM/a.jpg', '/DCIM/b.jpg']); // sa source, ordre affiché
    expect(viewer.initialIndex, 1);
    expect(api.calls.where((c) => c.startsWith('openWith')), isEmpty);
  });

  testWidgets('filterItemSheetOpenFolder', (tester) async {
    final api = api0()..viewPrefs = gridPrefs;
    await pushFilter(tester, api, 'documents');
    await tester.longPress(find.text('notes.txt')); // en grille : appui long
    await tester.pumpAndSettle();
    for (final a in [Fr.openFolder, Fr.openWith, Fr.share, Fr.properties]) {
      expect(find.widgetWithText(ListTile, a), findsOneWidget, reason: a);
    }
    await tester.tap(find.text(Fr.openFolder));
    await tester.pumpAndSettle();
    expect(find.byType(ExplorerScreen), findsOneWidget);
    expect(api.calls, contains('listDir sd:1 /Docs false'));
  });

  testWidgets('trashAndSpaceStayLists', (tester) async {
    // décision du plan : la corbeille (emplacement d'origine, date) et l'analyse (barres) restent en liste
    final api = api0()
      ..viewPrefs = gridPrefs
      ..trash['phone'] = [
        TrashItemMsg(id: 't', name: 'a.jpg', originalPath: '/DCIM/a.jpg', deletedAt: 0, size: 10, isDir: false),
      ];
    api.space['/'] = SpaceReportMsg(
        path: '/', totalBytes: 10, fileCount: 1, children: [], biggest: [], truncated: false, otherCount: 0, otherBytes: 0);
    final state = await pumpHome(tester, api);
    final ctx = tester.element(find.byType(Scaffold).first);
    openTrash(ctx, state, 'phone');
    await tester.pumpAndSettle();
    expect(find.byType(TrashScreen), findsOneWidget);
    expect(find.byType(SliverGrid), findsNothing);
    expect(find.byType(GridView), findsNothing);
    openSpace(ctx, state, 'phone');
    await tester.pumpAndSettle();
    expect(find.byType(SpaceScreen), findsOneWidget);
    expect(find.byType(SliverGrid), findsNothing);
    expect(find.byType(GridView), findsNothing);
  });
}
