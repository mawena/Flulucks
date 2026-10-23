import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/strings_fr.dart';
import 'package:fluluks/src/ui/dialogs.dart';
import 'package:fluluks/src/ui/feedback.dart';
import 'package:fluluks/src/ui/sheets.dart';
import 'package:fluluks/src/ui/states.dart';
import 'package:fluluks/src/ui/theme.dart';
import 'package:fluluks/src/ui/tokens.dart';
import 'package:fluluks/src/ui/widgets.dart';

import 'fake_host_api.dart';

/// [child] dans une application au thème de Portail ; [disableAnimations] : animations coupées par Android.
Future<void> host(WidgetTester tester, Widget child, {bool disableAnimations = false}) async {
  await tester.pumpWidget(MaterialApp(
    theme: portailTheme(Brightness.light),
    home: MediaQuery(
      data: MediaQueryData(disableAnimations: disableAnimations),
      child: Scaffold(body: child),
    ),
  ));
}

/// Résultat attendu d'un dialogue ou d'une feuille encore ouverts.
class Pending<T> {
  Pending(this.result);
  final Future<T?> result;
}

/// Bouton qui lance [action] avec un contexte sous le Scaffold, touché ; le dialogue est alors affiché.
Future<Pending<T>> launch<T>(WidgetTester tester, Future<T?> Function(BuildContext) action) async {
  late Future<T?> result;
  await host(tester, Builder(builder: (c) => TextButton(onPressed: () => result = action(c), child: const Text('go'))));
  await tester.tap(find.text('go'));
  await tester.pumpAndSettle();
  return Pending(result);
}

void main() {
  testWidgets('emptyStateShowsTitleMessageAction', (tester) async {
    var taps = 0;
    await host(
      tester,
      EmptyState(
        icon: Icons.usb_rounded,
        title: Fr.noDiskTitle,
        message: Fr.noDiskBody,
        action: FilledButton(onPressed: () => taps++, child: const Text('Action')),
      ),
    );
    expect(find.text(Fr.noDiskTitle), findsOneWidget);
    expect(find.text(Fr.noDiskBody), findsOneWidget);
    expect(find.byIcon(Icons.usb_rounded), findsOneWidget);
    await tester.tap(find.text('Action'));
    expect(taps, 1);
  });

  testWidgets('errorStateRetryCalls', (tester) async {
    var retries = 0;
    await host(tester, ErrorState(message: 'Disque illisible', onRetry: () => retries++));
    expect(find.text('Disque illisible'), findsOneWidget);
    await tester.tap(find.text(Fr.retry));
    expect(retries, 1);
    await host(tester, const ErrorState(message: 'Sans action'));
    expect(find.text(Fr.retry), findsNothing);
  });

  testWidgets('delayedProgressHiddenFirst200ms', (tester) async {
    await host(tester, const DelayedProgress(label: Fr.analyzing));
    await tester.pump(const Duration(milliseconds: 199));
    expect(find.byType(CircularProgressIndicator), findsNothing);
    expect(find.text(Fr.analyzing), findsNothing);
    await tester.pump(const Duration(milliseconds: 1));
    expect(find.byType(CircularProgressIndicator), findsOneWidget);
    expect(find.text(Fr.analyzing), findsOneWidget);
  });

  testWidgets('confirmDestructiveUsesErrorColor', (tester) async {
    var answer = await launch(tester, (c) => confirmAction(c,
        title: Fr.emptyTrashTitle, body: Fr.emptyTrashBody(2), confirmLabel: Fr.emptyAction, destructive: true));
    final button = tester.widget<FilledButton>(find.widgetWithText(FilledButton, Fr.emptyAction));
    final scheme = portailScheme(Brightness.light);
    expect(button.style!.backgroundColor!.resolve({}), scheme.error);
    expect(button.style!.foregroundColor!.resolve({}), scheme.onError);
    expect(find.text(Fr.emptyTrashBody(2)), findsOneWidget);
    await tester.tap(find.text(Fr.emptyAction));
    await tester.pumpAndSettle();
    expect(await answer.result, isTrue);

    answer = await launch(tester, (c) => confirmAction(c, title: Fr.emptyTrashTitle, confirmLabel: Fr.emptyAction));
    await tester.tap(find.text(Fr.cancel));
    await tester.pumpAndSettle();
    expect(await answer.result, isFalse);
  });

  testWidgets('promptSelectsBaseName', (tester) async {
    final answer = await launch(tester,
        (c) => promptText(c, title: Fr.rename, label: Fr.newName, initial: 'rapport.pdf', selectBaseName: true));
    final field = tester.widget<TextField>(find.byType(TextField));
    expect(field.controller!.selection, const TextSelection(baseOffset: 0, extentOffset: 7));
    await tester.enterText(find.byType(TextField), 'bilan.pdf');
    await tester.tap(find.text(Fr.ok));
    await tester.pumpAndSettle();
    expect(await answer.result, 'bilan.pdf');
  });

  testWidgets('promptValidatorDisablesOk', (tester) async {
    final answer =
        await launch(tester, (c) => promptText(c, title: Fr.newFolder, label: Fr.folderName, validator: nameError));
    FilledButton ok() => tester.widget<FilledButton>(find.widgetWithText(FilledButton, Fr.ok));
    expect(ok().onPressed, isNull); // vide
    for (final (text, error) in [
      ('a/b', Fr.nameSlash),
      ('..', Fr.nameReserved),
      ('.', Fr.nameReserved),
      ('é' * 128, Fr.nameTooLong), // 256 octets en UTF-8
      ('   ', Fr.nameEmpty),
    ]) {
      await tester.enterText(find.byType(TextField), text);
      await tester.pump();
      expect(ok().onPressed, isNull, reason: text);
      expect(find.text(error), findsOneWidget, reason: text);
    }
    // Entrée sur un nom refusé : rien n'est rendu
    await tester.testTextInput.receiveAction(TextInputAction.done);
    await tester.pumpAndSettle();
    expect(find.byType(AlertDialog), findsOneWidget);
    await tester.enterText(find.byType(TextField), 'Vacances');
    await tester.pump();
    expect(ok().onPressed, isNotNull);
    await tester.tap(find.text(Fr.ok));
    await tester.pumpAndSettle();
    expect(await answer.result, 'Vacances');
    expect(nameError('é' * 127), isNull); // 254 octets
    expect(nameError('a\u0000b'), Fr.nameBadChar);
  });

  testWidgets('actionSheetReturnsValueAndRespectsEnabled', (tester) async {
    var answer = await launch(tester, (c) => showActionSheet<String>(c, title: 'photo.jpg', actions: const [
          SheetAction('share', Icons.share_rounded, Fr.share),
          SheetAction('delete', Icons.delete_rounded, Fr.delete, enabled: false, destructive: true),
        ]));
    expect(find.text('photo.jpg'), findsOneWidget);
    await tester.tap(find.text(Fr.delete)); // désactivé : la feuille reste
    await tester.pumpAndSettle();
    expect(find.text(Fr.share), findsOneWidget);
    await tester.tap(find.text(Fr.share));
    await tester.pumpAndSettle();
    expect(await answer.result, 'share');

    answer = await launch(tester, (c) => showActionSheet<String>(c, actions: const [
          SheetAction('delete', Icons.delete_rounded, Fr.delete, destructive: true, subtitle: 'Définitif'),
        ]));
    final text = tester.widget<Text>(find.text(Fr.delete));
    final tile = tester.widget<ListTile>(find.widgetWithText(ListTile, Fr.delete));
    expect(tile.textColor ?? text.style?.color, portailScheme(Brightness.light).error);
    expect(find.text('Définitif'), findsOneWidget);
    await tester.tap(find.text(Fr.delete));
    await tester.pumpAndSettle();
    expect(await answer.result, 'delete');
  });

  testWidgets('messageKindIcons', (tester) async {
    var undone = 0;
    await launch<void>(tester, (c) async {
      showMessage(c, Fr.saved, kind: MessageKind.success);
    });
    expect(find.text(Fr.saved), findsOneWidget);
    expect(find.byIcon(Icons.check_circle_rounded), findsOneWidget);
    expect(tester.widget<SnackBar>(find.byType(SnackBar)).behavior ?? SnackBarBehavior.floating,
        SnackBarBehavior.floating);

    await launch<void>(tester, (c) async {
      showMessage(c, Fr.genericError, kind: MessageKind.error, actionLabel: Fr.undo, onAction: () => undone++);
    });
    expect(find.byIcon(Icons.error_rounded), findsOneWidget);
    expect(find.text(Fr.saved), findsNothing); // le message précédent est remplacé, pas mis en file
    await tester.tap(find.text(Fr.undo));
    await tester.pumpAndSettle();
    expect(undone, 1);
  });

  testWidgets('actionMessageGoesAway', (tester) async {
    await launch<void>(tester, (c) async {
      showMessage(c, Fr.movedToTrash(1), actionLabel: Fr.undo, onAction: () {});
    });
    expect(find.text(Fr.movedToTrash(1)), findsOneWidget);
    await tester.pump(const Duration(seconds: 11));
    await tester.pumpAndSettle();
    expect(find.text(Fr.movedToTrash(1)), findsNothing); // ne suit pas l'utilisateur sur les autres écrans
  });

  testWidgets('hapticsHonourPreference', (tester) async {
    final calls = <String?>[];
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(SystemChannels.platform, (call) async {
      if (call.method == 'HapticFeedback.vibrate') calls.add(call.arguments as String?);
      return null;
    });
    addTearDown(() => tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(SystemChannels.platform, null));
    final api = FakeHostApi();
    final state = AppState(api);
    await state.refresh();
    await AppHaptics.selection(state);
    await AppHaptics.success(state);
    await AppHaptics.warning(state);
    expect(calls, ['HapticFeedbackType.selectionClick', 'HapticFeedbackType.lightImpact', 'HapticFeedbackType.mediumImpact']);

    calls.clear();
    await state.setUiPrefs(UiPrefsMsg(theme: 'system', haptics: false, welcomeSeen: true));
    await AppHaptics.selection(state);
    await AppHaptics.success(state);
    await AppHaptics.warning(state);
    expect(calls, isEmpty);
  });

  testWidgets('usageBarSemanticsAndDanger', (tester) async {
    final semantics = tester.ensureSemantics();
    final scheme = portailScheme(Brightness.light);
    await host(tester, const UsageBar(used: 0.92));
    expect(tester.getSemantics(find.byType(UsageBar)), isSemantics(value: Fr.usedPercent(92)));
    expect(tester.widget<LinearProgressIndicator>(find.byType(LinearProgressIndicator)).color, scheme.error);
    await host(tester, const UsageBar(used: 0.75));
    expect(tester.getSemantics(find.byType(UsageBar)), isSemantics(value: '75 % utilisés'));
    expect(tester.widget<LinearProgressIndicator>(find.byType(LinearProgressIndicator)).color, scheme.primary);
    semantics.dispose();
  });

  testWidgets('stateSwitcherNoAnimationWhenDisabled', (tester) async {
    await host(tester, const StateSwitcher(child: Text('contenu')));
    expect(tester.widget<AnimatedSwitcher>(find.byType(AnimatedSwitcher)).duration, Motion.medium);
    await host(tester, const StateSwitcher(child: Text('contenu')), disableAnimations: true);
    expect(tester.widget<AnimatedSwitcher>(find.byType(AnimatedSwitcher)).duration, Duration.zero);
  });

  testWidgets('chipsBannersHeadersAndAdaptiveBody', (tester) async {
    final scheme = portailScheme(Brightness.light);
    tester.view.physicalSize = const Size(1280, 800);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await host(
      tester,
      AdaptiveBody(
        child: ListView(children: [
          SectionHeader(Fr.usbDisks, trailing: IconButton(onPressed: () {}, icon: const Icon(Icons.add_rounded))),
          const StatusChip(label: Fr.readOnlySwitch, icon: Icons.lock_rounded, tone: ChipTone.warning),
          StatusBanner(
            icon: Icons.warning_rounded,
            text: Fr.ftpClear,
            tone: ChipTone.danger,
            actions: [TextButton(onPressed: () {}, child: const Text(Fr.ok))],
          ),
        ]),
      ),
    );
    expect(tester.getSize(find.byType(ListView)).width, 840);
    final header = tester.widget<Text>(find.text(Fr.usbDisks));
    expect(header.style!.color, scheme.primary);
    final chip = tester.widget<DecoratedBox>(
        find.ancestor(of: find.text(Fr.readOnlySwitch), matching: find.byType(DecoratedBox)).first);
    expect((chip.decoration as BoxDecoration).color, scheme.tertiaryContainer);
    final banner = tester.widget<DecoratedBox>(
        find.ancestor(of: find.text(Fr.ftpClear), matching: find.byType(DecoratedBox)).first);
    expect((banner.decoration as BoxDecoration).color, scheme.errorContainer);
    expect(find.text(Fr.ok), findsOneWidget);
  });
}
