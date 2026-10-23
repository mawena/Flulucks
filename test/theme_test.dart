import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/state/prefs.dart';
import 'package:fluluks/src/strings_fr.dart';
import 'package:fluluks/src/ui/root.dart';
import 'package:fluluks/src/ui/theme.dart';
import 'package:fluluks/src/ui/tokens.dart';

import 'fake_host_api.dart';

/// Rapport de contraste WCAG 2 entre deux couleurs opaques.
double contrastRatio(Color a, Color b) {
  final la = a.computeLuminance(), lb = b.computeLuminance();
  final hi = la > lb ? la : lb, lo = la > lb ? lb : la;
  return (hi + 0.05) / (lo + 0.05);
}

String hex(Color c) => '#${(c.toARGB32() & 0xFFFFFF).toRadixString(16).padLeft(6, '0').toUpperCase()}';

/// Valeur de `<color name="portail_fond">` d'un fichier `colors.xml`.
String androidColor(String file, String name) {
  final xml = File(file).readAsStringSync();
  final m = RegExp('<color name="$name">(#[0-9A-Fa-f]{6})</color>').firstMatch(xml);
  expect(m, isNotNull, reason: '$name absent de $file');
  return m!.group(1)!.toUpperCase();
}

Future<AppState> loaded(FakeHostApi api) async {
  final state = AppState(api);
  await state.refresh();
  return state;
}

ThemeMode modeOf(WidgetTester tester) => tester.widget<MaterialApp>(find.byType(MaterialApp)).themeMode!;

void main() {
  test('schemeContrastAA', () {
    for (final b in Brightness.values) {
      final s = portailScheme(b);
      final pairs = {
        'onPrimary/primary': (s.onPrimary, s.primary),
        'onSurface/surface': (s.onSurface, s.surface),
        'onSurfaceVariant/surface': (s.onSurfaceVariant, s.surface),
        'onSurfaceVariant/surfaceContainerLow': (s.onSurfaceVariant, s.surfaceContainerLow),
        'onSecondaryContainer/secondaryContainer': (s.onSecondaryContainer, s.secondaryContainer),
        'onTertiaryContainer/tertiaryContainer': (s.onTertiaryContainer, s.tertiaryContainer),
        'onErrorContainer/errorContainer': (s.onErrorContainer, s.errorContainer),
        'error/surface': (s.error, s.surface),
        'primary/surface': (s.primary, s.surface),
      };
      pairs.forEach((name, p) {
        expect(contrastRatio(p.$1, p.$2), greaterThanOrEqualTo(4.5), reason: '$name en $b');
      });
    }
  });

  test('primaryStaysTerracotta', () {
    final seed = HSLColor.fromColor(portailSeed).hue;
    final primary = HSLColor.fromColor(portailScheme(Brightness.light).primary).hue;
    expect((primary - seed).abs(), lessThanOrEqualTo(10));
  });

  test('themeModeOfPreference', () {
    expect(themeModeOf('light'), ThemeMode.light);
    expect(themeModeOf('dark'), ThemeMode.dark);
    expect(themeModeOf('system'), ThemeMode.system);
    expect(themeModeOf('violet'), ThemeMode.system);
  });

  test('themeTokensApplied', () {
    final t = portailTheme(Brightness.light, haptics: false);
    expect(t.colorScheme.primary, portailScheme(Brightness.light).primary);
    expect(t.cardTheme.elevation, 0);
    expect(t.cardTheme.color, t.colorScheme.surfaceContainerLow);
    expect(t.listTileTheme.enableFeedback, isFalse);
    expect(portailTheme(Brightness.light).listTileTheme.enableFeedback, isTrue);
    expect(t.bottomSheetTheme.showDragHandle, isTrue);
    expect(t.snackBarTheme.behavior, SnackBarBehavior.floating);
    expect(t.progressIndicatorTheme.linearMinHeight, 6);
    expect(t.appBarTheme.scrolledUnderElevation, 3);
    expect(t.filledButtonTheme.style!.minimumSize!.resolve({})!.height, greaterThanOrEqualTo(48));
    expect(t.textTheme.titleMedium!.fontWeight, FontWeight.w600);
    expect(t.pageTransitionsTheme.builders[TargetPlatform.android], isA<FadeForwardsPageTransitionsBuilder>());
    expect(portailTheme(Brightness.dark).colorScheme.brightness, Brightness.dark);
  });

  test('tokensValues', () {
    expect([Space.xs, Space.s, Space.m, Space.l, Space.xl, Space.xxl], [4, 8, 12, 16, 24, 32]);
    expect([Radii.s, Radii.m, Radii.l, Radii.xl], [8, 12, 16, 28]);
    expect(numeric(const TextStyle()).fontFeatures, contains(const FontFeature.tabularFigures()));
  });

  testWidgets('motionAndWindowClass', (tester) async {
    late Duration on, off;
    late WindowClass compact, medium, expanded;
    Widget probe(Size size, bool disable, void Function(BuildContext) read) => MediaQuery(
          data: MediaQueryData(size: size, disableAnimations: disable),
          child: Builder(builder: (c) {
            read(c);
            return const SizedBox();
          }),
        );
    await tester.pumpWidget(probe(const Size(412, 869), false, (c) {
      on = Motion.of(c, Motion.medium);
      compact = windowClassOf(c);
    }));
    await tester.pumpWidget(probe(const Size(700, 869), true, (c) {
      off = Motion.of(c, Motion.medium);
      medium = windowClassOf(c);
    }));
    await tester.pumpWidget(probe(const Size(1280, 800), false, (c) => expanded = windowClassOf(c)));
    expect(on, const Duration(milliseconds: 250));
    expect(off, Duration.zero);
    expect([compact, medium, expanded], [WindowClass.compact, WindowClass.medium, WindowClass.expanded]);
  });

  testWidgets('themeFollowsSystemByDefault', (tester) async {
    await tester.pumpWidget(FluLuksApp(state: await loaded(FakeHostApi())));
    expect(modeOf(tester), ThemeMode.system);
    final app = tester.widget<MaterialApp>(find.byType(MaterialApp));
    expect(app.theme!.colorScheme.primary, portailScheme(Brightness.light).primary);
    expect(app.darkTheme!.colorScheme.primary, portailScheme(Brightness.dark).primary);
  });

  testWidgets('themeChoiceApplied', (tester) async {
    final dark = FakeHostApi()..uiPrefs = UiPrefsMsg(theme: 'dark', haptics: true, welcomeSeen: true);
    await tester.pumpWidget(FluLuksApp(state: await loaded(dark)));
    expect(modeOf(tester), ThemeMode.dark);

    // dans les Réglages, « Sombre » est enregistré sans toucher aux autres préférences, et appliqué aussitôt
    final api = FakeHostApi()..uiPrefs = UiPrefsMsg(theme: 'system', haptics: false, welcomeSeen: true);
    final state = await loaded(api);
    await tester.pumpWidget(PortailRoot(state: state, builder: (_) => FluLuksApp(state: state)));
    await tester.tap(find.byTooltip(Fr.settings));
    await tester.pumpAndSettle();
    expect(find.text(Fr.theme), findsOneWidget);
    await tester.tap(find.text(Fr.themeDark));
    await tester.pumpAndSettle();
    expect([api.uiPrefs.theme, api.uiPrefs.haptics, api.uiPrefs.welcomeSeen], ['dark', false, true]);
    expect(modeOf(tester), ThemeMode.dark);
    expect(tester.widget<SegmentedButton<String>>(find.byType(SegmentedButton<String>)).selected, {'dark'});
  });

  testWidgets('textScaleClampedAt200Percent', (tester) async {
    tester.platformDispatcher.textScaleFactorTestValue = 3.0;
    addTearDown(tester.platformDispatcher.clearTextScaleFactorTestValue);
    final state = await loaded(FakeHostApi());
    await tester.pumpWidget(PortailRoot(state: state, builder: (_) => FluLuksApp(state: state)));
    final ctx = tester.element(find.byType(Scaffold).first); // pas le titre : AppBar le borne à 134 %
    expect(MediaQuery.textScalerOf(ctx).scale(10), 20);
  });

  test('androidColorsMatchTheme', () {
    expect(androidColor('android/app/src/main/res/values/colors.xml', 'portail_fond'),
        hex(portailScheme(Brightness.light).surface));
    expect(androidColor('android/app/src/main/res/values-night/colors.xml', 'portail_fond'),
        hex(portailScheme(Brightness.dark).surface));
    expect(androidColor('android/app/src/main/res/values/colors.xml', 'portail_terracotta'), hex(portailSeed));
  });

  test('copyWithKeepsOtherFields', () {
    final v = ViewPrefsMsg(sort: 'size', ascending: false, grid: true, showHidden: true, builtInViewers: false);
    final g = v.copyWith(grid: false);
    expect([g.sort, g.ascending, g.grid, g.showHidden, g.builtInViewers], ['size', false, false, true, false]);
    final w = v.copyWith(sort: 'date', ascending: true, showHidden: false, builtInViewers: true);
    expect([w.sort, w.ascending, w.grid, w.showHidden, w.builtInViewers], ['date', true, true, false, true]);

    final u = UiPrefsMsg(theme: 'light', haptics: false, welcomeSeen: true);
    final t = u.copyWith(theme: 'dark');
    expect([t.theme, t.haptics, t.welcomeSeen], ['dark', false, true]);
    final h = u.copyWith(haptics: true, welcomeSeen: false);
    expect([h.theme, h.haptics, h.welcomeSeen], ['light', true, false]);
  });
}
