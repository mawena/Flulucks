import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/screens/about_screen.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:fluluks/src/screens/filter_screen.dart';
import 'package:fluluks/src/screens/format_screen.dart';
import 'package:fluluks/src/screens/global_search_screen.dart';
import 'package:fluluks/src/screens/help_screen.dart';
import 'package:fluluks/src/screens/home_screen.dart';
import 'package:fluluks/src/screens/now_playing_screen.dart';
import 'package:fluluks/src/screens/search_screen.dart';
import 'package:fluluks/src/screens/server_form_screen.dart';
import 'package:fluluks/src/screens/settings_screen.dart';
import 'package:fluluks/src/screens/space_screen.dart';
import 'package:fluluks/src/screens/text_editor_screen.dart';
import 'package:fluluks/src/screens/trash_screen.dart';
import 'package:fluluks/src/screens/welcome_screen.dart';
import 'package:fluluks/src/widgets/unlock_dialog.dart';

import 'screen_harness.dart';

/// Phase 11, T11 : chaque écran, en clair et en sombre, respecte les règles d'accessibilité d'Android (cibles de
/// 48 dp, cibles nommées pour TalkBack, contraste AA du texte).
void main() {
  const shown = {
    'home': HomeScreen, 'explorerList': ExplorerScreen, 'explorerGrid': ExplorerScreen, 'filterGrid': FilterScreen, //
    'search': SearchScreen, 'globalSearch': GlobalSearchScreen, 'trash': TrashScreen, 'space': SpaceScreen,
    'settings': SettingsScreen, 'about': AboutScreen, 'help': HelpScreen, 'welcome': WelcomeScreen,
    'unlock': UnlockDialog, 'format': FormatScreen, 'editor': TextEditorScreen, 'serverForm': ServerFormScreen,
    'nowPlaying': NowPlayingScreen,
  };
  testWidgets('harnessShowsEachScreen', (t) async {
    for (final screen in screens) {
      await pumpScreen(t, screen);
      expect(find.byType(shown[screen]!), findsOneWidget, reason: screen);
      expect(t.takeException(), isNull, reason: screen);
    }
    await pumpScreen(t, 'explorerGrid');
    expect(find.byType(SliverGrid), findsOneWidget);
    expect(find.text('IMG_20251001.jpg'), findsOneWidget);
  });

  for (final screen in screens) {
    for (final b in Brightness.values) {
      testWidgets('$screen ${b.name}', (t) async {
        final semantics = t.ensureSemantics();
        await pumpScreen(t, screen, brightness: b);
        await expectLater(t, meetsGuideline(androidTapTargetGuideline));
        await expectLater(t, meetsGuideline(labeledTapTargetGuideline));
        await expectLater(t, meetsGuideline(textContrastGuideline));
        semantics.dispose();
      });
    }
  }
}
