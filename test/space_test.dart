import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:fluluks/src/screens/space_screen.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/strings_fr.dart';

import 'fake_host_api.dart';

EntryMsg f(String path, {int size = 1000}) =>
    EntryMsg(name: path.split('/').last, path: path, type: 'file', size: size, mtime: 1700000000);

SourceMsg src(String id) => SourceMsg(
    id: id, stableKey: id, kind: 'phone', label: 'Téléphone', totalBytes: 1000, freeBytes: 500, readOnly: false,
    roReason: '', encrypted: false);

SpaceItemMsg item(String path, int size, {bool dir = false, int files = 1}) =>
    SpaceItemMsg(name: path.split('/').last, path: path, isDir: dir, size: size, fileCount: files);

SpaceReportMsg report(String path, int total, List<SpaceItemMsg> children, List<EntryMsg> biggest,
        {int otherCount = 0, int otherBytes = 0, bool truncated = false}) =>
    SpaceReportMsg(
      path: path,
      totalBytes: total,
      fileCount: children.fold(0, (n, c) => n + c.fileCount),
      children: children,
      biggest: biggest,
      truncated: truncated,
      otherCount: otherCount,
      otherBytes: otherBytes,
    );

FakeHostApi api0() => FakeHostApi()
  ..sources = [src('phone')]
  ..dirs['phone'] = {
    '/': [f('/a.bin', size: 150)],
  }
  ..space['/'] = report('/', 100 * mo, [item('/D', 85 * mo, dir: true, files: 2), item('/a.bin', 15 * mo)],
      [f('/D/gros.bin', size: 70 * mo), f('/a.bin', size: 15 * mo), f('/D/E/petit.bin', size: 15 * mo)])
  ..space['/D'] = report('/D', 85 * mo, [item('/D/gros.bin', 70 * mo), item('/D/E', 15 * mo, dir: true)],
      [f('/D/gros.bin', size: 70 * mo), f('/D/E/petit.bin', size: 15 * mo)], otherCount: 3, otherBytes: 0);

const mo = 1000000;

Future<AppState> pumpHome(WidgetTester tester, FakeHostApi api) async {
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  await tester.pumpAndSettle();
  return state;
}

Future<void> push(WidgetTester tester, Widget screen) async {
  tester.state<NavigatorState>(find.byType(Navigator)).push(MaterialPageRoute<void>(builder: (_) => screen));
  await tester.pumpAndSettle();
}

List<double?> bars(WidgetTester tester) =>
    tester.widgetList<LinearProgressIndicator>(find.byType(LinearProgressIndicator)).map((b) => b.value).toList();

void main() {
  testWidgets('spaceScreenShowsBarsAndDrillsDown', (tester) async {
    final api = api0();
    final state = await pumpHome(tester, api);
    await push(tester, SpaceScreen(state: state, sourceId: 'phone'));
    expect(api.calls, contains('analyzeSpace phone /'));
    expect(find.text('100,0 Mo — 3 fichiers'), findsOneWidget);
    expect(find.text(Fr.percent(85)), findsOneWidget);
    expect(find.text(Fr.percent(15)), findsOneWidget);
    expect(bars(tester), [0.85, 0.15]);

    // descendre dans un dossier : lu dans l'analyse en mémoire
    await tester.tap(find.text('D'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('spaceChildren phone /D'));
    expect(find.text(Fr.percent(82)), findsOneWidget); // 70 / 85
    expect(find.text(Fr.percent(18)), findsOneWidget);
    expect(find.text('… et 3 autres éléments'), findsOneWidget);
    // un fichier ne s'ouvre pas
    await tester.tap(find.text('gros.bin'));
    await tester.pumpAndSettle();
    expect(api.calls.where((c) => c.startsWith('spaceChildren')).length, 1);

    // retour : le niveau précédent, sans quitter l'écran
    await tester.tap(find.byTooltip('Retour'));
    await tester.pumpAndSettle();
    expect(find.text(Fr.percent(85)), findsOneWidget);
    expect(find.byType(SpaceScreen), findsOneWidget);

    // les plus gros fichiers
    await tester.tap(find.text('Plus gros fichiers'));
    await tester.pumpAndSettle();
    expect(find.text('gros.bin'), findsOneWidget);
    expect(find.text('petit.bin'), findsOneWidget);
    expect(find.text('/D'), findsOneWidget); // dossier du fichier
  });

  testWidgets('analysisCancelButton', (tester) async {
    final api = api0();
    final pending = Completer<SpaceReportMsg>();
    api.spaceFuture = pending.future;
    final state = await pumpHome(tester, api);
    tester.state<NavigatorState>(find.byType(Navigator))
        .push(MaterialPageRoute<void>(builder: (_) => SpaceScreen(state: state, sourceId: 'phone')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 200)); // indicateur affiché après 200 ms (DelayedProgress)
    expect(find.text('Analyse en cours…'), findsOneWidget);
    await tester.tap(find.text('Annuler'));
    await tester.pump();
    expect(api.calls, contains('cancelAnalysis'));
    pending.complete(report('/', 100, [item('/a.bin', 100)], [], truncated: true));
    await tester.pumpAndSettle();
    expect(find.text(Fr.spaceTruncated), findsOneWidget);
    expect(find.text(Fr.percent(100)), findsOneWidget);
  });

  testWidgets('spaceOpenedFromExplorerRoot', (tester) async {
    final api = api0();
    final state = await pumpHome(tester, api);
    await push(tester, ExplorerScreen(state: state, sourceId: 'phone'));
    await tester.tap(find.byTooltip('Trier par'));
    await tester.pumpAndSettle();
    await tester.tap(find.text("Analyse de l'espace"));
    await tester.pumpAndSettle();
    expect(find.byType(SpaceScreen), findsOneWidget);
  });
}
