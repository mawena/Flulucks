import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/global_search_screen.dart';
import 'package:fluluks/src/screens/search_screen.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/strings_fr.dart';
import 'package:fluluks/src/ui/states.dart';

import 'explorer_screen_test.dart' show f, src;
import 'fake_host_api.dart';

SearchResultMsg res(List<EntryMsg> l, {bool truncated = false}) => SearchResultMsg(entries: l, truncated: truncated);

FakeHostApi api0() => FakeHostApi()
  ..sources = [
    src('phone'),
    src('sd:1', kind: 'removable', label: 'Carte SD'),
    src('v1', kind: 'disk', label: 'Données'),
    src('remote:x', kind: 'remote', label: 'NAS'),
  ];

Future<AppState> pumpGlobal(WidgetTester tester, FakeHostApi api, {String query = ''}) async {
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  tester.state<NavigatorState>(find.byType(Navigator)).push(
      MaterialPageRoute<void>(builder: (_) => GlobalSearchScreen(state: state, initialQuery: query)));
  await tester.pumpAndSettle();
  return state;
}

Future<void> type(WidgetTester tester, String q) async {
  await tester.enterText(find.byType(TextField), q);
  await tester.pump(const Duration(milliseconds: 401));
  await tester.pumpAndSettle();
}

void main() {
  test('searchableSourcesInHomeOrder', () async {
    final api = FakeHostApi()
      ..sources = [
        src('v1', kind: 'disk', label: 'Données'),
        src('remote:x', kind: 'remote', label: 'NAS'),
        src('phone'),
        src('sd:1', kind: 'removable', label: 'Carte SD'),
      ];
    final state = AppState(api);
    await state.refresh();
    expect([for (final s in searchableSources(state)) s.id], ['phone', 'sd:1', 'v1']);
  });

  testWidgets('searchesEveryOpenStorage', (tester) async {
    final api = api0();
    await pumpGlobal(tester, api);
    await type(tester, 'mer');
    final searches = api.calls.where((c) => c.startsWith('search ')).toList();
    expect(searches, ['search phone / mer', 'search sd:1 / mer', 'search v1 / mer']);
  });

  testWidgets('groupsBySource', (tester) async {
    final api = api0()
      ..searchBySource['phone'] = ((_) => res([f('/DCIM/mer.jpg'), f('/mer.txt')]))
      ..searchBySource['sd:1'] = ((_) => res([f('/Musique/mer.mp3')], truncated: true));
    await pumpGlobal(tester, api);
    await type(tester, 'mer');
    expect(find.text('Téléphone · 2'), findsOneWidget);
    expect(find.text('Carte SD · 1'), findsOneWidget);
    expect(find.textContaining('Données'), findsNothing); // aucun résultat : pas de groupe
    expect(find.text(Fr.tooManyResults), findsOneWidget); // sous la carte SD
    expect(find.text('mer.mp3'), findsOneWidget);
    expect(find.text('/DCIM'), findsOneWidget); // dossier en sous-titre
  });

  testWidgets('globalFollowsGridPreference', (tester) async {
    final api = api0()
      ..viewPrefs = ViewPrefsMsg(sort: 'name', ascending: true, grid: true, showHidden: false, builtInViewers: true)
      ..searchBySource['phone'] = ((_) => res([f('/DCIM/mer.jpg')]))
      ..searchBySource['v1'] = ((_) => res([f('/mer.jpg')]));
    await pumpGlobal(tester, api);
    await type(tester, 'mer');
    expect(find.byType(SliverGrid), findsNWidgets(2));
    expect(api.thumbSources, containsAll(['phone|/DCIM/mer.jpg', 'v1|/mer.jpg']));
  });

  testWidgets('staleResultsDropped', (tester) async {
    final gate = Completer<void>();
    final api = api0()
      ..searchGates['a'] = gate
      ..searchResult = ((q) => res([f('/$q-trouvé.txt')]));
    await pumpGlobal(tester, api);
    await tester.enterText(find.byType(TextField), 'a');
    await tester.pump(const Duration(milliseconds: 401));
    expect(api.calls.last, 'search phone / a'); // en attente
    await type(tester, 'ab');
    expect(api.searchCancels, greaterThanOrEqualTo(1));
    gate.complete();
    await tester.pumpAndSettle();
    expect(find.text('a-trouvé.txt'), findsNothing);
    expect(find.text('ab-trouvé.txt'), findsNWidgets(3));
    expect(api.calls.where((c) => c.startsWith('search ') && c.endsWith(' a')).length, 1); // l'ancienne s'est arrêtée
  });

  testWidgets('cancelOnLeave', (tester) async {
    final api = api0()..searchGates['a'] = Completer<void>();
    await pumpGlobal(tester, api);
    await tester.enterText(find.byType(TextField), 'a');
    await tester.pump(const Duration(milliseconds: 401));
    await tester.pageBack();
    await tester.pumpAndSettle();
    expect(api.searchCancels, 1);
  });

  testWidgets('homeSearchBarOpensGlobal', (tester) async {
    final api = api0();
    final state = AppState(api);
    await state.refresh();
    await tester.pumpWidget(FluLuksApp(state: state));
    await tester.pumpAndSettle();
    expect(find.byType(TextField), findsNothing); // pas de champ ni de clavier sur l'accueil
    await tester.tap(find.bySemanticsLabel(Fr.searchEverywhere));
    await tester.pumpAndSettle();
    expect(find.byType(GlobalSearchScreen), findsOneWidget);
  });

  testWidgets('searchChipEverywhere', (tester) async {
    final api = api0();
    final state = AppState(api);
    await state.refresh();
    await tester.pumpWidget(FluLuksApp(state: state));
    tester.state<NavigatorState>(find.byType(Navigator)).push(
        MaterialPageRoute<void>(builder: (_) => SearchScreen(state: state, sourceId: 'phone', path: '/DCIM')));
    await tester.pumpAndSettle();
    await tester.enterText(find.byType(TextField), 'mer');
    await tester.tap(find.text(Fr.searchAll));
    await tester.pumpAndSettle();
    expect(find.byType(GlobalSearchScreen), findsOneWidget);
    expect(find.byType(SearchScreen), findsNothing);
    expect(tester.widget<TextField>(find.byType(TextField)).controller!.text, 'mer');
    expect(api.calls, containsAll(['search phone / mer', 'search v1 / mer']));
  });

  testWidgets('emptyQueryShowsHint', (tester) async {
    await pumpGlobal(tester, api0());
    expect(find.byType(EmptyState), findsOneWidget);
    expect(find.text(Fr.globalSearchHint), findsOneWidget);
  });

  testWidgets('noResultEmptyState', (tester) async {
    await pumpGlobal(tester, api0());
    await type(tester, 'rien');
    expect(find.text(Fr.noResult), findsOneWidget);
  });
}
