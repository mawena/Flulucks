import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/strings_fr.dart';

import 'screen_harness.dart';

/// Phase 11, T11 : aucun débordement à 200 % de texte, en portrait, en paysage et sur tablette ; les actions vitales
/// restent atteignables.
void main() {
  const sizes = [Size(412, 869), Size(869, 412), Size(1280, 800)];
  for (final screen in screens) {
    for (final size in sizes) {
      for (final scale in [1.0, 2.0]) {
        testWidgets('noOverflowAt200Percent $screen ${size.width.toInt()}x${size.height.toInt()} ×$scale', (t) async {
          await pumpScreen(t, screen, size: size, textScale: scale);
          expect(t.takeException(), isNull);
        });
      }
    }
  }

  group('vitalActionsReachable', () {
    Future<void> reachable(WidgetTester t, Finder f) async {
      expect(f, findsWidgets);
      await t.ensureVisible(f.first);
      await t.pumpAndSettle();
      final r = t.getRect(f.first);
      final screen = t.view.physicalSize / t.view.devicePixelRatio;
      expect(r.top >= 0 && r.bottom <= screen.height && r.left >= 0 && r.right <= screen.width, isTrue,
          reason: '$f hors de l\'écran : $r');
      t.hitTestOnBinding(r.center); // touchable
    }

    testWidgets('unlock', (t) async {
      await pumpScreen(t, 'unlock', textScale: 2.0);
      await reachable(t, find.text(Fr.unlock));
    });

    testWidgets('eject', (t) async {
      await pumpScreen(t, 'home', textScale: 2.0);
      await t.scrollUntilVisible(find.text(Fr.eject), 200, scrollable: find.byType(Scrollable).first);
      await reachable(t, find.text(Fr.eject));
    });

    testWidgets('cancelTransfer', (t) async {
      await pumpScreen(t, 'explorerList', textScale: 2.0);
      await harnessState.startTransfer('v1', ['/Sauvegarde.zip'], 'phone', '/', move: false);
      harnessState.onTransferChanged(TransferMsg(
          kind: 'copy', move: false, currentName: 'Sauvegarde.zip', fileIndex: 1, fileCount: 1, doneBytes: 50,
          totalBytes: 200, finished: false, cancelled: false, elapsedMs: 0));
      await t.pump();
      await reachable(t, find.widgetWithText(TextButton, Fr.cancel));
    });

    testWidgets('save', (t) async {
      await pumpScreen(t, 'editor', textScale: 2.0);
      await t.enterText(find.byType(TextField), 'modifié');
      await t.pump();
      await reachable(t, find.byTooltip(Fr.save));
      SystemChannels.textInput.invokeMethod<void>('TextInput.hide');
    });
  });
}
