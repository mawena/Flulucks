import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

import '../test/screen_harness.dart';

/// Phase 11, T12 : captures de chaque écran sur le Pixel 4 XL (données de démonstration, aucun disque touché) ; la
/// fluidité est mesurée par `scroll_test.dart`. Lancer avec
/// `flutter drive --profile --no-dds --keep-app-running --driver=test_driver/integration_test.dart
/// --target=integration_test/ui_tour_test.dart` (jamais sans `--keep-app-running` : `flutter drive` désinstalle
/// l'application, et ses données, à la fin).
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
}
