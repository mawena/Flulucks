
import 'package:flutter/foundation.dart' show SynchronousFuture;
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/strings_fr.dart';
import 'package:fluluks/src/ui/states.dart';
import 'package:fluluks/src/ui/widgets.dart';

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
    expect(find.text('DCIM'), findsOneWidget); // fil d'Ariane : dossier ouvert
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
      ..viewPrefs = ViewPrefsMsg(sort: 'name', ascending: true, grid: true, showHidden: false, builtInViewers: true)
      ..thumbs['/a.jpg'] = png;
    await pump(tester, api);
    expect(find.byType(SliverGrid), findsOneWidget);
    expect(api.thumbRequests, ['/a.jpg']); // ni dossier ni texte
    expect(find.byType(Image), findsOneWidget);
    expect(find.byIcon(Icons.folder_rounded), findsOneWidget);
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
  });

  testWidgets('tapFileOpensWith', (tester) async {
    // « Ouvrir dans Portail » désactivé : le texte s'ouvre dans une autre application
    final api = api0()
      ..viewPrefs = ViewPrefsMsg(sort: 'name', ascending: true, grid: false, showHidden: false, builtInViewers: false);
    await pump(tester, api);
    await tester.tap(find.text('b.txt'));
    await tester.pump();
    expect(api.calls, contains('openWith phone /b.txt'));
  });

  testWidgets('tapVideoPlaysInPortail', (tester) async {
    final api = api0();
    await pump(tester, api, source: 'v1');
    await tester.tap(find.text('film.mkv'));
    await tester.pump();
    expect(api.calls, contains('playVideo v1 /film.mkv'));
    expect(api.calls, isNot(contains('openWith v1 /film.mkv')));
  });

  testWidgets('selectionCopyPasteStartsTransfer', (tester) async {
    final api = api0();
    final state = await pump(tester, api);
    await longPress(tester, 'a.jpg');
    await tester.tap(find.text('b.txt')); // en sélection, toucher ajoute
    await tester.pump();
    expect(find.text('2 sélectionnés'), findsOneWidget);
    await tester.tap(find.byTooltip('Copier'));
    await tester.pump();
    expect(find.text('2 éléments à copier'), findsOneWidget);
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
    expect(find.text('1 élément à déplacer'), findsOneWidget);
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
    expect(find.text(Fr.readOnly('NTFS en veille prolongée')), findsOneWidget);
    expect(find.byType(FloatingActionButton), findsNothing);
    state.setClipboard('phone', ['/b.txt'], move: false);
    await tester.pump();
    expect(tester.widget<FilledButton>(find.widgetWithText(FilledButton, 'Coller ici')).onPressed, isNull);
    await longPress(tester, 'film.mkv');
    expect(tester.widget<IconButton>(find.widgetWithIcon(IconButton, Icons.content_cut_rounded)).onPressed, isNull);
    expect(tester.widget<IconButton>(find.widgetWithIcon(IconButton, Icons.delete_rounded)).onPressed, isNull);
  });

  testWidgets('deleteAfterConfirm', (tester) async {
    final api = api0();
    await pump(tester, api);
    await longPress(tester, 'b.txt');
    await tester.tap(find.byTooltip('Supprimer'));
    await tester.pumpAndSettle();
    expect(find.text(Fr.deleteOne('b.txt')), findsOneWidget);
    expect(find.text('Les éléments restent 30 jours dans la corbeille de ce stockage.'), findsOneWidget);
    await tester.tap(find.text('Annuler'));
    await tester.pumpAndSettle();
    expect(api.calls.where((c) => c.startsWith('delete') || c.startsWith('trash')), isEmpty);
    await tester.tap(find.text('a.jpg'));
    await tester.pump();
    await tester.tap(find.byTooltip('Supprimer'));
    await tester.pumpAndSettle();
    expect(find.text(Fr.deleteMany(2)), findsOneWidget);
    await tester.tap(find.widgetWithText(FilledButton, 'Supprimer'));
    await tester.pumpAndSettle();
    expect(api.calls.where((c) => c.startsWith('trash')).single, anyOf('trash phone /b.txt,/a.jpg', 'trash phone /a.jpg,/b.txt'));
  });

  testWidgets('renameDialog', (tester) async {
    final api = api0();
    await pump(tester, api);
    await longPress(tester, 'b.txt');
    await tester.tap(find.byTooltip("Plus d'actions"));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Renommer'));
    await tester.pumpAndSettle();
    expect(find.widgetWithText(TextField, 'b.txt'), findsOneWidget);
    await tester.enterText(find.byType(TextField), 'c.txt');
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('rename phone /b.txt c.txt'));
    await longPress(tester, 'a.jpg');
    await tester.tap(find.text('b.txt'));
    await tester.pump();
    await tester.tap(find.byTooltip("Plus d'actions"));
    await tester.pumpAndSettle();
    // plusieurs éléments : renommage par lot (test/batch_rename_test.dart)
    expect(tester.widget<PopupMenuItem<String>>(find.widgetWithText(PopupMenuItem<String>, 'Renommer')).enabled, isTrue);
  });

  testWidgets('renameSelectsBaseNameAndValidates', (tester) async {
    final api = api0();
    await pump(tester, api);
    await longPress(tester, 'b.txt');
    await tester.tap(find.byTooltip("Plus d'actions"));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Renommer'));
    await tester.pumpAndSettle();
    final controller = tester.widget<TextField>(find.byType(TextField)).controller!;
    expect(controller.selection, const TextSelection(baseOffset: 0, extentOffset: 1)); // « b » sans « .txt »
    FilledButton ok() => tester.widget<FilledButton>(find.widgetWithText(FilledButton, Fr.ok));
    await tester.enterText(find.byType(TextField), 'a/b');
    await tester.pump();
    expect(ok().onPressed, isNull);
    expect(find.text(Fr.nameSlash), findsOneWidget);
    await tester.enterText(find.byType(TextField), 'b.txt'); // même nom : rien n'est envoyé
    await tester.pump();
    await tester.tap(find.text(Fr.ok));
    await tester.pumpAndSettle();
    expect(api.calls.where((c) => c.startsWith('rename')), isEmpty);
  });

  testWidgets('newFolderRejectsReservedNames', (tester) async {
    final api = api0();
    await pump(tester, api);
    await tester.tap(find.byTooltip(Fr.newFolder));
    await tester.pumpAndSettle();
    FilledButton ok() => tester.widget<FilledButton>(find.widgetWithText(FilledButton, Fr.ok));
    expect(ok().onPressed, isNull); // vide
    await tester.enterText(find.byType(TextField), '..');
    await tester.pump();
    expect(ok().onPressed, isNull);
    expect(find.text(Fr.nameReserved), findsOneWidget);
    await tester.enterText(find.byType(TextField), 'Vacances');
    await tester.pump();
    await tester.tap(find.text(Fr.ok));
    await tester.pumpAndSettle();
    expect(api.calls, contains('createFolder phone / Vacances'));
  });

  testWidgets('shareDisabledForFolder', (tester) async {
    final api = api0();
    await pump(tester, api);
    await longPress(tester, 'b.txt');
    await tester.tap(find.byTooltip(Fr.moreActions));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Partager'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('share phone /b.txt'));
    await tester.tap(find.text('DCIM')); // toujours en sélection : le dossier s'y ajoute
    await tester.pump();
    await tester.tap(find.byTooltip(Fr.moreActions));
    await tester.pumpAndSettle();
    expect(tester.widget<PopupMenuItem<String>>(find.widgetWithText(PopupMenuItem<String>, 'Partager')).enabled, isFalse);
  });

  testWidgets('transferBarProgressAndCancel', (tester) async {
    final api = api0();
    final state = await pump(tester, api);
    await state.startTransfer('v1', ['/film.mkv'], 'phone', '/', move: false);
    TransferMsg t({int done = 50, bool finished = false, String? error}) => TransferMsg(
        kind: 'copy', move: false, currentName: 'film.mkv', fileIndex: 1, fileCount: 1, doneBytes: done, totalBytes: 200,
        finished: finished, cancelled: false, error: error, elapsedMs: 0);
    state.onTransferChanged(t());
    await tester.pump();
    expect(find.text(Fr.transferLine('copy', 'film.mkv', 1, 1, 25)), findsOneWidget);
    expect(tester.widget<LinearProgressIndicator>(find.byType(LinearProgressIndicator)).value, 0.25);
    await tester.tap(find.widgetWithText(TextButton, 'Annuler'));
    await tester.pump();
    expect(api.transferCancels, 1);
    final before = api.calls.where((c) => c == 'listDir phone / false').length;
    state.onTransferChanged(t(done: 200, finished: true));
    await tester.pump();
    expect(find.text(Fr.transferDone(1)), findsOneWidget);
    expect(api.calls.where((c) => c == 'listDir phone / false').length, before + 1); // liste rafraîchie
    await tester.pump(const Duration(seconds: 5));
    expect(find.text(Fr.transferDone(1)), findsNothing);
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
    await tester.pump(); // OK s'active une fois le nom validé
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('createFolder phone / Vacances'));
  });

  TransferMsg done({int bytes = 200, int elapsedMs = 0, String? error}) => TransferMsg(
      kind: 'copy', move: false, currentName: 'film.mkv', fileIndex: 1, fileCount: 1, doneBytes: bytes, totalBytes: bytes,
      finished: true, cancelled: false, error: error, elapsedMs: elapsedMs);

  testWidgets('transferDoneShowsRate', (tester) async {
    final state = await pump(tester, api0());
    state.onTransferChanged(done(bytes: 100000000, elapsedMs: 4000));
    await tester.pump();
    expect(find.text('${Fr.transferDone(1)}, 25 Mo/s'), findsOneWidget);
    await tester.pump(const Duration(seconds: 5));
  });

  testWidgets('transferResultShownOnce', (tester) async {
    final state = await pump(tester, api0());
    state.onTransferChanged(done());
    await tester.pump();
    expect(find.text(Fr.transferDone(1)), findsOneWidget);
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
    expect(find.text('1 sélectionné'), findsOneWidget);
    api.dirs['phone']!['/'] = [d('/DCIM'), f('/a.jpg', size: 3000000)]; // b.txt supprimé par une autre application
    final v = state.viewPrefs;
    await state.setViewPrefs(ViewPrefsMsg(sort: v.sort, ascending: v.ascending, grid: v.grid, showHidden: true, builtInViewers: v.builtInViewers));
    await tester.pumpAndSettle();
    expect(find.text('1 sélectionné'), findsNothing);
  });

  testWidgets('favoriteDisabledWithoutPartUuid', (tester) async {
    final api = api0()..sources = [src('phone'), src('v1', kind: 'disk', label: 'Données', key: 'disk:image:1#1')];
    await pump(tester, api, source: 'v1');
    final star = tester.widget<IconButton>(find.widgetWithIcon(IconButton, Icons.star_border_rounded));
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
    expect(cache.containsKey('vignette'), isTrue); // le logo de l'accueil y est aussi : compter ne suffit plus
    api.sources = [src('phone')];
    await state.refreshSources();
    await tester.pumpAndSettle();
    expect(cache.containsKey('vignette'), isFalse);
  });

  // phase 11, T6 : collection d'éléments et explorateur revu

  testWidgets('gridAdaptsColumns', (tester) async {
    addTearDown(tester.view.reset);
    tester.view.devicePixelRatio = 1.0;
    final api = api0()
      ..viewPrefs = ViewPrefsMsg(sort: 'name', ascending: true, grid: true, showHidden: false, builtInViewers: true)
      ..dirs['phone']!['/'] = [for (var i = 10; i < 22; i++) f('/f$i.txt')];
    int firstRow() {
      final tops = [for (var i = 10; i < 22; i++) tester.getTopLeft(find.byKey(ValueKey('phone|/f$i.txt'))).dy];
      return tops.where((y) => y == tops.first).length;
    }

    tester.view.physicalSize = const Size(412, 869);
    await pump(tester, api);
    expect(firstRow(), 3);
    tester.view.physicalSize = const Size(1280, 800);
    await tester.pumpAndSettle();
    expect(firstRow(), greaterThanOrEqualTo(9));
  });

  testWidgets('breadcrumbJumpsToAncestor', (tester) async {
    final api = api0()
      ..dirs['phone']!['/a'] = [d('/a/b')]
      ..dirs['phone']!['/a/b'] = [d('/a/b/c')]
      ..dirs['phone']!['/a/b/c'] = [f('/a/b/c/x.txt')];
    await pump(tester, api, path: '/a/b/c');
    expect(find.text('x.txt'), findsOneWidget);
    await tester.tap(find.text('a'));
    await tester.pumpAndSettle();
    expect(api.calls.last, 'listDir phone /a false');
    expect(titles(tester), ['b']);
    await tester.tap(find.byTooltip('Téléphone')); // racine du stockage
    await tester.pumpAndSettle();
    expect(titles(tester), ['DCIM', 'a.jpg', 'b.txt']);
  });

  testWidgets('selectAllSelectsEverything', (tester) async {
    final haptics = <String?>[];
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(SystemChannels.platform, (call) async {
      if (call.method == 'HapticFeedback.vibrate') haptics.add(call.arguments as String?);
      return null;
    });
    addTearDown(() => tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(SystemChannels.platform, null));
    final api = api0()..dirs['phone']!['/'] = [d('/DCIM'), f('/a.jpg'), f('/b.txt'), f('/c.pdf')];
    await pump(tester, api);
    await longPress(tester, 'b.txt');
    expect(haptics, ['HapticFeedbackType.selectionClick']);
    await tester.tap(find.byTooltip(Fr.selectAll));
    await tester.pump();
    expect(find.text('4 sélectionnés'), findsOneWidget);
    expect(haptics, ['HapticFeedbackType.selectionClick', 'HapticFeedbackType.selectionClick']);
  });

  testWidgets('itemMenuOffersActions', (tester) async {
    final api = api0();
    final state = await pump(tester, api);
    await tester.tap(find.bySemanticsLabel(Fr.actionsOf('b.txt')));
    await tester.pumpAndSettle();
    for (final a in [Fr.openWith, Fr.share, Fr.copy, Fr.cut, Fr.rename, Fr.compress, Fr.properties, Fr.delete]) {
      expect(find.widgetWithText(ListTile, a), findsOneWidget, reason: a);
    }
    await tester.tap(find.widgetWithText(ListTile, Fr.copy));
    await tester.pumpAndSettle();
    expect(state.clipboard?.paths, ['/b.txt']);
    expect(state.clipboard?.move, isFalse);
    expect(find.text('1 sélectionné'), findsNothing); // la sélection n'est pas touchée

    // lecture seule : écriture désactivée
    final ro = api0()..sources = [src('phone', readOnly: true, roReason: 'Carte protégée')];
    await pump(tester, ro);
    await tester.tap(find.bySemanticsLabel(Fr.actionsOf('b.txt')));
    await tester.pumpAndSettle();
    for (final a in [Fr.cut, Fr.rename, Fr.compress, Fr.delete]) {
      expect(tester.widget<ListTile>(find.widgetWithText(ListTile, a)).enabled, isFalse, reason: a);
    }
    expect(tester.widget<ListTile>(find.widgetWithText(ListTile, Fr.copy)).enabled, isTrue);
  });

  testWidgets('deleteToTrashOffersUndo', (tester) async {
    final later = DateTime.now().millisecondsSinceEpoch + 60000;
    TrashItemMsg t(String id, String path, int at) =>
        TrashItemMsg(id: id, name: path.split('/').last, originalPath: path, deletedAt: at, size: 1000, isDir: false);
    final api = api0()
      ..trash['phone'] = [t('t1', '/b.txt', later), t('ancien', '/b.txt', 1000), t('autre', '/z.txt', later)];
    await pump(tester, api);
    await longPress(tester, 'b.txt');
    await tester.tap(find.byTooltip('Supprimer'));
    await tester.pumpAndSettle();
    await tester.tap(find.widgetWithText(FilledButton, 'Supprimer'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('trash phone /b.txt'));
    expect(find.text(Fr.movedToTrash(1)), findsOneWidget);
    await tester.tap(find.widgetWithText(SnackBarAction, Fr.undo));
    await tester.pumpAndSettle();
    expect(api.calls, contains('restoreTrash phone t1'));
    expect(api.calls.last, 'listDir phone / false'); // liste rafraîchie
  });

  testWidgets('emptyFolderShowsEmptyState', (tester) async {
    final api = api0()..dirs['phone']!['/vide'] = [];
    await pump(tester, api, path: '/vide');
    expect(find.byType(EmptyState), findsOneWidget);
    expect(find.text(Fr.emptyFolder), findsOneWidget);
  });

  testWidgets('errorShowsRetry', (tester) async {
    final api = api0()..protectedDirs.add('/DCIM');
    await pump(tester, api, path: '/DCIM');
    expect(find.byType(ErrorState), findsOneWidget);
    api.protectedDirs.clear();
    await tester.tap(find.text(Fr.retry));
    await tester.pumpAndSettle();
    expect(api.calls.where((c) => c == 'listDir phone /DCIM false').length, 2);
    expect(titles(tester), ['photo.jpg']);
  });

  testWidgets('readOnlyHidesWriteActions', (tester) async {
    final api = api0()..sources = [src('phone', readOnly: true, roReason: 'Carte protégée')];
    await pump(tester, api);
    expect(find.widgetWithText(StatusBanner, Fr.readOnly('Carte protégée')), findsOneWidget);
    expect(find.byType(FloatingActionButton), findsNothing);
    await longPress(tester, 'b.txt');
    expect(tester.widget<IconButton>(find.widgetWithIcon(IconButton, Icons.content_cut_rounded)).onPressed, isNull);
    expect(tester.widget<IconButton>(find.widgetWithIcon(IconButton, Icons.delete_rounded)).onPressed, isNull);
    expect(tester.widget<IconButton>(find.widgetWithIcon(IconButton, Icons.copy_rounded)).onPressed, isNotNull);
  });

  testWidgets('gridTileSemantics', (tester) async {
    final handle = tester.ensureSemantics();
    final api = api0()
      ..viewPrefs = ViewPrefsMsg(sort: 'name', ascending: true, grid: true, showHidden: false, builtInViewers: true);
    await pump(tester, api);
    final label = 'a.jpg, image, ${Fr.humanSize(3000000)}';
    expect(find.bySemanticsLabel(label), findsOneWidget);
    expect(find.bySemanticsLabel('DCIM, dossier'), findsOneWidget);
    await tester.longPress(find.bySemanticsLabel(label));
    await tester.pumpAndSettle();
    expect(tester.getSemantics(find.bySemanticsLabel(label)), isSemantics(label: label, isSelected: true, isButton: true));
    handle.dispose();
  });

  testWidgets('viewToggleFlipsPreference', (tester) async {
    final api = api0();
    await pump(tester, api);
    expect(find.byType(SliverGrid), findsNothing);
    await tester.tap(find.byTooltip(Fr.showGrid));
    await tester.pumpAndSettle();
    expect(api.viewPrefs.grid, isTrue);
    expect(find.byType(SliverGrid), findsOneWidget);
    await tester.tap(find.byTooltip(Fr.showList));
    await tester.pumpAndSettle();
    expect(api.viewPrefs.grid, isFalse);
    expect(titles(tester), ['DCIM', 'a.jpg', 'b.txt']);
  });

  testWidgets('itemMenuButtonLightweight', (tester) async {
    // T12 : l'infobulle d'un IconButton par ligne doublait le coût du défilement (90e centile 12 → 16 ms sur le Pixel)
    final handle = tester.ensureSemantics();
    await pump(tester, api0());
    final rows = find.byType(SliverPrototypeExtentList);
    expect(find.descendant(of: rows, matching: find.byType(Tooltip)), findsNothing);
    final more = find.bySemanticsLabel(Fr.actionsOf('b.txt'));
    expect(more, findsOneWidget);
    expect(tester.getSize(more).width, greaterThanOrEqualTo(48));
    expect(tester.getSize(more).height, greaterThanOrEqualTo(48));
    handle.dispose();
  });
}
