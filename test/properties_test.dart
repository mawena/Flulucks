import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/strings_fr.dart';

import 'fake_host_api.dart';

EntryMsg f(String path, {int size = 1000}) =>
    EntryMsg(name: path.split('/').last, path: path, type: 'file', size: size, mtime: 1700000000);
EntryMsg d(String path) => EntryMsg(name: path.split('/').last, path: path, type: 'dir', size: 0, mtime: 1700000000);

SourceMsg src(String id, {String kind = 'phone', String label = 'Téléphone'}) => SourceMsg(
    id: id, stableKey: id, kind: kind, label: label, totalBytes: 1000, freeBytes: 500, readOnly: false, roReason: '',
    encrypted: false);

FakeHostApi api0() => FakeHostApi()
  ..sources = [src('phone'), src('v1', kind: 'disk', label: 'Données')]
  ..dirs['phone'] = {
    '/': [d('/DCIM'), f('/b.txt', size: 3)],
  }
  ..dirs['v1'] = {'/': [f('/film.mkv', size: 3000000)]}
  ..detailsByPath['/film.mkv'] = DetailsMsg(
      name: 'film.mkv', path: '/film.mkv', type: 'file', size: 3000000, mtime: 1700000000, mode: 0x81A4, uid: 1000,
      gid: 1001, permissions: 'rw-r--r--', fs: 'ext4')
  ..detailsByPath['/b.txt'] = DetailsMsg(name: 'b.txt', path: '/b.txt', type: 'file', size: 3, mtime: 1700000000, fs: '')
  ..detailsByPath['/DCIM'] = DetailsMsg(name: 'DCIM', path: '/DCIM', type: 'dir', size: 0, mtime: 1700000000, fs: '')
  ..folderSizes['/DCIM'] = FolderSizeMsg(size: 3000000, files: 12, dirs: 3, truncated: false);

Future<AppState> pump(WidgetTester tester, FakeHostApi api, String source) async {
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  tester.state<NavigatorState>(find.byType(Navigator)).push(
      MaterialPageRoute<void>(builder: (_) => ExplorerScreen(state: state, sourceId: source)));
  await tester.pumpAndSettle();
  return state;
}

Future<void> openProperties(WidgetTester tester, String name) async {
  await tester.longPress(find.text(name));
  await tester.pump();
  await tester.tap(find.byTooltip("Plus d'actions"));
  await tester.pumpAndSettle();
  await tester.tap(find.text('Propriétés'));
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('propertiesShowOwnerOnExt4', (tester) async {
    final api = api0();
    await pump(tester, api, 'v1');
    await openProperties(tester, 'film.mkv');
    expect(api.calls, contains('details v1 /film.mkv'));
    expect(find.text('3,0 Mo'), findsWidgets);
    expect(find.text('ext4'), findsOneWidget);
    expect(find.text('rw-r--r-- (0644)'), findsOneWidget);
    expect(find.text('uid 1000, gid 1001'), findsOneWidget);
  });

  testWidgets('noOwnerOutsideExt4', (tester) async {
    final api = api0();
    await pump(tester, api, 'phone');
    await openProperties(tester, 'b.txt');
    expect(find.text('Droits'), findsNothing);
    expect(find.text('Propriétaire'), findsNothing);
    expect(find.text('Système de fichiers'), findsNothing);
  });

  testWidgets('hashButtonShowsProgressThenDigest', (tester) async {
    final api = api0();
    final state = await pump(tester, api, 'phone');
    await openProperties(tester, 'b.txt');
    var pending = Completer<String?>();
    api.sha256Future = pending.future;
    await tester.tap(find.text("Calculer l'empreinte SHA-256"));
    await tester.pump();
    expect(api.calls, contains('sha256 phone /b.txt'));
    state.onHashProgress(40);
    await tester.pump();
    expect(find.text(Fr.percent(40)), findsOneWidget);
    expect(find.byType(LinearProgressIndicator), findsOneWidget);
    pending.complete('ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
    state.onHashProgress(null);
    await tester.pumpAndSettle();
    expect(find.text('ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'), findsOneWidget);
    expect(find.byType(LinearProgressIndicator), findsNothing);

    // feuille fermée (la sélection a été quittée) puis rouverte ; annulé : message, le bouton revient
    await tester.tapAt(const Offset(10, 10));
    await tester.pumpAndSettle();
    expect(find.text('1 sélectionné'), findsNothing);
    await openProperties(tester, 'b.txt');
    pending = Completer<String?>();
    api.sha256Future = pending.future;
    await tester.tap(find.text("Calculer l'empreinte SHA-256"));
    await tester.pump();
    await tester.tap(find.text('Annuler'));
    await tester.pump();
    expect(api.hashCancels, 1);
    pending.complete(null);
    await tester.pumpAndSettle();
    expect(find.text('Calcul annulé'), findsOneWidget);
    expect(find.text("Calculer l'empreinte SHA-256"), findsOneWidget);
  });

  testWidgets('folderSizeShownForDir', (tester) async {
    final api = api0();
    await pump(tester, api, 'phone');
    await openProperties(tester, 'DCIM');
    expect(api.calls, contains('folderSize phone /DCIM'));
    expect(find.text('3,0 Mo — 12 fichiers, 3 dossiers'), findsOneWidget);
    expect(find.text("Calculer l'empreinte SHA-256"), findsNothing); // pas d'empreinte pour un dossier
  });
}
