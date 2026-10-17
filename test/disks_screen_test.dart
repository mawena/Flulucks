import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/strings_fr.dart';

import 'fake_host_api.dart';

Future<(AppState, FakeHostApi)> pump(WidgetTester tester, FakeHostApi api) async {
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  return (state, api);
}

void main() {
  testWidgets('liste vide', (tester) async {
    await pump(tester, FakeHostApi());
    expect(find.text('FluLuks'), findsOneWidget);
    expect(find.text('Branchez un disque USB-C'), findsOneWidget);
  });

  testWidgets('disque LUKS2 : bouton Déverrouiller', (tester) async {
    await pump(tester, FakeHostApi()..disks = [disk([part()])]);
    expect(find.text('Partition 1 · 2,0 Go · LUKS2'), findsOneWidget);
    expect(find.text('Déverrouiller'), findsOneWidget);
  });

  testWidgets('NTFS non pris en charge, sans bouton', (tester) async {
    await pump(
      tester,
      FakeHostApi()..disks = [disk([part(content: 'NTFS', supported: false, encrypted: false)])],
    );
    expect(find.text('Non pris en charge (NTFS)'), findsOneWidget);
    expect(find.byType(FilledButton), findsNothing);
  });

  testWidgets('volume ouvert : Fichiers et Éjecter, jamais Déverrouiller', (tester) async {
    final api = FakeHostApi()
      ..disks = [disk([part(volumeId: 'v1')])]
      ..volumes = [volume()];
    await pump(tester, api);
    expect(find.text("Ouvert — visible dans l'application Fichiers"), findsOneWidget);
    expect(find.text('Ouvrir dans Fichiers'), findsOneWidget);
    expect(find.text('Déverrouiller'), findsNothing);
    await tester.tap(find.text('Éjecter'));
    await tester.pump();
    expect(api.ejected, ['v1']);
  });

  test('taille lisible', () {
    expect(Fr.humanSize(1500000000), '1,5 Go');
    expect(Fr.humanSize(250000000), '250,0 Mo');
  });
}
