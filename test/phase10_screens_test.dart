import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:fluluks/src/screens/image_viewer_screen.dart';
import 'package:fluluks/src/screens/now_playing_screen.dart';
import 'package:fluluks/src/screens/server_form_screen.dart';
import 'package:fluluks/src/screens/text_editor_screen.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/strings_fr.dart';
import 'package:fluluks/src/ui/states.dart';
import 'package:fluluks/src/ui/widgets.dart';
import 'package:fluluks/src/widgets/bottom_dock.dart';
import 'package:fluluks/src/widgets/clipboard_bar.dart';
import 'package:fluluks/src/widgets/mini_player.dart';
import 'package:fluluks/src/widgets/transfer_bar.dart';

import 'fake_host_api.dart';
import 'image_viewer_test.dart' show png;

/// Écrans de la phase 10 alignés sur les composants, et barres du bas réunies (phase 11, T10).

EntryMsg f(String path) => EntryMsg(name: path.split('/').last, path: path, type: 'file', size: 1000, mtime: 1700000000);

SourceMsg src(String id) => SourceMsg(
      id: id,
      stableKey: id,
      kind: 'phone',
      label: 'Téléphone',
      totalBytes: 1000,
      freeBytes: 500,
      readOnly: false,
      roReason: '',
      encrypted: false,
    );

TextFileMsg text(String t, {bool bom = false, bool crlf = false}) =>
    TextFileMsg(text: t, bom: bom, crlf: crlf, mtime: 1700000000, size: t.length);

PlayerMsg playing() =>
    PlayerMsg(sourceId: 'phone', title: 'b.flac', index: 1, count: 2, playing: true, positionMs: 61000, durationMs: 185000);

TransferMsg copying() => TransferMsg(
      move: false,
      currentName: 'film.mkv',
      fileIndex: 1,
      fileCount: 3,
      doneBytes: 50,
      totalBytes: 100,
      finished: false,
      cancelled: false,
      elapsedMs: 1000,
      kind: 'copy',
    );

FakeHostApi api0() {
  final api = FakeHostApi()
    ..sources = [src('phone')]
    ..dirs['phone'] = {
      '/': [f('/a.jpg'), f('/notes.txt')],
    }
    ..texts['/notes.txt'] = text('ligne 1\nligne 2', bom: true, crlf: true)
    ..texts['/README'] = text('lisez-moi');
  for (final p in ['/a.jpg', '/b.jpg', '/c.jpg', '/d.jpg', '/e.jpg']) {
    api.images[p] = png;
  }
  return api;
}

/// L'application, puis [screen] poussé par-dessus l'accueil.
Future<AppState> open(WidgetTester tester, FakeHostApi api, Widget Function(AppState) screen) async {
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  tester.state<NavigatorState>(find.byType(Navigator)).push(MaterialPageRoute<void>(builder: (_) => screen(state)));
  await tester.pumpAndSettle();
  return state;
}

Widget explorer(AppState s) => ExplorerScreen(state: s, sourceId: 'phone');

Widget editor(AppState s, String path) =>
    TextEditorScreen(state: s, sourceId: 'phone', path: path, name: path.substring(1));

Widget form(AppState s) => ServerFormScreen(state: s);

Future<void> saveForm(WidgetTester tester) async {
  final save = find.widgetWithText(FilledButton, Fr.save);
  await tester.scrollUntilVisible(save, 200,
      scrollable: find.descendant(of: find.byType(ServerFormScreen), matching: find.byType(Scrollable)).first);
  await tester.tap(save);
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('dockOrder', (tester) async {
    final state = await open(tester, api0(), explorer);
    state.setClipboard('phone', ['/a.jpg'], move: false);
    state.onPlayerChanged(playing(), null);
    state.onTransferChanged(copying());
    await tester.pumpAndSettle();
    final pending = tester.getCenter(find.text(Fr.clipboard(1, false))).dy;
    final player = tester.getCenter(find.descendant(of: find.byType(MiniPlayer), matching: find.text('b.flac'))).dy;
    final copy = tester.getCenter(find.textContaining('Copie de film.mkv')).dy;
    expect(pending, lessThan(player)); // presse-papiers au-dessus du lecteur
    expect(player, lessThan(copy)); // lecteur au-dessus de la copie
    // un seul exemplaire de chaque barre
    expect(find.byType(MiniPlayer), findsOneWidget);
    expect(find.textContaining('Copie de film.mkv'), findsOneWidget);
    // la barre de copie reste au bas de l'écran
    expect(tester.getBottomLeft(find.byType(TransferBar)).dy, closeTo(600, 1));
  });

  testWidgets('dockHiddenWhenIdle', (tester) async {
    final state = await open(tester, api0(), explorer);
    expect(find.byType(ClipboardBar), findsNothing);
    expect(find.byType(MiniPlayer), findsNothing);
    expect(find.byType(TransferBar), findsNothing);
    final docks = find.byType(BottomDock).evaluate().toList();
    expect(docks, isNotEmpty);
    for (final d in docks) {
      expect(d.size!.height, 0);
    }
    // une copie terminée puis vue : le dock redevient vide
    state.onTransferChanged(copying()
      ..finished = true
      ..fileIndex = 3);
    await tester.pump();
    expect(find.byType(TransferBar), findsOneWidget);
    state.transferResultSeen();
    await tester.pumpAndSettle();
    expect(find.byType(TransferBar), findsNothing);
  });

  testWidgets('editorStatusLine', (tester) async {
    final state = await open(tester, api0(), (s) => editor(s, '/notes.txt'));
    expect(find.text(Fr.editorStatus(bom: true, crlf: true, lines: 2)), findsOneWidget);
    expect(find.text('UTF-8 avec BOM · CRLF · 2 lignes'), findsOneWidget);
    await tester.enterText(find.byType(TextField), 'a\nb\nc\n');
    await tester.pump();
    expect(find.text('UTF-8 avec BOM · CRLF · 3 lignes'), findsOneWidget); // suit la saisie
    final mono = tester.widget<TextField>(find.byType(TextField)).style!;
    expect(mono.fontFamily, 'monospace');
    tester.state<NavigatorState>(find.byType(Navigator)).push(
        MaterialPageRoute<void>(builder: (_) => editor(state, '/README')));
    await tester.pumpAndSettle();
    expect(find.text('UTF-8 · LF · 1 ligne'), findsOneWidget);
  });

  testWidgets('editorErrorStateRetry', (tester) async {
    final api = api0()..readErrors['/notes.txt'] = "Ce fichier n'est pas du texte UTF-8";
    await open(tester, api, (s) => editor(s, '/notes.txt'));
    expect(find.widgetWithText(ErrorState, "Ce fichier n'est pas du texte UTF-8"), findsOneWidget);
    expect(find.byType(TextField), findsNothing);
    api.readErrors.clear();
    await tester.tap(find.text(Fr.retry));
    await tester.pumpAndSettle();
    expect(find.byType(ErrorState), findsNothing);
    expect(find.text('ligne 1\nligne 2'), findsOneWidget);
    expect(api.calls.where((c) => c == 'readText phone /notes.txt'), hasLength(2));
  });

  testWidgets('editorReadOnlyChip', (tester) async {
    final api = api0()
      ..texts['/notes.txt'] = TextFileMsg(
          text: 'figé', bom: false, crlf: false, mtime: 0, size: 4, readOnlyReason: 'Stockage en lecture seule');
    await open(tester, api, (s) => editor(s, '/notes.txt'));
    final chip = find.widgetWithText(StatusChip, Fr.readOnlyText('Stockage en lecture seule'));
    expect(chip, findsOneWidget);
    expect(tester.widget<StatusChip>(chip).tone, ChipTone.warning);
  });

  testWidgets('imageViewerCounterTabular', (tester) async {
    final images = [for (final p in ['/a.jpg', '/b.jpg', '/c.jpg', '/d.jpg', '/e.jpg']) f(p)];
    await open(tester, api0(),
        (s) => ImageViewerScreen(state: s, sourceId: 'phone', images: images, initialIndex: 2));
    final counter = tester.widget<Text>(find.text('3 / 5'));
    expect(counter.style?.fontFeatures, contains(const FontFeature.tabularFigures()));
    for (final tip in [Fr.share, Fr.openWith, Fr.properties]) {
      expect(find.byTooltip(tip), findsOneWidget);
    }
    final scaffold = tester.widget<Scaffold>(find.byType(Scaffold).last);
    expect(scaffold.backgroundColor, Colors.black); // fond noir gardé
  });

  testWidgets('miniPlayerSemantics', (tester) async {
    final handle = tester.ensureSemantics();
    final state = await open(tester, api0(), explorer);
    state.onPlayerChanged(playing(), null);
    await tester.pumpAndSettle();
    expect(find.bySemanticsLabel(Fr.playingNow('b.flac')), findsOneWidget);
    expect(Fr.playingNow('b.flac'), 'Lecture en cours : b.flac');
    handle.dispose();
  });

  testWidgets('nowPlayingSliderSemantics', (tester) async {
    final handle = tester.ensureSemantics();
    final api = api0();
    final state = AppState(api);
    await state.refresh();
    state.onPlayerChanged(playing(), null);
    await tester.pumpWidget(FluLuksApp(state: state));
    tester.state<NavigatorState>(find.byType(Navigator)).push(
        MaterialPageRoute<void>(builder: (_) => NowPlayingScreen(state: state)));
    await tester.pumpAndSettle();
    expect(Fr.positionOf(61000, 185000), 'Position 1:01 sur 3:05');
    expect(tester.getSemantics(find.byType(Slider)), isSemantics(value: 'Position 1:01 sur 3:05'));
    for (final t in ['1:01', '3:05']) {
      expect(tester.widget<Text>(find.text(t)).style?.fontFeatures, contains(const FontFeature.tabularFigures()));
    }
    expect(find.byKey(const ValueKey('artwork')), findsOneWidget); // pochette de remplacement
    handle.dispose();
  });

  testWidgets('serverFormValidation', (tester) async {
    final api = api0();
    await open(tester, api, form);
    // adresse vide
    await saveForm(tester);
    expect(find.text(Fr.hostRequired), findsOneWidget);
    expect(api.savedServers, isEmpty);
    await tester.enterText(find.widgetWithText(TextFormField, Fr.serverHost), 'nas.local');
    for (final port in ['0', '70000', 'abc']) {
      await tester.enterText(find.widgetWithText(TextFormField, Fr.serverPort), port);
      await saveForm(tester);
      expect(find.text(Fr.portInvalid), findsOneWidget, reason: port);
      expect(api.savedServers, isEmpty, reason: port);
    }
    await tester.enterText(find.widgetWithText(TextFormField, Fr.serverPort), '2222');
    await saveForm(tester);
    expect(api.savedServers.single.$1.port, 2222);
    expect(find.byType(ServerFormScreen), findsNothing);
  });

  testWidgets('ftpWarningBanner', (tester) async {
    await open(tester, api0(), form);
    expect(find.byType(StatusBanner), findsNothing); // SFTP
    await tester.tap(find.text('FTP'));
    await tester.pumpAndSettle();
    final banner = find.widgetWithText(StatusBanner, Fr.ftpClear);
    expect(banner, findsOneWidget);
    expect(tester.widget<StatusBanner>(banner).tone, ChipTone.warning);
    await tester.tap(find.text('SFTP'));
    await tester.pumpAndSettle();
    expect(find.byType(StatusBanner), findsNothing);
  });

  testWidgets('hostKeyMonospaceWithCopy', (tester) async {
    final server = RemoteServerMsg(
      id: 's1',
      protocol: 'sftp',
      name: 'Maison',
      host: 'nas.local',
      port: 22,
      user: 'charles',
      startPath: '/',
      remember: false,
      hostKey: 'ssh-ed25519 SHA256:abc',
      hasPassword: false,
      connected: false,
    );
    await open(tester, api0(), (s) => ServerFormScreen(state: s, server: server));
    await tester.scrollUntilVisible(find.text(Fr.forgetHostKey), 200,
        scrollable: find.descendant(of: find.byType(ServerFormScreen), matching: find.byType(Scrollable)).first);
    await tester.ensureVisible(find.byTooltip(Fr.copyHash));
    await tester.pumpAndSettle();
    expect(tester.widget<SelectableText>(find.widgetWithText(SelectableText, 'ssh-ed25519 SHA256:abc')).style?.fontFamily, 'monospace');
    final copied = <Object?>[];
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(SystemChannels.platform, (call) async {
      if (call.method == 'Clipboard.setData') copied.add((call.arguments as Map)['text']);
      return null;
    });
    await tester.tap(find.byTooltip(Fr.copyHash));
    await tester.pumpAndSettle();
    expect(copied, ['ssh-ed25519 SHA256:abc']);
    expect(find.text(Fr.hashCopied), findsOneWidget);
  });
}
