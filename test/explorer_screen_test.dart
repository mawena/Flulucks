import 'dart:typed_data';

import 'package:flutter/foundation.dart' show SynchronousFuture;
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:fluluks/src/state/app_state.dart';

import 'fake_host_api.dart';

EntryMsg f(String path, {int size = 1000, int mtime = 1700000000}) =>
    EntryMsg(name: path.split('/').last, path: path, type: 'file', size: size, mtime: mtime);
EntryMsg d(String path) => EntryMsg(name: path.split('/').last, path: path, type: 'dir', size: 0, mtime: 1700000000);

SourceMsg src(String id,
        {String kind = 'phone', String label = 'Téléphone', bool readOnly = false, String roReason = '', String? key, bool encrypted = false}) =>
    SourceMsg(
      id: id,
      stableKey: key ?? id,
      kind: kind,
      label: label,
      totalBytes: 1000,
      freeBytes: 500,
      readOnly: readOnly,
      roReason: roReason,
      encrypted: encrypted,
    );

/// Une PNG 1 × 1 valide (vignette factice).
final png = Uint8List.fromList([
  0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, //
  0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49,
  0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99, 0x3D,
  0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
]);

FakeHostApi api0() => FakeHostApi()
  ..sources = [src('phone'), src('v1', kind: 'disk', label: 'Données')]
  ..dirs['phone'] = {
    '/': [f('/b.txt'), d('/DCIM'), f('/a.jpg', size: 3000000), f('/.cache')],
    '/DCIM': [f('/DCIM/photo.jpg')],
  }
  ..dirs['v1'] = {'/': [f('/film.mkv')]};

Future<AppState> pump(WidgetTester tester, FakeHostApi api, {String source = 'phone', String path = '/'}) async {
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  final nav = tester.state<NavigatorState>(find.byType(Navigator));
  nav.push(MaterialPageRoute<void>(builder: (_) => ExplorerScreen(state: state, sourceId: source, path: path)));
  await tester.pumpAndSettle();
  return state;
}

List<String> titles(WidgetTester tester) =>
    tester.widgetList<ListTile>(find.byType(ListTile)).map((t) => (t.title as Text).data!).toList();

Future<void> longPress(WidgetTester tester, String name) async {
  await tester.longPress(find.text(name));
  await tester.pump();
}

void main() {
  testWidgets('listsAndNavigates', (tester) async {
    final api = api0();
    await pump(tester, api);
    expect(find.text('Téléphone'), findsOneWidget); // titre à la racine
    expect(titles(tester), ['DCIM', 'a.jpg', 'b.txt']); // dossiers d'abord, cachés masqués
    expect(find.textContaining('3,0 Mo · '), findsOneWidget); // taille · date
    await tester.tap(find.text('DCIM'));
    await tester.pumpAndSettle();
    expect(find.text('DCIM'), findsOneWidget); // titre = nom du dossier
    expect(titles(tester), ['photo.jpg']);
    await tester.pageBack(); // remonte d'un dossier
    await tester.pumpAndSettle();
    expect(titles(tester), ['DCIM', 'a.jpg', 'b.txt']);
    await tester.pageBack(); // puis l'accueil
    await tester.pumpAndSettle();
    expect(find.byType(ExplorerScreen), findsNothing);
  });

  testWidgets('gridShowsThumbnails', (tester) async {
    final api = api0()
      ..viewPrefs = ViewPrefsMsg(sort: 'name', ascending: true, grid: true, showHidden: false)
      ..thumbs['/a.jpg'] = png;
    await pump(tester, api);
    expect(find.byType(GridView), findsOneWidget);
    expect(api.thumbRequests, ['/a.jpg']); // ni dossier ni texte
    expect(find.byType(Image), findsOneWidget);
    expect(find.byIcon(Icons.folder), findsOneWidget);
  });

  testWidgets('sortMenuPersists', (tester) async {
    final api = api0();
    await pump(tester, api);
    await tester.tap(find.byTooltip('Trier par'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Taille'));
    await tester.pumpAndSettle();
    expect(api.viewPrefs.sort, 'size');
    await tester.tap(find.byTooltip('Trier par'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Décroissant'));
    await tester.pumpAndSettle();
    expect(api.viewPrefs.ascending, isFalse);
    expect(titles(tester), ['DCIM', 'a.jpg', 'b.txt']);
    await tester.tap(find.byTooltip('Trier par'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Afficher les fichiers cachés'));
    await tester.pumpAndSettle();
    expect(api.viewPrefs.showHidden, isTrue);
    expect(titles(tester), contains('.cache'));
    await tester.tap(find.byTooltip('Trier par'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Grille'));
    await tester.pumpAndSettle();
    expect(api.viewPrefs.grid, isTrue);
  });

  testWidgets('tapFileOpensWith', (tester) async {
    final api = api0();
    await pump(tester, api);
    await tester.tap(find.text('b.txt'));
    await tester.pump();
    expect(api.calls, contains('openWith phone /b.txt'));
  });

  testWidgets('selectionCopyPasteStartsTransfer', (tester) async {
    final api = api0();
    final state = await pump(tester, api);
    await longPress(tester, 'a.jpg');
    await tester.tap(find.text('b.txt')); // en sélection, toucher ajoute
    await tester.pump();
    expect(find.text('2 sélectionné(s)'), findsOneWidget);
    await tester.tap(find.byTooltip('Copier'));
    await tester.pump();
    expect(find.text('2 élément(s) à copier'), findsOneWidget);
    // coller sur le disque
    tester.state<NavigatorState>(find.byType(Navigator)).push(
        MaterialPageRoute<void>(builder: (_) => ExplorerScreen(state: state, sourceId: 'v1')));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Coller ici'));
    await tester.pump();
    final (s, paths, dst, dir, move) = api.transfers.single;
    expect([s, ...paths, dst, dir, move], ['phone', '/a.jpg', '/b.txt', 'v1', '/', false]);
    // demande de l'utilisateur (2026-10-22) : « Coller ici » disparaît une fois la copie lancée
    expect(state.clipboard, isNull);
    expect(find.text('Coller ici'), findsNothing);
  });

  testWidgets('cutPasteMoves', (tester) async {
    final api = api0();
    final state = await pump(tester, api);
    await longPress(tester, 'b.txt');
    await tester.tap(find.byTooltip('Couper'));
    await tester.pump();
    expect(find.text('1 élément(s) à déplacer'), findsOneWidget);
    await tester.tap(find.text('DCIM'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Coller ici'));
    await tester.pump();
    expect(api.transfers.single.$5, isTrue);
    expect(api.transfers.single.$4, '/DCIM');
    expect(state.clipboard, isNull);
  });

  testWidgets('pasteDisabledOnReadOnly', (tester) async {
    final api = api0()..sources = [src('phone'), src('v1', kind: 'disk', label: 'Données', readOnly: true, roReason: 'NTFS en veille prolongée')];
    final state = await pump(tester, api, source: 'v1');
    expect(find.text('Lecture seule : NTFS en veille prolongée'), findsOneWidget);
    expect(find.byType(FloatingActionButton), findsNothing);
    state.setClipboard('phone', ['/b.txt'], move: false);
    await tester.pump();
    expect(tester.widget<FilledButton>(find.widgetWithText(FilledButton, 'Coller ici')).onPressed, isNull);
    await longPress(tester, 'film.mkv');
    expect(tester.widget<IconButton>(find.widgetWithIcon(IconButton, Icons.content_cut)).onPressed, isNull);
    expect(tester.widget<IconButton>(find.widgetWithIcon(IconButton, Icons.delete)).onPressed, isNull);
  });

  testWidgets('deleteAfterConfirm', (tester) async {
    final api = api0();
    await pump(tester, api);
    await longPress(tester, 'b.txt');
    await tester.tap(find.byTooltip('Supprimer'));
    await tester.pumpAndSettle();
    expect(find.text('Supprimer « b.txt » ?'), findsOneWidget);
    expect(find.text('La suppression est définitive.'), findsOneWidget);
    await tester.tap(find.text('Annuler'));
    await tester.pumpAndSettle();
    expect(api.calls.where((c) => c.startsWith('delete')), isEmpty);
    await tester.tap(find.text('a.jpg'));
    await tester.pump();
    await tester.tap(find.byTooltip('Supprimer'));
    await tester.pumpAndSettle();
    expect(find.text('Supprimer 2 éléments ?'), findsOneWidget);
    await tester.tap(find.widgetWithText(FilledButton, 'Supprimer'));
    await tester.pumpAndSettle();
    expect(api.calls.where((c) => c.startsWith('delete')).single, anyOf('delete phone /b.txt,/a.jpg', 'delete phone /a.jpg,/b.txt'));
  });

  testWidgets('renameDialog', (tester) async {
    final api = api0();
    await pump(tester, api);
    await longPress(tester, 'b.txt');
    await tester.tap(find.byTooltip('Renommer'));
    await tester.pumpAndSettle();
    expect(find.widgetWithText(TextField, 'b.txt'), findsOneWidget);
    await tester.enterText(find.byType(TextField), 'c.txt');
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('rename phone /b.txt c.txt'));
    await longPress(tester, 'a.jpg');
    await tester.tap(find.text('b.txt'));
    await tester.pump();
    expect(tester.widget<IconButton>(find.widgetWithIcon(IconButton, Icons.drive_file_rename_outline)).onPressed, isNull);
  });

  testWidgets('shareDisabledForFolder', (tester) async {
    final api = api0();
    await pump(tester, api);
    await longPress(tester, 'b.txt');
    await tester.tap(find.byTooltip('Partager'));
    await tester.pump();
    expect(api.calls, contains('share phone /b.txt'));
    await tester.tap(find.text('DCIM'));
    await tester.pump();
    expect(tester.widget<IconButton>(find.widgetWithIcon(IconButton, Icons.share)).onPressed, isNull);
  });

  testWidgets('transferBarProgressAndCancel', (tester) async {
    final api = api0();
    final state = await pump(tester, api);
    await state.startTransfer('v1', ['/film.mkv'], 'phone', '/', move: false);
    TransferMsg t({int done = 50, bool finished = false, String? error}) => TransferMsg(
        move: false, currentName: 'film.mkv', fileIndex: 1, fileCount: 1, doneBytes: done, totalBytes: 200,
        finished: finished, cancelled: false, error: error, elapsedMs: 0);
    state.onTransferChanged(t());
    await tester.pump();
    expect(find.text('Copie : film.mkv (1/1) — 25 %'), findsOneWidget);
    expect(tester.widget<LinearProgressIndicator>(find.byType(LinearProgressIndicator)).value, 0.25);
    await tester.tap(find.widgetWithText(TextButton, 'Annuler'));
    await tester.pump();
    expect(api.transferCancels, 1);
    final before = api.calls.where((c) => c == 'listDir phone / false').length;
    state.onTransferChanged(t(done: 200, finished: true));
    await tester.pump();
    expect(find.text('Terminé : 1 fichier(s)'), findsOneWidget);
    expect(api.calls.where((c) => c == 'listDir phone / false').length, before + 1); // liste rafraîchie
    await tester.pump(const Duration(seconds: 5));
    expect(find.text('Terminé : 1 fichier(s)'), findsNothing);
  });

  testWidgets('explorerPopsWhenSourceCloses', (tester) async {
    final api = api0();
    final state = await pump(tester, api, source: 'v1');
    expect(find.text('film.mkv'), findsOneWidget);
    api.sources = [src('phone')];
    await state.refreshSources();
    await tester.pumpAndSettle();
    expect(find.byType(ExplorerScreen), findsNothing);
    expect(find.text('Le disque a été fermé'), findsOneWidget);
  });

  testWidgets('favoriteStarToggles', (tester) async {
    final api = api0();
    await pump(tester, api, path: '/DCIM');
    await tester.tap(find.byTooltip('Ajouter aux favoris'));
    await tester.pumpAndSettle();
    expect(api.favorites.single.path, '/DCIM');
    expect(api.favorites.single.label, 'DCIM');
    await tester.tap(find.byTooltip('Retirer des favoris'));
    await tester.pumpAndSettle();
    expect(api.favorites, isEmpty);
  });

  testWidgets('protectedFolderMessage', (tester) async {
    final api = api0()..protectedDirs.add('/Android/data');
    await pump(tester, api, path: '/Android/data');
    expect(find.text('Dossier protégé par Android'), findsOneWidget);
    expect(find.text('Dossier vide'), findsNothing);
    api.dirs['phone']!['/vide'] = [];
    await pump(tester, api, path: '/vide');
    expect(find.text('Dossier vide'), findsOneWidget);
  });

  testWidgets('nouveau dossier', (tester) async {
    final api = api0();
    await pump(tester, api);
    await tester.tap(find.byType(FloatingActionButton));
    await tester.pumpAndSettle();
    await tester.enterText(find.byType(TextField), 'Vacances');
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('createFolder phone / Vacances'));
  });

  TransferMsg done({int bytes = 200, int elapsedMs = 0, String? error}) => TransferMsg(
      move: false, currentName: 'film.mkv', fileIndex: 1, fileCount: 1, doneBytes: bytes, totalBytes: bytes,
      finished: true, cancelled: false, error: error, elapsedMs: elapsedMs);

  testWidgets('transferDoneShowsRate', (tester) async {
    final state = await pump(tester, api0());
    state.onTransferChanged(done(bytes: 100000000, elapsedMs: 4000));
    await tester.pump();
    expect(find.text('Terminé : 1 fichier(s), 25 Mo/s'), findsOneWidget);
    await tester.pump(const Duration(seconds: 5));
  });

  testWidgets('transferResultShownOnce', (tester) async {
    final state = await pump(tester, api0());
    state.onTransferChanged(done());
    await tester.pump();
    expect(find.text('Terminé : 1 fichier(s)'), findsOneWidget);
    await tester.pump(const Duration(seconds: 5)); // résultat vu
    await tester.pageBack();
    await tester.pumpAndSettle();
    final nav = tester.state<NavigatorState>(find.byType(Navigator));
    nav.push(MaterialPageRoute<void>(builder: (_) => ExplorerScreen(state: state, sourceId: 'phone', path: '/DCIM')));
    await tester.pumpAndSettle();
    expect(find.textContaining('Terminé'), findsNothing);
    expect(state.transfer, isNull);
  });

  // --- relecture de la phase 6 (points mineurs)

  testWidgets('oneClosedMessageForStackedExplorers', (tester) async {
    final api = api0()..dirs['v1']!['/sous'] = [];
    final state = await pump(tester, api, source: 'v1');
    tester.state<NavigatorState>(find.byType(Navigator)).push(
        MaterialPageRoute<void>(builder: (_) => ExplorerScreen(state: state, sourceId: 'v1', path: '/sous')));
    await tester.pumpAndSettle();
    api.sources = [src('phone')];
    await state.refreshSources();
    await tester.pumpAndSettle();
    expect(find.byType(ExplorerScreen), findsNothing);
    expect(find.text('Le disque a été fermé'), findsOneWidget);
    await tester.pump(const Duration(seconds: 6)); // un second message attendrait son tour ici
    await tester.pumpAndSettle();
    expect(find.text('Le disque a été fermé'), findsNothing);
  });

  testWidgets('selectionPrunedAfterReload', (tester) async {
    final api = api0();
    final state = await pump(tester, api);
    await longPress(tester, 'b.txt');
    expect(find.text('1 sélectionné(s)'), findsOneWidget);
    api.dirs['phone']!['/'] = [d('/DCIM'), f('/a.jpg', size: 3000000)]; // b.txt supprimé par une autre application
    final v = state.viewPrefs;
    await state.setViewPrefs(ViewPrefsMsg(sort: v.sort, ascending: v.ascending, grid: v.grid, showHidden: true));
    await tester.pumpAndSettle();
    expect(find.text('1 sélectionné(s)'), findsNothing);
  });

  testWidgets('favoriteDisabledWithoutPartUuid', (tester) async {
    final api = api0()..sources = [src('phone'), src('v1', kind: 'disk', label: 'Données', key: 'disk:image:1#1')];
    await pump(tester, api, source: 'v1');
    final star = tester.widget<IconButton>(find.widgetWithIcon(IconButton, Icons.star_border));
    expect(star.onPressed, isNull);
  });

  testWidgets('encryptedFavoriteAsksConfirmation', (tester) async {
    final api = api0()..sources = [src('phone'), src('v1', kind: 'disk', label: 'Données', key: 'disk:u1', encrypted: true)];
    await pump(tester, api, source: 'v1');
    await tester.tap(find.byTooltip('Ajouter aux favoris'));
    await tester.pumpAndSettle();
    expect(find.text("Le nom de ce dossier restera visible sur l'accueil, même disque verrouillé"), findsOneWidget);
    await tester.tap(find.text('Annuler'));
    await tester.pumpAndSettle();
    expect(api.favorites, isEmpty);
    await tester.tap(find.byTooltip('Ajouter aux favoris'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Ajouter'));
    await tester.pumpAndSettle();
    expect(api.favorites.length, 1);
  });

  testWidgets('imageCacheClearedOnClose', (tester) async {
    final api = api0();
    final state = await pump(tester, api, source: 'v1');
    // une vignette décodée du disque, gardée par le cache d'images de Flutter
    final img = (await tester.runAsync(() => createTestImage(width: 4, height: 4)))!;
    final cache = PaintingBinding.instance.imageCache;
    cache.putIfAbsent('vignette', () => OneFrameImageStreamCompleter(SynchronousFuture(ImageInfo(image: img))));
    expect(cache.currentSize, 1);
    api.sources = [src('phone')];
    await state.refreshSources();
    await tester.pumpAndSettle();
    expect(cache.currentSize, 0);
  });
}
