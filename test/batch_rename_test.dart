import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/batch_rename_screen.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:fluluks/src/state/app_state.dart';

import 'fake_host_api.dart';

EntryMsg f(String path) => EntryMsg(name: path.split('/').last, path: path, type: 'file', size: 10, mtime: 1700000000);

SourceMsg src(String id) => SourceMsg(
    id: id, stableKey: id, kind: 'phone', label: 'Téléphone', totalBytes: 1000, freeBytes: 500, readOnly: false,
    roReason: '', encrypted: false);

String ext(String name) {
  final i = name.lastIndexOf('.');
  return i > 0 ? name.substring(i) : '';
}

/// Aperçu simulé : numérotation sur deux chiffres ; « Lot » entre en conflit avec un fichier existant.
List<RenamePreviewMsg> fakePreview(List<String> paths, RenameRuleMsg rule) => [
      for (var i = 0; i < paths.length; i++)
        RenamePreviewMsg(
          path: paths[i],
          newName: '${rule.base} (${(rule.start + i).toString().padLeft(2, '0')})${ext(paths[i])}',
          conflict: rule.base == 'Lot' && i == 1 ? 'Un élément porte déjà ce nom' : null,
        ),
    ];

FakeHostApi api0() => FakeHostApi()
  ..sources = [src('phone')]
  ..dirs['phone'] = {
    '/': [f('/a.jpg'), f('/b.png'), f('/c.txt')],
  }
  ..renamer = fakePreview;

Future<AppState> pump(WidgetTester tester, FakeHostApi api) async {
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  tester.state<NavigatorState>(find.byType(Navigator)).push(
      MaterialPageRoute<void>(builder: (_) => ExplorerScreen(state: state, sourceId: 'phone')));
  await tester.pumpAndSettle();
  return state;
}

void main() {
  testWidgets('previewThenApply', (tester) async {
    final api = api0();
    await pump(tester, api);
    await tester.longPress(find.text('a.jpg'));
    await tester.pump();
    await tester.tap(find.text('b.png'));
    await tester.pump();
    await tester.tap(find.byTooltip("Plus d'actions"));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Renommer'));
    await tester.pumpAndSettle();
    expect(find.byType(BatchRenameScreen), findsOneWidget);

    // aperçu en direct
    await tester.enterText(find.widgetWithText(TextField, 'Nom de base'), 'Vacances');
    await tester.pumpAndSettle();
    expect(find.text('Vacances (01).jpg'), findsOneWidget);
    expect(find.text('Vacances (02).png'), findsOneWidget);
    expect(find.text('a.jpg'), findsOneWidget); // ancien nom sous le nouveau

    // conflit : bouton désactivé, raison affichée
    await tester.enterText(find.widgetWithText(TextField, 'Nom de base'), 'Lot');
    await tester.pumpAndSettle();
    expect(find.text('Un élément porte déjà ce nom'), findsOneWidget);
    expect(tester.widget<FilledButton>(find.widgetWithText(FilledButton, 'Renommer')).onPressed, isNull);

    await tester.enterText(find.widgetWithText(TextField, 'Nom de base'), 'Vacances');
    await tester.pumpAndSettle();
    await tester.tap(find.widgetWithText(FilledButton, 'Renommer'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('applyRename phone /a.jpg,/b.png number Vacances 1'));
    expect(find.byType(BatchRenameScreen), findsNothing); // retour à l'explorateur
    expect(find.text('2 sélectionné(s)'), findsNothing);
  });

  testWidgets('modesShowTheirFields', (tester) async {
    final api = api0()..renamer = (paths, rule) => [for (final p in paths) RenamePreviewMsg(path: p, newName: p.substring(1))];
    final state = AppState(api);
    await state.refresh();
    await tester.pumpWidget(FluLuksApp(state: state));
    tester.state<NavigatorState>(find.byType(Navigator)).push(MaterialPageRoute<void>(
        builder: (_) => BatchRenameScreen(state: state, sourceId: 'phone', paths: const ['/a.jpg', '/b.png'])));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Remplacer'));
    await tester.pumpAndSettle();
    expect(find.widgetWithText(TextField, 'Rechercher'), findsOneWidget);
    expect(find.widgetWithText(TextField, 'Remplacer par'), findsOneWidget);
    expect(find.text('Respecter la casse'), findsOneWidget);
    await tester.tap(find.text('Date de prise de vue'));
    await tester.pumpAndSettle();
    expect(find.byType(TextField), findsNothing);
    expect(api.calls.last, startsWith('previewRename phone /a.jpg,/b.png photoDate'));
    // rien ne change : bouton désactivé
    expect(tester.widget<FilledButton>(find.widgetWithText(FilledButton, 'Renommer')).onPressed, isNull);
  });
}
