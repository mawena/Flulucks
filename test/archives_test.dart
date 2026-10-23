import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
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

FakeHostApi api0({bool diskReadOnly = false}) => FakeHostApi()
  ..sources = [src('phone'), src('v1', kind: 'disk', label: 'Données', readOnly: diskReadOnly)]
  ..dirs['phone'] = {
    '/': [d('/DCIM'), f('/b.txt'), f('/notes.txt'), f('/photos.tar.gz')],
    '/DCIM': [f('/DCIM/x.jpg')],
  }
  ..dirs['v1'] = {'/': [f('/film.mkv')]};

Future<AppState> pump(WidgetTester tester, FakeHostApi api, {String source = 'phone'}) async {
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  tester.state<NavigatorState>(find.byType(Navigator)).push(
      MaterialPageRoute<void>(builder: (_) => ExplorerScreen(state: state, sourceId: source)));
  await tester.pumpAndSettle();
  return state;
}

Future<void> openExplorerOn(WidgetTester tester, AppState state, String source) async {
  tester.state<NavigatorState>(find.byType(Navigator)).push(
      MaterialPageRoute<void>(builder: (_) => ExplorerScreen(state: state, sourceId: source)));
  await tester.pumpAndSettle();
}

/// Action du menu « Plus d'actions » de la barre de sélection.
Future<void> tapMenu(WidgetTester tester, String action) async {
  await tester.tap(find.byTooltip("Plus d'actions"));
  await tester.pumpAndSettle();
  await tester.tap(find.text(action));
  await tester.pumpAndSettle();
}

void main() {
  test('archiveNames', () {
    for (final n in ['a.zip', 'A.ZIP', 'a.7z', 'a.rar', 'a.tar', 'a.tar.gz', 'a.tgz', 'a.tar.xz', 'a.txz', 'a.gz', 'a.xz']) {
      expect(isArchiveName(n), isTrue, reason: n);
    }
    for (final n in ['a.txt', '.zip', 'zip', 'a.zip.txt', 'a.bz2']) {
      expect(isArchiveName(n), isFalse, reason: n);
    }
  });

  testWidgets('compressAskNameThenStarts', (tester) async {
    final api = api0();
    await pump(tester, api);
    await tester.longPress(find.text('b.txt'));
    await tester.pump();
    await tapMenu(tester, 'Compresser');
    // un seul fichier : son nom sans l'extension est proposé
    expect(find.widgetWithText(TextField, 'b'), findsOneWidget);
    await tester.enterText(find.byType(TextField), 'sauvegarde');
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
    final (s, paths, dir, name) = api.compresses.single;
    expect([s, ...paths, dir, name], ['phone', '/b.txt', '/', 'sauvegarde.zip']);
    expect(find.text('1 sélectionné(s)'), findsNothing); // sélection quittée

    // plusieurs éléments : « Archive » proposé ; une extension .zip déjà tapée est gardée
    await tester.longPress(find.text('DCIM'));
    await tester.pump();
    await tester.tap(find.text('notes.txt'));
    await tester.pump();
    await tapMenu(tester, 'Compresser');
    expect(find.widgetWithText(TextField, 'Archive'), findsOneWidget);
    await tester.enterText(find.byType(TextField), 'lot.ZIP');
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
    final (s2, paths2, dir2, name2) = api.compresses.last;
    expect([s2, ...paths2, dir2, name2], ['phone', '/DCIM', '/notes.txt', '/', 'lot.ZIP']);

    // Annuler ou nom vide : rien n'est lancé
    await tester.longPress(find.text('b.txt'));
    await tester.pump();
    await tapMenu(tester, 'Compresser');
    await tester.enterText(find.byType(TextField), '  ');
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
    expect(api.compresses.length, 2);
  });

  testWidgets('compressDisabledOnReadOnly', (tester) async {
    final api = api0(diskReadOnly: true)..dirs['v1'] = {'/': [f('/film.mkv')]};
    await pump(tester, api, source: 'v1');
    await tester.longPress(find.text('film.mkv'));
    await tester.pump();
    await tester.tap(find.byTooltip("Plus d'actions"));
    await tester.pumpAndSettle();
    expect(tester.widget<PopupMenuItem<String>>(find.widgetWithText(PopupMenuItem<String>, 'Compresser')).enabled, isFalse);
  });

  testWidgets('tapArchiveShowsExtractSheet', (tester) async {
    final api = api0();
    await pump(tester, api);
    await tester.tap(find.text('photos.tar.gz'));
    await tester.pumpAndSettle();
    expect(find.text('Extraire ici'), findsOneWidget);
    expect(find.text('Extraire vers…'), findsOneWidget);
    expect(find.text('Ouvrir avec…'), findsOneWidget);
    expect(api.calls, isNot(contains('openWith phone /photos.tar.gz')));
    await tester.tap(find.text('Extraire ici'));
    await tester.pumpAndSettle();
    final (s, archive, dst, dir, newFolder) = api.extracts.single;
    expect([s, archive, dst, dir, newFolder], ['phone', '/photos.tar.gz', 'phone', '/', true]);
    expect(find.text('Extraire vers…'), findsNothing); // feuille fermée

    // « Ouvrir avec… » reste possible
    await tester.tap(find.text('photos.tar.gz'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Ouvrir avec…'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('openWith phone /photos.tar.gz'));

    // un fichier ordinaire s'ouvre directement
    await tester.tap(find.text('notes.txt'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('openWith phone /notes.txt'));
    expect(find.text('Extraire ici'), findsNothing);
  });

  testWidgets('extractToPicksDestinationWithClipboardBar', (tester) async {
    final api = api0();
    final state = await pump(tester, api);
    await tester.tap(find.text('photos.tar.gz'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Extraire vers…'));
    await tester.pumpAndSettle();
    expect(find.text('« photos.tar.gz » à extraire'), findsOneWidget);
    // la barre suit dans un autre stockage, où « Extraire ici » lance l'extraction
    await openExplorerOn(tester, state, 'v1');
    expect(find.text('« photos.tar.gz » à extraire'), findsOneWidget);
    await tester.tap(find.widgetWithText(FilledButton, 'Extraire ici'));
    await tester.pump();
    final (s, archive, dst, dir, newFolder) = api.extracts.single;
    expect([s, archive, dst, dir, newFolder], ['phone', '/photos.tar.gz', 'v1', '/', true]);
    expect(state.pendingExtract, isNull);
    expect(find.text('« photos.tar.gz » à extraire'), findsNothing);

    // Annuler vide la barre ; copier remplace l'extraction en attente
    state.setPendingExtract('phone', '/photos.tar.gz');
    await tester.pump();
    await tester.tap(find.widgetWithText(TextButton, 'Annuler'));
    await tester.pump();
    expect(state.pendingExtract, isNull);
    state.setPendingExtract('phone', '/photos.tar.gz');
    state.setClipboard('phone', ['/b.txt'], move: false);
    await tester.pump();
    expect(state.pendingExtract, isNull);
    expect(find.text('1 élément(s) à copier'), findsOneWidget);
    state.setPendingExtract('phone', '/photos.tar.gz');
    expect(state.clipboard, isNull);
  });

  testWidgets('extractHereDisabledOnReadOnlyDestination', (tester) async {
    final api = api0(diskReadOnly: true);
    final state = await pump(tester, api);
    state.setPendingExtract('phone', '/photos.tar.gz');
    await openExplorerOn(tester, state, 'v1');
    expect(tester.widget<FilledButton>(find.widgetWithText(FilledButton, 'Extraire ici')).onPressed, isNull);
  });

  testWidgets('transferBarShowsKind', (tester) async {
    final api = api0();
    final state = await pump(tester, api);
    TransferMsg t(String kind) => TransferMsg(
        kind: kind, move: kind == 'move', currentName: 'b.txt', fileIndex: 1, fileCount: 2, doneBytes: 50,
        totalBytes: 100, finished: false, cancelled: false, elapsedMs: 0);
    state.onTransferChanged(t('compress'));
    await tester.pump();
    expect(find.text('Compression : b.txt (1/2) — 50 %'), findsOneWidget);
    state.onTransferChanged(t('extract'));
    await tester.pump();
    expect(find.text('Extraction : b.txt (1/2) — 50 %'), findsOneWidget);
    state.onTransferChanged(t('move'));
    await tester.pump();
    expect(find.text('Déplacement : b.txt (1/2) — 50 %'), findsOneWidget);
    state.onTransferChanged(t('copy'));
    await tester.pump();
    expect(find.text('Copie : b.txt (1/2) — 50 %'), findsOneWidget);
  });

  test('extractFirstPassAnnounced', () {
    // premier passage (en-têtes seulement) : aucun fichier compté encore
    expect(Fr.transferLine('extract', 'b.tar.xz', 0, 0, 0), "Lecture de l'archive b.tar.xz…");
    expect(Fr.transferLine('extract', 'a.txt', 1, 2, 50), 'Extraction : a.txt (1/2) — 50 %');
    expect(Fr.transferLine('copy', 'a.txt', 0, 0, 0), 'Copie : a.txt (0/0) — 0 %');
  });
}
