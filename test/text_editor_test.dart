import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:fluluks/src/screens/text_editor_screen.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/viewers.dart';

import 'fake_host_api.dart';

EntryMsg f(String path, {int size = 1000}) =>
    EntryMsg(name: path.split('/').last, path: path, type: 'file', size: size, mtime: 1700000000);

SourceMsg src(String id, {bool readOnly = false}) => SourceMsg(
      id: id,
      stableKey: id,
      kind: 'phone',
      label: 'Téléphone',
      totalBytes: 1000,
      freeBytes: 500,
      readOnly: readOnly,
      roReason: readOnly ? 'protégé en écriture' : '',
      encrypted: false,
    );

TextFileMsg text(String t, {bool bom = false, bool crlf = false, String? ro}) =>
    TextFileMsg(text: t, bom: bom, crlf: crlf, mtime: 1700000000, size: t.length, readOnlyReason: ro);

FakeHostApi api0({bool viewers = true}) => FakeHostApi()
  ..sources = [src('phone')]
  ..viewPrefs = ViewPrefsMsg(sort: 'name', ascending: true, grid: false, showHidden: false, builtInViewers: viewers)
  ..dirs['phone'] = {
    '/': [f('/notes.txt'), f('/rapport.pdf'), f('/README')],
  }
  ..texts['/notes.txt'] = text('ligne 1\nligne 2', bom: true, crlf: true)
  ..texts['/README'] = text('lisez-moi');

Future<AppState> pump(WidgetTester tester, FakeHostApi api) async {
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  tester.state<NavigatorState>(find.byType(Navigator)).push(
      MaterialPageRoute<void>(builder: (_) => ExplorerScreen(state: state, sourceId: 'phone')));
  await tester.pumpAndSettle();
  return state;
}

/// Ouvre notes.txt dans l'éditeur depuis l'explorateur.
Future<void> openNotes(WidgetTester tester) async {
  await tester.tap(find.text('notes.txt'));
  await tester.pumpAndSettle();
  expect(find.byType(TextEditorScreen), findsOneWidget);
}

void main() {
  test('viewerForExtensions', () {
    expect(viewerFor('IMG.JPG'), 'image');
    expect(viewerFor('photo.heic'), 'image');
    expect(viewerFor('a.Mp3'), 'audio');
    expect(viewerFor('film.mkv'), 'video');
    expect(viewerFor('notes.TXT'), 'text');
    expect(viewerFor('.gitignore'), 'text');
    expect(viewerFor('a.tar.gz'), isNull);
    expect(viewerFor('README'), isNull);
    expect(viewerFor('doc.pdf'), isNull);
    expect(viewerFor('fin.'), isNull);
  });

  testWidgets('tapTextOpensEditor', (tester) async {
    final api = api0();
    await pump(tester, api);
    await openNotes(tester);
    expect(find.text('ligne 1\nligne 2'), findsOneWidget);
    expect(api.calls, isNot(contains('openWith phone /notes.txt')));
    // un fichier sans lecteur de Portail s'ouvre dans une autre application
    await tester.pageBack();
    await tester.pumpAndSettle();
    await tester.tap(find.text('rapport.pdf'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('openWith phone /rapport.pdf'));
  });

  testWidgets('settingOffOpensWithOtherApp', (tester) async {
    final api = api0(viewers: false);
    await pump(tester, api);
    await tester.tap(find.text('notes.txt'));
    await tester.pumpAndSettle();
    expect(find.byType(TextEditorScreen), findsNothing);
    expect(api.calls, contains('openWith phone /notes.txt'));
  });

  testWidgets('editAndSaveKeepsBomAndCrlf', (tester) async {
    final api = api0();
    await pump(tester, api);
    await openNotes(tester);
    expect(find.text('notes.txt'), findsOneWidget);
    await tester.enterText(find.byType(TextField), 'ligne 1\nligne 2\nligne 3');
    await tester.pump();
    expect(find.text('• notes.txt'), findsOneWidget); // modifié
    await tester.tap(find.byTooltip('Enregistrer'));
    await tester.pumpAndSettle();
    final (path, t, bom, crlf, mtime, size, force) = api.writes.single;
    expect([path, t, bom, crlf, mtime, size, force], ['/notes.txt', 'ligne 1\nligne 2\nligne 3', true, true, 1700000000, 15, false]);
    expect(find.text('notes.txt'), findsOneWidget); // plus marqué modifié
    expect(find.text('Enregistré'), findsOneWidget);
    // l'enregistrement suivant repart de la date et de la taille rendues
    await tester.enterText(find.byType(TextField), 'autre');
    await tester.pump();
    await tester.tap(find.byTooltip('Enregistrer'));
    await tester.pumpAndSettle();
    expect(api.writes.last.$5, 1700000001);
  });

  testWidgets('unsavedChangesAskBeforeLeaving', (tester) async {
    final api = api0();
    await pump(tester, api);
    await openNotes(tester);
    await tester.enterText(find.byType(TextField), 'brouillon');
    await tester.pump();
    // Annuler : on reste dans l'éditeur
    await tester.pageBack();
    await tester.pumpAndSettle();
    expect(find.text('Enregistrer les modifications avant de quitter ?'), findsOneWidget);
    await tester.tap(find.text('Annuler'));
    await tester.pumpAndSettle();
    expect(find.byType(TextEditorScreen), findsOneWidget);
    // Ne pas enregistrer : on quitte sans écrire
    await tester.pageBack();
    await tester.pumpAndSettle();
    await tester.tap(find.text('Ne pas enregistrer'));
    await tester.pumpAndSettle();
    expect(find.byType(TextEditorScreen), findsNothing);
    expect(api.writes, isEmpty);
    // Enregistrer : écrit puis quitte
    await openNotes(tester);
    await tester.enterText(find.byType(TextField), 'gardé');
    await tester.pump();
    await tester.pageBack();
    await tester.pumpAndSettle();
    await tester.tap(find.widgetWithText(TextButton, 'Enregistrer'));
    await tester.pumpAndSettle();
    expect(api.writes.single.$2, 'gardé');
    expect(find.byType(TextEditorScreen), findsNothing);
  });

  testWidgets('changedFileAsksToOverwrite', (tester) async {
    final api = api0()
      ..onWriteText = (path, force) => force
          ? TextSaveMsg(saved: true, changed: false, mtime: 1700000300, size: 3)
          : TextSaveMsg(saved: false, changed: true, mtime: 1700000200, size: 99);
    await pump(tester, api);
    await openNotes(tester);
    await tester.enterText(find.byType(TextField), 'moi');
    await tester.pump();
    await tester.tap(find.byTooltip('Enregistrer'));
    await tester.pumpAndSettle();
    expect(find.text('Le fichier a été modifié depuis son ouverture. Le remplacer par votre version ?'), findsOneWidget);
    await tester.tap(find.text('Remplacer'));
    await tester.pumpAndSettle();
    expect(api.writes.map((w) => w.$7), [false, true]);
    expect(find.text('notes.txt'), findsOneWidget);
  });

  testWidgets('readOnlyShowsReason', (tester) async {
    final api = api0()..texts['/notes.txt'] = text('figé', ro: 'Stockage en lecture seule');
    await pump(tester, api);
    await openNotes(tester);
    expect(find.text('Lecture seule : Stockage en lecture seule'), findsOneWidget);
    expect(find.byTooltip('Enregistrer'), findsNothing);
    expect(tester.widget<TextField>(find.byType(TextField)).readOnly, isTrue);
  });

  testWidgets('saveFailureKeepsText', (tester) async {
    final api = api0()..writeError = 'Disque plein';
    await pump(tester, api);
    await openNotes(tester);
    await tester.enterText(find.byType(TextField), 'précieux');
    await tester.pump();
    await tester.tap(find.byTooltip('Enregistrer'));
    await tester.pumpAndSettle();
    expect(find.text('Disque plein'), findsOneWidget);
    expect(find.text('précieux'), findsOneWidget);
    expect(find.text('• notes.txt'), findsOneWidget);
  });

  testWidgets('readErrorShown', (tester) async {
    final api = api0()..readErrors['/notes.txt'] = "Ce fichier n'est pas du texte UTF-8";
    await pump(tester, api);
    await openNotes(tester);
    expect(find.text("Ce fichier n'est pas du texte UTF-8"), findsOneWidget);
    expect(find.byType(TextField), findsNothing);
  });

  testWidgets('moreActionsOpenWithAndEditAsText', (tester) async {
    final api = api0();
    await pump(tester, api);
    await tester.longPress(find.text('README'));
    await tester.pump();
    await tester.tap(find.byTooltip("Plus d'actions"));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Modifier comme texte'));
    await tester.pumpAndSettle();
    expect(find.byType(TextEditorScreen), findsOneWidget);
    expect(find.text('lisez-moi'), findsOneWidget);
    await tester.pageBack();
    await tester.pumpAndSettle();
    await tester.longPress(find.text('rapport.pdf'));
    await tester.pump();
    await tester.tap(find.byTooltip("Plus d'actions"));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Ouvrir avec…'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('openWith phone /rapport.pdf'));
  });

  /// Relecture finale (C2) : le stockage se ferme pendant une modification ; le texte reste à l'écran.
  testWidgets('dirtyEditorKeptWhenSourceCloses', (tester) async {
    final api = api0();
    final state = await pump(tester, api);
    await openNotes(tester);
    await tester.enterText(find.byType(TextField), 'texte précieux');
    await tester.pump();
    api.sources = [];
    await state.refreshSources();
    await tester.pumpAndSettle();
    expect(find.byType(TextEditorScreen), findsOneWidget); // pas fermé
    expect(find.text('texte précieux'), findsOneWidget);
    expect(find.textContaining('Stockage fermé'), findsOneWidget);
    expect(tester.widget<IconButton>(find.widgetWithIcon(IconButton, Icons.save)).onPressed, isNull);
    // en partant, on peut abandonner le texte ; l'explorateur du stockage fermé part avec
    await tester.pageBack();
    await tester.pumpAndSettle();
    expect(find.text('Enregistrer'), findsNothing); // plus d'enregistrement possible
    await tester.tap(find.text('Ne pas enregistrer'));
    await tester.pumpAndSettle();
    expect(find.byType(TextEditorScreen), findsNothing);
    expect(find.byType(ExplorerScreen), findsNothing);
  });

  testWidgets('cleanEditorClosesWithSource', (tester) async {
    final api = api0();
    final state = await pump(tester, api);
    await openNotes(tester);
    api.sources = [];
    await state.refreshSources();
    await tester.pumpAndSettle();
    expect(find.byType(TextEditorScreen), findsNothing);
    expect(find.byType(ExplorerScreen), findsNothing);
  });

  /// Relecture finale (I6) : le clavier n'apprend pas le texte (disques chiffrés).
  testWidgets('keyboardDoesNotLearn', (tester) async {
    await pump(tester, api0());
    await openNotes(tester);
    final tf = tester.widget<TextField>(find.byType(TextField));
    expect([tf.enableIMEPersonalizedLearning, tf.enableSuggestions, tf.autocorrect], [false, false, false]);
  });
}
