import 'dart:typed_data';

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:fluluks/src/screens/image_viewer_screen.dart';
import 'package:fluluks/src/state/app_state.dart';

import 'fake_host_api.dart';

/// PNG de 1 × 1 pixel.
final png = Uint8List.fromList(const [
  0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, //
  0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49,
  0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99, 0x3D,
  0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
]);

EntryMsg f(String path) => EntryMsg(name: path.split('/').last, path: path, type: 'file', size: 1000, mtime: 1700000000);

SourceMsg src(String id) => SourceMsg(
      id: id,
      stableKey: id,
      kind: 'phone',
      label: 'Téléphone',
      totalBytes: 1000,
      freeBytes: 500,
      readOnly: false,
      roReason: '',
      encrypted: false,
    );

const photos = ['/a.jpg', '/b.png', '/c.jpg', '/d.webp', '/e.heic'];

FakeHostApi api0() {
  final api = FakeHostApi()
    ..sources = [src('phone')]
    ..dirs['phone'] = {
      '/': [for (final p in photos) f(p), f('/notes.txt'), EntryMsg(name: 'DCIM', path: '/DCIM', type: 'dir', size: 0, mtime: 0)],
    };
  for (final p in photos) {
    api.images[p] = png;
  }
  return api;
}

Future<AppState> pump(WidgetTester tester, FakeHostApi api) async {
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  tester.state<NavigatorState>(find.byType(Navigator)).push(
      MaterialPageRoute<void>(builder: (_) => ExplorerScreen(state: state, sourceId: 'phone')));
  await tester.pumpAndSettle();
  return state;
}

/// Images lues par le pont, dans l'ordre des appels.
List<String> reads(FakeHostApi api) =>
    api.calls.where((c) => c.startsWith('readImage ')).map((c) => c.split(' ').last).toList();

Future<void> swipeLeft(WidgetTester tester) async {
  await tester.fling(find.byType(PageView), const Offset(-600, 0), 2000);
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('opensAtTappedImage', (tester) async {
    final api = api0();
    await pump(tester, api);
    await tester.tap(find.text('c.jpg'));
    await tester.pumpAndSettle();
    expect(find.byType(ImageViewerScreen), findsOneWidget);
    // seules les images du dossier, dans l'ordre affiché : c.jpg est la 3e sur 5
    expect(find.text('c.jpg'), findsOneWidget);
    expect(find.text('3 / 5'), findsOneWidget);
    // l'image et ses voisines seulement
    expect(reads(api).toSet(), {'/b.png', '/c.jpg', '/d.webp'});
  });

  testWidgets('swipeShowsNext', (tester) async {
    final api = api0();
    await pump(tester, api);
    await tester.tap(find.text('c.jpg'));
    await tester.pumpAndSettle();
    await swipeLeft(tester);
    expect(find.text('d.webp'), findsOneWidget);
    expect(find.text('4 / 5'), findsOneWidget);
    expect(reads(api).toSet(), {'/b.png', '/c.jpg', '/d.webp', '/e.heic'});
    expect(reads(api).where((p) => p == '/d.webp').length, 1); // gardée en mémoire, pas relue
  });

  testWidgets('doubleTapZooms', (tester) async {
    final api = api0();
    await pump(tester, api);
    await tester.tap(find.text('a.jpg'));
    await tester.pumpAndSettle();
    final center = tester.getCenter(find.byType(PageView));
    await tester.tapAt(center);
    await tester.pump(const Duration(milliseconds: 60));
    await tester.tapAt(center);
    await tester.pumpAndSettle();
    expect(tester.widget<PageView>(find.byType(PageView)).physics, isA<NeverScrollableScrollPhysics>());
    // agrandie : glisser ne change pas d'image
    await tester.fling(find.byType(PageView), const Offset(-600, 0), 2000);
    await tester.pumpAndSettle();
    expect(find.text('1 / 5'), findsOneWidget);
    // second double toucher : taille normale, le glissement revient
    await tester.tapAt(center);
    await tester.pump(const Duration(milliseconds: 60));
    await tester.tapAt(center);
    await tester.pumpAndSettle();
    expect(tester.widget<PageView>(find.byType(PageView)).physics, isNot(isA<NeverScrollableScrollPhysics>()));
  });

  testWidgets('readErrorShownOnPage', (tester) async {
    final api = api0()..readErrors['/b.png'] = 'Image trop grande pour la visionneuse (64 Mio au plus)';
    await pump(tester, api);
    await tester.tap(find.text('b.png'));
    await tester.pumpAndSettle();
    expect(find.text('Image trop grande pour la visionneuse (64 Mio au plus)'), findsOneWidget);
    await swipeLeft(tester);
    expect(find.text('3 / 5'), findsOneWidget);
    expect(find.text('Image trop grande pour la visionneuse (64 Mio au plus)'), findsNothing);
  });

  testWidgets('actionsOnCurrentImage', (tester) async {
    final api = api0();
    await pump(tester, api);
    await tester.tap(find.text('a.jpg'));
    await tester.pumpAndSettle();
    await swipeLeft(tester);
    await tester.tap(find.byTooltip('Partager'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('share phone /b.png'));
    await tester.tap(find.byTooltip('Ouvrir avec…'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('openWith phone /b.png'));
  });

  testWidgets('tapHidesBars', (tester) async {
    final api = api0();
    await pump(tester, api);
    await tester.tap(find.text('a.jpg'));
    await tester.pumpAndSettle();
    expect(find.text('1 / 5'), findsOneWidget);
    await tester.tapAt(tester.getCenter(find.byType(PageView)));
    await tester.pump(kDoubleTapTimeout); // un toucher simple attend la fin du délai du double toucher
    await tester.pumpAndSettle();
    expect(find.text('1 / 5'), findsNothing);
  });

  /// Relecture finale (I5) : sur un dossier distant, une seule voisine préchargée (connexions comptées).
  testWidgets('remotePreloadsOneNeighbour', (tester) async {
    final api = api0();
    api.sources = [src('phone')..kind = 'remote'];
    await pump(tester, api);
    await tester.tap(find.text('c.jpg'));
    await tester.pumpAndSettle();
    expect(reads(api).toSet(), {'/c.jpg', '/d.webp'});
  });
}
