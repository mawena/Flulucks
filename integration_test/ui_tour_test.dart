import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:integration_test/integration_test.dart';

import '../test/screen_harness.dart';

/// Phase 11, T12 : captures de chaque écran sur le Pixel 4 XL (données de démonstration, aucun disque touché) et
/// fluidité du défilement d'un dossier de 2 000 éléments. Lancer avec
/// `flutter drive --profile --driver=test_driver/integration_test.dart --target=integration_test/ui_tour_test.dart`.
void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  Future<void> shot(WidgetTester t, String name) async {
    await waitForImages(t);
    await binding.takeScreenshot(name);
  }

  testWidgets('captureTour', (t) async {
    await binding.convertFlutterSurfaceToImage();
    for (final screen in screens) {
      for (final b in Brightness.values) {
        await pumpScreen(t, screen, brightness: b, deviceView: true);
        await shot(t, '${screen}_${b.name}');
      }
    }
    for (final screen in ['home', 'explorerList', 'unlock']) {
      await pumpScreen(t, screen, textScale: 2.0, deviceView: true);
      await shot(t, '${screen}_200');
    }
    await SystemChrome.setPreferredOrientations([DeviceOrientation.landscapeLeft]);
    await t.pumpAndSettle(const Duration(seconds: 1));
    for (final screen in ['home', 'explorerGrid', 'unlock']) {
      await pumpScreen(t, screen, deviceView: true);
      await shot(t, '${screen}_paysage');
    }
    await SystemChrome.setPreferredOrientations([]);
    await t.pumpAndSettle(const Duration(seconds: 1));
  });

  testWidgets('scroll2000EntriesSmooth', (t) async {
    for (final grid in [false, true]) {
      await pumpScreen(t, 'home', deviceView: true);
      final big = [
        for (var i = 0; i < 2000; i++)
          EntryMsg(
            name: i % 5 == 0 ? 'Dossier $i' : 'Fichier $i.${['jpg', 'mp3', 'pdf', 'txt'][i % 4]}',
            path: '/Gros/$i',
            type: i % 5 == 0 ? 'dir' : 'file',
            size: 1000 * i,
            mtime: 1760000000 - i,
          ),
      ];
      harnessApi.dirs['phone']!['/Gros'] = big;
      await harnessState.setViewPrefs(
          ViewPrefsMsg(sort: 'name', ascending: true, grid: grid, showHidden: false, builtInViewers: true));
      t.state<NavigatorState>(find.byType(Navigator).first).push(MaterialPageRoute<void>(
          builder: (_) => ExplorerScreen(state: harnessState, sourceId: 'phone', path: '/Gros')));
      await t.pumpAndSettle();
      final key = grid ? 'grille' : 'liste';
      await binding.watchPerformance(() async {
        final list = find.byType(Scrollable).first;
        for (var i = 0; i < 12; i++) {
          await t.fling(list, const Offset(0, -1500), 4000);
          await t.pumpAndSettle();
        }
      }, reportKey: key);
    }
    // les deux mesures d'abord (rapport complet dans fluidite.txt), puis les seuils
    for (final key in ['liste', 'grille']) {
      // ignore: avoid_print
      print('MESURE $key ${binding.reportData![key]}');
    }
    for (final key in ['liste', 'grille']) {
      final r = binding.reportData![key] as Map<String, dynamic>;
      expect(r['90th_percentile_frame_build_time_millis'] as num, lessThanOrEqualTo(8), reason: '$key : $r');
      expect(r['90th_percentile_frame_rasterizer_time_millis'] as num, lessThanOrEqualTo(16), reason: '$key : $r');
    }
  });
}
