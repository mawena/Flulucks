import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:fluluks/src/screens/search_screen.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/strings_fr.dart';

import 'explorer_screen_test.dart' show d, f, src;
import 'fake_host_api.dart';

Future<FakeHostApi> pump(WidgetTester tester, {FakeHostApi? api}) async {
  final a = api ?? (FakeHostApi()..sources = [src('phone')]);
  final state = AppState(a);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  tester.state<NavigatorState>(find.byType(Navigator)).push(
      MaterialPageRoute<void>(builder: (_) => SearchScreen(state: state, sourceId: 'phone', path: '/DCIM')));
  await tester.pumpAndSettle();
  return a;
}

SearchResultMsg res(List<EntryMsg> l, {bool truncated = false}) => SearchResultMsg(entries: l, truncated: truncated);

void main() {
  testWidgets('debouncesAndCancelsPrevious', (tester) async {
    final api = FakeHostApi()
      ..sources = [src('phone')]
      ..searchGates['va'] = Completer<void>()
      ..searchResult = (q) => res([f('/DCIM/$q.jpg')]);
    await pump(tester, api: api);
    await tester.enterText(find.byType(TextField), 'v');
    await tester.pump(const Duration(milliseconds: 200));
    await tester.enterText(find.byType(TextField), 'va');
    await tester.pump(const Duration(milliseconds: 399));
    expect(api.calls.where((c) => c.startsWith('search')), isEmpty);
    await tester.pump(const Duration(milliseconds: 2));
    expect(api.calls.where((c) => c.startsWith('search')).toList(), ['search phone /DCIM va']);
    await tester.enterText(find.byType(TextField), 'vac');
    await tester.pump(const Duration(milliseconds: 401));
    expect(api.searchCancels, 1); // « va » encore en cours : annulée
    await tester.pumpAndSettle();
    expect(find.text('vac.jpg'), findsOneWidget);
    api.searchGates['va']!.complete(); // réponse tardive : ignorée
    await tester.pumpAndSettle();
    expect(find.text('va.jpg'), findsNothing);
  });

  testWidgets('showsResultsWithParentPath', (tester) async {
    final api = FakeHostApi()
      ..sources = [src('phone')]
      ..searchResult = (_) => res([f('/DCIM/2026/Été.jpg')]);
    await pump(tester, api: api);
    await tester.enterText(find.byType(TextField), 'ete');
    await tester.pumpAndSettle(const Duration(milliseconds: 500));
    expect(find.text('Été.jpg'), findsOneWidget);
    expect(find.text('/DCIM/2026'), findsOneWidget);
    await tester.tap(find.text('Été.jpg'));
    await tester.pump();
    expect(api.calls, contains('openWith phone /DCIM/2026/Été.jpg'));
    api.searchResult = (_) => res([]);
    await tester.enterText(find.byType(TextField), 'rien');
    await tester.pumpAndSettle(const Duration(milliseconds: 500));
    expect(find.text('Aucun résultat'), findsOneWidget);
  });

  testWidgets('tooManyResultsBanner', (tester) async {
    final api = FakeHostApi()
      ..sources = [src('phone')]
      ..searchResult = (_) => res([for (var i = 0; i < 500; i++) f('/DCIM/f$i')], truncated: true);
    await pump(tester, api: api);
    await tester.enterText(find.byType(TextField), 'f');
    await tester.pumpAndSettle(const Duration(milliseconds: 500));
    expect(find.text(Fr.tooManyResults), findsOneWidget);
  });

  testWidgets('tapFolderOpensExplorer', (tester) async {
    final api = FakeHostApi()
      ..sources = [src('phone')]
      ..searchResult = ((_) => res([d('/DCIM/Vacances')]))
      ..dirs['phone'] = {'/DCIM/Vacances': [f('/DCIM/Vacances/mer.jpg')]};
    await pump(tester, api: api);
    await tester.enterText(find.byType(TextField), 'vac');
    await tester.pumpAndSettle(const Duration(milliseconds: 500));
    await tester.tap(find.text('Vacances'));
    await tester.pumpAndSettle();
    expect(find.byType(ExplorerScreen), findsOneWidget);
    expect(find.text('mer.jpg'), findsOneWidget);
  });

  testWidgets("depuis l'explorateur", (tester) async {
    final api = FakeHostApi()..sources = [src('phone')];
    final state = AppState(api);
    await state.refresh();
    await tester.pumpWidget(FluLuksApp(state: state));
    tester.state<NavigatorState>(find.byType(Navigator)).push(
        MaterialPageRoute<void>(builder: (_) => ExplorerScreen(state: state, sourceId: 'phone')));
    await tester.pumpAndSettle();
    await tester.tap(find.byTooltip('Rechercher'));
    await tester.pumpAndSettle();
    expect(find.byType(SearchScreen), findsOneWidget);
  });
}
