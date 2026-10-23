import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/now_playing_screen.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/widgets/mini_player.dart';

import 'explorer_screen_test.dart' show f, pump, src;
import 'fake_host_api.dart';

FakeHostApi api0() => FakeHostApi()
  ..sources = [src('phone')]
  ..dirs['phone'] = {
    '/': [f('/a.mp3'), f('/notes.txt'), f('/b.flac')],
  };

PlayerMsg playing({bool on = true}) =>
    PlayerMsg(sourceId: 'phone', title: 'b.flac', index: 1, count: 2, playing: on, positionMs: 61000, durationMs: 185000);

Future<AppState> started(WidgetTester tester, FakeHostApi api) async {
  final state = await pump(tester, api);
  state.onPlayerChanged(playing(), null);
  await tester.pumpAndSettle();
  return state;
}

void main() {
  testWidgets('tapAudioStartsQueue', (tester) async {
    final api = api0();
    await pump(tester, api);
    expect(find.byType(MiniPlayer), findsNothing); // rien en cours
    await tester.tap(find.text('b.flac'));
    await tester.pump();
    expect(api.calls, contains('playAudio phone /a.mp3,/b.flac,/notes.txt 1'));
  });

  testWidgets('miniPlayerShowsAndControls', (tester) async {
    final api = api0();
    await started(tester, api);
    expect(find.descendant(of: find.byType(MiniPlayer), matching: find.text('b.flac')), findsOneWidget);
    await tester.tap(find.byTooltip('Pause'));
    await tester.pump();
    expect(api.calls, contains('playerCommand pause null'));
  });

  testWidgets('nowPlayingScreen', (tester) async {
    final api = api0();
    final state = await started(tester, api);
    await tester.tap(find.descendant(of: find.byType(MiniPlayer), matching: find.text('b.flac')));
    await tester.pumpAndSettle();
    expect(find.byType(NowPlayingScreen), findsOneWidget);
    expect(find.byType(MiniPlayer), findsNothing); // la barre réduite s'efface devant l'écran complet
    expect(find.text('Morceau 2 sur 2'), findsOneWidget);
    expect(find.text('1:01'), findsOneWidget);
    expect(find.text('3:05'), findsOneWidget);
    expect(find.byType(Slider), findsOneWidget);
    for (final (tip, cmd) in [('Précédent', 'previous'), ('Suivant', 'next'), ('Reculer de 10 s', 'back'), ('Avancer de 10 s', 'forward')]) {
      await tester.tap(find.byTooltip(tip));
      await tester.pump();
      expect(api.calls, contains('playerCommand $cmd null'));
    }
    state.onPlayerChanged(null, null); // arrêt : l'écran se ferme
    await tester.pumpAndSettle();
    expect(find.byType(NowPlayingScreen), findsNothing);
  });

  testWidgets('disappearsOnStopWithMessage', (tester) async {
    final state = await started(tester, api0());
    state.onPlayerChanged(null, 'Lecture arrêtée : stockage fermé');
    await tester.pumpAndSettle();
    expect(find.byType(MiniPlayer), findsNothing);
    expect(find.text('Lecture arrêtée : stockage fermé'), findsOneWidget);
  });
}
