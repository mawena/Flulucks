import 'dart:io';

import 'package:integration_test/integration_test_driver_extended.dart';

/// Pilote de `integration_test/ui_tour_test.dart` : écrit les captures dans le dossier des essais de la phase 11
/// et le rapport de fluidité à côté.
const _dir = 'docs/superpowers/research/2026-10-23-essais-phase11/captures';

Future<void> main() => integrationDriver(
      onScreenshot: (name, bytes, [args]) async {
        final f = File('$_dir/$name.png');
        await f.parent.create(recursive: true);
        await f.writeAsBytes(bytes);
        return true;
      },
      responseDataCallback: (data) async {
        if (data == null) return;
        final f = File('$_dir/../fluidite.txt');
        await f.parent.create(recursive: true);
        await f.writeAsString([
          for (final e in data.entries) '${e.key} : ${e.value}',
        ].join('\n'));
      },
    );
