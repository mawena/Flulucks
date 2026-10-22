import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/strings_fr.dart';

import 'fake_host_api.dart';

Future<(AppState, FakeHostApi)> pumpHome(WidgetTester tester, FakeHostApi api) async {
  tester.view.physicalSize = const Size(412, 869); // largeur du Pixel 4 XL : pas de débordement
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  return (state, api);
}

/// Ouvre « Formater… » depuis le menu de la carte du disque.
Future<void> openFormat(WidgetTester tester) async {
  await tester.tap(find.byTooltip(Fr.diskMenu));
  await tester.pumpAndSettle();
  await tester.tap(find.text(Fr.formatMenu));
  await tester.pumpAndSettle();
}

FakeHostApi apiWithDisk() => FakeHostApi()..disks = [disk([part()])];

void main() {
  testWidgets('defaultsToExfat', (tester) async {
    await pumpHome(tester, apiWithDisk());
    await openFormat(tester);
    expect(find.text(Fr.formatTitle), findsOneWidget);
    expect(tester.widget<RadioGroup<String>>(find.byType(RadioGroup<String>)).groupValue, 'exfat');
    expect(find.text(Fr.fsHelpExfat), findsOneWidget);
    expect(find.text(Fr.fsHelpFat32), findsOneWidget);
    expect(find.text(Fr.fsHelpExt4), findsOneWidget);
    expect(find.text(Fr.fsHelpNtfs), findsOneWidget);
    expect(tester.widget<SwitchListTile>(find.byType(SwitchListTile)).value, isFalse);
    expect(tester.takeException(), isNull);
  });

  testWidgets('emptyOrMismatchedPassphraseRefused', (tester) async {
    final (_, api) = await pumpHome(tester, apiWithDisk());
    await openFormat(tester);
    await tester.tap(find.byType(SwitchListTile));
    await tester.pumpAndSettle();
    await tester.ensureVisible(find.widgetWithText(FilledButton, Fr.formatButton));
    await tester.tap(find.widgetWithText(FilledButton, Fr.formatButton));
    await tester.pumpAndSettle();
    expect(find.text(Fr.passEmpty), findsOneWidget);
    expect(find.byType(AlertDialog), findsNothing);
    await tester.enterText(find.widgetWithText(TextField, Fr.passphrase), 'secret');
    await tester.enterText(find.widgetWithText(TextField, Fr.passConfirm), 'secreT');
    await tester.tap(find.widgetWithText(FilledButton, Fr.formatButton));
    await tester.pumpAndSettle();
    expect(find.text(Fr.passMismatch), findsOneWidget);
    expect(find.byType(AlertDialog), findsNothing);
    expect(api.formats, isEmpty);
  });

  testWidgets('confirmRequiresExactDiskName', (tester) async {
    final (_, api) = await pumpHome(tester, apiWithDisk());
    await openFormat(tester);
    await tester.ensureVisible(find.widgetWithText(FilledButton, Fr.formatButton));
    await tester.tap(find.widgetWithText(FilledButton, Fr.formatButton));
    await tester.pumpAndSettle();
    expect(find.text(Fr.formatConfirm('Disque USB', Fr.humanSize(4000000000))), findsOneWidget);
    final confirm = find.descendant(of: find.byType(AlertDialog), matching: find.widgetWithText(FilledButton, Fr.formatButton));
    expect(tester.widget<FilledButton>(confirm).onPressed, isNull);
    await tester.enterText(find.descendant(of: find.byType(AlertDialog), matching: find.byType(TextField)), 'disque usb');
    await tester.pump();
    expect(tester.widget<FilledButton>(confirm).onPressed, isNull);
    await tester.enterText(find.descendant(of: find.byType(AlertDialog), matching: find.byType(TextField)), 'Disque USB');
    await tester.pump();
    expect(tester.widget<FilledButton>(confirm).onPressed, isNotNull);
    await tester.tap(find.widgetWithText(TextButton, Fr.cancel));
    await tester.pumpAndSettle();
    expect(api.formats, isEmpty);
  });

  testWidgets('formatCallsApiWithOptions', (tester) async {
    final (state, api) = await pumpHome(tester, apiWithDisk());
    api.formatGate = Completer<void>();
    await openFormat(tester);
    await tester.tap(find.text('ext4'));
    await tester.pumpAndSettle();
    await tester.enterText(find.widgetWithText(TextField, Fr.formatLabel), 'Données');
    await tester.tap(find.byType(SwitchListTile));
    await tester.pumpAndSettle();
    await tester.enterText(find.widgetWithText(TextField, Fr.passphrase), 'secret');
    await tester.enterText(find.widgetWithText(TextField, Fr.passConfirm), 'secret');
    await tester.ensureVisible(find.widgetWithText(FilledButton, Fr.formatButton));
    await tester.tap(find.widgetWithText(FilledButton, Fr.formatButton));
    await tester.pumpAndSettle();
    await tester.enterText(find.descendant(of: find.byType(AlertDialog), matching: find.byType(TextField)), 'Disque USB');
    await tester.pump();
    await tester.tap(find.descendant(of: find.byType(AlertDialog), matching: find.widgetWithText(FilledButton, Fr.formatButton)));
    await tester.pump();
    expect(api.formats.single, ('d1', 'ext4', true, 'Données', 'secret'));
    state.onFormatProgress(42);
    await tester.pump();
    expect(find.text(Fr.formatting(42)), findsOneWidget);
    api.formatGate!.complete();
    state.onFormatProgress(null);
    await tester.pumpAndSettle();
    expect(api.formatPassRef!.every((b) => b == 0), isTrue); // phrase effacée après l'appel
    expect(find.text(Fr.formatTitle), findsNothing); // retour à l'accueil
    expect(find.text(Fr.formatted), findsOneWidget);
  });

  testWidgets('formatErrorShownOnScreen', (tester) async {
    final (_, api) = await pumpHome(tester, apiWithDisk());
    api.formatError = 'Disque débranché pendant le formatage : recommencez le formatage';
    await openFormat(tester);
    await tester.ensureVisible(find.widgetWithText(FilledButton, Fr.formatButton));
    await tester.tap(find.widgetWithText(FilledButton, Fr.formatButton));
    await tester.pumpAndSettle();
    await tester.enterText(find.descendant(of: find.byType(AlertDialog), matching: find.byType(TextField)), 'Disque USB');
    await tester.pump();
    await tester.tap(find.descendant(of: find.byType(AlertDialog), matching: find.widgetWithText(FilledButton, Fr.formatButton)));
    await tester.pumpAndSettle();
    expect(find.text(api.formatError!), findsOneWidget);
    expect(find.text(Fr.formatTitle), findsOneWidget); // on reste sur l'écran
  });

  testWidgets('menuDisabledWithOpenVolume', (tester) async {
    final api = FakeHostApi()
      ..disks = [disk([part(volumeId: 'v1')])]
      ..volumes = [volume()];
    await pumpHome(tester, api);
    await tester.tap(find.byTooltip(Fr.diskMenu));
    await tester.pumpAndSettle();
    final item = tester.widget<PopupMenuItem<String>>(find.ancestor(
        of: find.text(Fr.formatMenu), matching: find.byWidgetPredicate((w) => w is PopupMenuItem<String>)));
    expect(item.enabled, isFalse);
    expect(find.text(Fr.formatEjectFirst), findsOneWidget);
  });

  testWidgets('labelLimitShown', (tester) async {
    final (_, api) = await pumpHome(tester, apiWithDisk());
    await openFormat(tester);
    expect(find.text(Fr.labelLimitChars(11)), findsOneWidget);
    await tester.tap(find.text('ext4'));
    await tester.pumpAndSettle();
    expect(find.text(Fr.labelLimitBytes(16)), findsOneWidget);
    await tester.tap(find.text('NTFS'));
    await tester.pumpAndSettle();
    expect(find.text(Fr.labelLimitChars(32)), findsOneWidget);
    await tester.tap(find.text('FAT32'));
    await tester.pumpAndSettle();
    expect(find.text(Fr.labelLimitFat32), findsOneWidget);
    await tester.enterText(find.widgetWithText(TextField, Fr.formatLabel), 'clé');
    await tester.pump();
    expect(find.text(Fr.labelInvalid), findsOneWidget);
    await tester.ensureVisible(find.widgetWithText(FilledButton, Fr.formatButton));
    await tester.tap(find.widgetWithText(FilledButton, Fr.formatButton));
    await tester.pumpAndSettle();
    expect(find.byType(AlertDialog), findsNothing);
    expect(api.formats, isEmpty);
    expect(tester.takeException(), isNull);
  });
}
