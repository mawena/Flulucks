import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:fluluks/src/screens/trash_screen.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/strings_fr.dart';

import 'fake_host_api.dart';

EntryMsg f(String path, {int size = 1000}) =>
    EntryMsg(name: path.split('/').last, path: path, type: 'file', size: size, mtime: 1700000000);
EntryMsg d(String path) => EntryMsg(name: path.split('/').last, path: path, type: 'dir', size: 0, mtime: 1700000000);

SourceMsg src(String id, {String kind = 'phone', String label = 'Téléphone', bool readOnly = false}) => SourceMsg(
      id: id,
      stableKey: id,
      kind: kind,
      label: label,
      totalBytes: 1000,
      freeBytes: 500,
      readOnly: readOnly,
      roReason: readOnly ? 'protégé en écriture' : '',
      encrypted: false,
    );

TrashItemMsg item(String id, String original, {int size = 1500, bool isDir = false}) => TrashItemMsg(
    id: id, name: original.split('/').last, originalPath: original, deletedAt: 1761181500000, size: size, isDir: isDir);

FakeHostApi api0() => FakeHostApi()
  ..sources = [src('phone'), src('v1', kind: 'disk', label: 'Données')]
  ..dirs['phone'] = {
    '/': [d('/DCIM'), f('/a.jpg'), f('/b.txt')],
    '/.Portail-corbeille/files': [f('/.Portail-corbeille/files/vieux.txt')],
  }
  ..dirs['v1'] = {'/': [f('/film.mkv')]}
  ..trash['phone'] = [item('a (1).txt', '/docs/a.txt'), item('D', '/D', size: 3000000, isDir: true)];

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

Future<void> askDelete(WidgetTester tester, String name) async {
  await tester.longPress(find.text(name));
  await tester.pump();
  await tester.tap(find.byTooltip('Supprimer'));
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('deleteDialogOffersPermanent', (tester) async {
    final api = api0();
    final state = await pumpHome(tester, api);
    await push(tester, ExplorerScreen(state: state, sourceId: 'phone'));
    // par défaut : à la corbeille
    await askDelete(tester, 'b.txt');
    expect(find.text('Les éléments restent 30 jours dans la corbeille de ce stockage.'), findsOneWidget);
    await tester.tap(find.widgetWithText(FilledButton, 'Supprimer'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('trash phone /b.txt'));
    // « Supprimer définitivement » reste possible
    await askDelete(tester, 'a.jpg');
    await tester.tap(find.widgetWithText(TextButton, 'Supprimer définitivement'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('delete phone /a.jpg'));

    // dans la corbeille : toujours définitif, sans choix
    await push(tester, ExplorerScreen(state: state, sourceId: 'phone', path: '/.Portail-corbeille/files'));
    await askDelete(tester, 'vieux.txt');
    expect(find.text('La suppression est définitive.'), findsOneWidget);
    expect(find.text('Supprimer définitivement'), findsNothing);
    await tester.tap(find.widgetWithText(FilledButton, 'Supprimer'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('delete phone /.Portail-corbeille/files/vieux.txt'));
  });

  testWidgets('trashScreenRestoreAndEmpty', (tester) async {
    final api = api0();
    final state = await pumpHome(tester, api);
    await push(tester, TrashScreen(state: state, sourceId: 'phone'));
    expect(find.text('Corbeille'), findsOneWidget);
    expect(find.text(Fr.trashSummary(2, 3000000)), findsOneWidget);
    expect(find.text('a.txt'), findsOneWidget); // nom d'origine, pas celui de la corbeille
    expect(find.textContaining('/docs/a.txt'), findsOneWidget);

    // restaurer un élément
    await tester.tap(find.byTooltip('Actions de a.txt'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Restaurer'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('restoreTrash phone a (1).txt'));
    expect(find.text('a.txt'), findsNothing);
    expect(find.text(Fr.trashSummary(1, 3000000)), findsOneWidget);

    // supprimer définitivement un élément, après confirmation
    api.trash['phone']!.add(item('c.txt', '/c.txt'));
    await tester.tap(find.byTooltip('Actions de D'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Supprimer définitivement'));
    await tester.pumpAndSettle();
    await tester.tap(find.widgetWithText(FilledButton, 'Supprimer'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('purgeTrash phone D'));

    // vider, après confirmation
    await tester.tap(find.byTooltip('Vider la corbeille'));
    await tester.pumpAndSettle();
    expect(find.text(Fr.emptyTrashTitle), findsOneWidget);
    await tester.tap(find.widgetWithText(FilledButton, 'Vider'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('emptyTrash phone'));
    expect(find.text('La corbeille est vide'), findsOneWidget);
    expect(tester.widget<IconButton>(find.widgetWithIcon(IconButton, Icons.delete_forever_rounded)).onPressed, isNull);
  });

  testWidgets('trashOpenedFromExplorerRootAndHome', (tester) async {
    final api = api0();
    final state = await pumpHome(tester, api);
    // menu de la carte du stockage sur l'accueil
    await tester.tap(find.byTooltip(Fr.sourceMenu('Téléphone')));
    await tester.pumpAndSettle();
    await tester.tap(find.text(Fr.trash));
    await tester.pumpAndSettle();
    expect(find.byType(TrashScreen), findsOneWidget);
    await tester.pageBack();
    await tester.pumpAndSettle();
    // menu de l'explorateur, à la racine seulement
    await push(tester, ExplorerScreen(state: state, sourceId: 'phone'));
    await tester.tap(find.byTooltip('Trier par'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Corbeille'));
    await tester.pumpAndSettle();
    expect(find.byType(TrashScreen), findsOneWidget);
    await tester.pageBack();
    await tester.pumpAndSettle();
    await tester.tap(find.text('DCIM'));
    await tester.pumpAndSettle();
    await tester.tap(find.byTooltip('Trier par'));
    await tester.pumpAndSettle();
    expect(find.text('Corbeille'), findsNothing);
  });
}
