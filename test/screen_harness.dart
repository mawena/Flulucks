import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/about_screen.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:fluluks/src/screens/filter_screen.dart';
import 'package:fluluks/src/screens/format_screen.dart';
import 'package:fluluks/src/screens/global_search_screen.dart';
import 'package:fluluks/src/screens/help_screen.dart';
import 'package:fluluks/src/screens/now_playing_screen.dart';
import 'package:fluluks/src/screens/search_screen.dart';
import 'package:fluluks/src/screens/server_form_screen.dart';
import 'package:fluluks/src/screens/settings_screen.dart';
import 'package:fluluks/src/screens/space_screen.dart';
import 'package:fluluks/src/screens/text_editor_screen.dart';
import 'package:fluluks/src/screens/trash_screen.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/ui/root.dart';
import 'package:fluluks/src/widgets/unlock_dialog.dart';

import 'demo_data.dart';
import 'fake_host_api.dart';

/// Écrans que [pumpScreen] sait afficher, sur les données de [demoApi].
const screens = [
  'home', 'explorerList', 'explorerGrid', 'filterGrid', 'search', 'globalSearch', 'trash', 'space', 'settings', //
  'about', 'help', 'welcome', 'unlock', 'format', 'editor', 'serverForm', 'nowPlaying',
];

/// Pont et état du dernier [pumpScreen] (pour les tests qui agissent ensuite sur l'écran).
late FakeHostApi harnessApi;
late AppState harnessState;

/// Affiche [screen] (une valeur de [screens]) sur les données de démonstration, comme dans l'application (racine
/// `PortailRoot` : taille du texte bornée à 200 %), en [brightness], à [size] dp et à l'échelle de texte [textScale].
Future<void> pumpScreen(
  WidgetTester t,
  String screen, {
  Brightness brightness = Brightness.light,
  Size size = const Size(412, 869),
  double textScale = 1.0,
}) async {
  t.view.physicalSize = size;
  t.view.devicePixelRatio = 1.0;
  t.platformDispatcher.platformBrightnessTestValue = brightness;
  t.platformDispatcher.textScaleFactorTestValue = textScale;
  addTearDown(() {
    t.view.reset();
    t.platformDispatcher.clearPlatformBrightnessTestValue();
    t.platformDispatcher.clearTextScaleFactorTestValue();
  });

  final api = demoApi();
  if (screen == 'explorerGrid' || screen == 'filterGrid') {
    api.viewPrefs = ViewPrefsMsg(sort: 'name', ascending: true, grid: true, showHidden: false, builtInViewers: true);
  }
  if (screen == 'welcome') api.uiPrefs = UiPrefsMsg(theme: 'system', haptics: true, welcomeSeen: false);
  final state = AppState(api);
  await state.loadUiPrefs();
  await state.refresh();
  harnessApi = api;
  harnessState = state;
  if (screen == 'nowPlaying') {
    state.onPlayerChanged(
      PlayerMsg(sourceId: 'v1', title: 'Morceau 2.mp3', index: 1, count: 6, playing: true, positionMs: 83000, durationMs: 245000),
      null,
    );
  }
  await t.pumpWidget(const SizedBox.shrink()); // arbre neuf : rien ne reste d'un écran précédent
  await t.pumpWidget(PortailRoot(state: state, builder: (_) => FluLuksApp(state: state)));
  await t.pumpAndSettle();

  final Widget? page = switch (screen) {
    'home' || 'welcome' => null,
    'explorerList' || 'explorerGrid' => ExplorerScreen(state: state, sourceId: 'phone'),
    'filterGrid' => FilterScreen(state: state, filter: 'photos', title: 'Photos'),
    'search' => SearchScreen(state: state, sourceId: 'phone', path: '/'),
    'globalSearch' => GlobalSearchScreen(state: state, initialQuery: 'IMG'),
    'trash' => TrashScreen(state: state, sourceId: 'phone'),
    'space' => SpaceScreen(state: state, sourceId: 'phone'),
    'settings' => SettingsScreen(state: state),
    'about' => AboutScreen(state: state),
    'help' => const HelpScreen(),
    'format' => FormatScreen(state: state, disk: api.disks.single),
    'editor' => TextEditorScreen(state: state, sourceId: 'phone', path: '/notes-1.txt', name: 'notes-1.txt'),
    'serverForm' => ServerFormScreen(state: state, server: api.servers.single),
    'nowPlaying' => NowPlayingScreen(state: state),
    'unlock' => null,
    _ => throw ArgumentError.value(screen, 'screen'),
  };
  final nav = t.state<NavigatorState>(find.byType(Navigator).first);
  if (page != null) {
    nav.push(MaterialPageRoute<void>(builder: (_) => page));
  } else if (screen == 'unlock') {
    showDialog<void>(
      context: nav.context,
      builder: (_) => UnlockDialog(state: state, diskId: 'd1', partIndex: 2, partitionName: 'Archives'),
    );
  }
  await t.pumpAndSettle();
}

/// Attend le décodage des images affichées (logo…) : sans cela, la première capture peut les montrer vides.
Future<void> waitForImages(WidgetTester t) async {
  await t.runAsync(() async {
    for (final e in find.byType(Image).evaluate()) {
      await precacheImage((e.widget as Image).image, e);
    }
  });
  await t.pumpAndSettle();
}
