@Tags(['golden'])
library;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'screen_harness.dart';

/// Phase 11, T11 : rendu de référence de six écrans à 412 × 869 (Linux, police de test de Flutter : les textes
/// sont des pavés, la mise en page, les couleurs et les icônes sont comparées). Après un changement voulu :
/// `flutter test --update-goldens test/goldens_test.dart`, puis regarder chaque image avant de la garder.
void main() {
  const cases = {
    'home_light': ('home', Brightness.light),
    'home_dark': ('home', Brightness.dark),
    'explorer_grid_dark': ('explorerGrid', Brightness.dark),
    'unlock_light': ('unlock', Brightness.light),
    'settings_light': ('settings', Brightness.light),
    'welcome_light': ('welcome', Brightness.light),
  };
  for (final MapEntry(key: name, value: (screen, b)) in cases.entries) {
    testWidgets(name, (t) async {
      await pumpScreen(t, screen, brightness: b);
      await waitForImages(t);
      await expectLater(find.byType(MaterialApp), matchesGoldenFile('goldens/$name.png'));
    });
  }
}
