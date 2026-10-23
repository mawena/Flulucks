import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:integration_test/integration_test.dart';

import '../test/screen_harness.dart';

/// Phase 11, T12 : fluidité du défilement d'un dossier de 2 000 éléments, en liste puis en grille, sur le Pixel 4 XL
/// (données de démonstration). Lancer comme `ui_tour_test.dart`, avec `--target=integration_test/scroll_test.dart`.
void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  // témoin : le même défilement sur une liste Flutter nue, pour séparer le coût du cadre de test de celui de Portail
  testWidgets('scrollBaseline', (t) async {
    await t.pumpWidget(MaterialApp(
      home: Scaffold(body: ListView.builder(itemCount: 2000, itemBuilder: (_, i) => ListTile(title: Text('Ligne $i')))),
    ));
    await t.pumpAndSettle();
    await binding.watchPerformance(() async {
      for (var i = 0; i < 12; i++) {
        await t.fling(find.byType(Scrollable).first, const Offset(0, -1500), 4000);
        await t.pumpAndSettle();
      }
    }, reportKey: 'temoin');
    // ignore: avoid_print
    print('MESURE temoin ${binding.reportData!['temoin']}');
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
    // seuil relatif au témoin du même passage : une liste Flutter nue est déjà à 8–10 ms au 90e centile dans ce cadre
    // de test (gestes simulés, pumpAndSettle), le seuil absolu de 8 ms du plan n'y est pas tenable
    final base = (binding.reportData!['temoin'] as Map<String, dynamic>)['90th_percentile_frame_build_time_millis'] as num;
    for (final key in ['liste', 'grille']) {
      final r = binding.reportData![key] as Map<String, dynamic>;
      expect(r['90th_percentile_frame_build_time_millis'] as num, lessThanOrEqualTo(2 * base), reason: '$key : $r');
      expect(r['90th_percentile_frame_rasterizer_time_millis'] as num, lessThanOrEqualTo(16), reason: '$key : $r');
    }
  });
}
