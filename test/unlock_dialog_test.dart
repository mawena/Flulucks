import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/state/app_state.dart';

import 'fake_host_api.dart';

Future<FakeHostApi> openDialog(WidgetTester tester, {String correct = 'bonne'}) async {
  final api = FakeHostApi()
    ..correctPassphrase = correct
    ..disks = [disk([part()])];
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  await tester.tap(find.text('Déverrouiller'));
  await tester.pumpAndSettle();
  return api;
}

Finder get field => find.byType(TextField);
Finder get submit =>
    find.descendant(of: find.byType(AlertDialog), matching: find.widgetWithText(FilledButton, 'Déverrouiller'));

void main() {
  testWidgets('champ masqué', (tester) async {
    await openDialog(tester);
    final tf = tester.widget<TextField>(field);
    expect(tf.obscureText, isTrue);
    expect(tf.autocorrect, isFalse);
    expect(tf.enableSuggestions, isFalse);
  });

  testWidgets('mauvaise phrase : message, champ vide, bouton réactivé', (tester) async {
    await openDialog(tester);
    await tester.enterText(field, 'mauvaise');
    await tester.tap(submit);
    await tester.pumpAndSettle();
    expect(find.text('phrase de passe incorrecte'), findsOneWidget);
    expect(tester.widget<TextField>(field).controller!.text, isEmpty);
    expect(tester.widget<FilledButton>(submit).onPressed, isNotNull);
  });

  testWidgets('échec : tampon aussi mis à zéro', (tester) async {
    final api = await openDialog(tester);
    await tester.enterText(field, 'mauvaise');
    await tester.tap(submit);
    await tester.pumpAndSettle();
    expect(api.passphraseCopy, isNotEmpty);
    expect(api.passphraseRef!.every((b) => b == 0), isTrue);
  });

  testWidgets('octets UTF-8 envoyés puis tampon mis à zéro', (tester) async {
    const phrase = 'mot de passe été ☀';
    final api = await openDialog(tester, correct: phrase);
    await tester.enterText(field, phrase);
    await tester.tap(submit);
    await tester.pumpAndSettle();
    expect(api.passphraseCopy, [
      109, 111, 116, 32, 100, 101, 32, 112, 97, 115, 115, 101, 32, 195, 169, 116, 195, 169, 32, 226, 152, 128,
    ]);
    expect(api.passphraseRef!.every((b) => b == 0), isTrue);
    expect(field, findsNothing); // dialogue fermé après succès
    expect(api.unlockWritable, [true]); // écriture demandée par défaut
  });

  testWidgets('pendant l\'appel : indicateur et bouton désactivé', (tester) async {
    final api = await openDialog(tester);
    api.gate = Completer<void>();
    await tester.enterText(field, 'bonne');
    await tester.tap(submit);
    await tester.pump();
    expect(find.byType(CircularProgressIndicator), findsOneWidget);
    expect(find.text('Déverrouillage en cours… cela peut prendre quelques secondes'), findsOneWidget);
    expect(tester.widget<FilledButton>(submit).onPressed, isNull);
    api.gate!.complete();
    await tester.pumpAndSettle();
    expect(find.byType(CircularProgressIndicator), findsNothing);
  });
}
