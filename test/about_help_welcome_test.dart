import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/licenses.dart';
import 'package:fluluks/src/screens/home_screen.dart';
import 'package:fluluks/src/screens/welcome_screen.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/strings_fr.dart';
import 'package:fluluks/src/ui/widgets.dart';
import 'package:fluluks/src/widgets/brand_mark.dart';

import 'fake_host_api.dart';

Future<AppState> pumpApp(WidgetTester tester, FakeHostApi api) async {
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  return state;
}

/// Ouvre l'entrée [item] du menu « Plus d'options » de l'accueil.
Future<void> homeMenu(WidgetTester tester, String item) async {
  await tester.tap(find.byTooltip(Fr.moreOptions));
  await tester.pumpAndSettle();
  await tester.tap(find.text(item));
  await tester.pumpAndSettle();
}

FakeHostApi firstLaunch() =>
    FakeHostApi()..uiPrefs = UiPrefsMsg(theme: 'system', haptics: true, welcomeSeen: false);

/// Les 12 bibliothèques dont la licence est montrée dans « Licences des bibliothèques ».
const libraries = [
  'lwext4',
  'ntfs-3g',
  'FatFs',
  'libarchive',
  'XZ Utils',
  'Argon2',
  'cJSON',
  'Mbed TLS',
  'Media3',
  'Apache Commons Net',
  'JSch',
  'Bouncy Castle',
];

void main() {
  testWidgets('aboutShowsVersion', (tester) async {
    await pumpApp(tester, FakeHostApi());
    await homeMenu(tester, Fr.about);
    expect(find.descendant(of: find.byType(ListView), matching: find.byType(BrandMark)), findsOneWidget);
    expect(tester.widget<BrandMark>(find.byType(BrandMark).last).size, 72);
    expect(find.text('Version 1.4.0 (11)'), findsOneWidget);
    expect(find.text('Android 13 · Pixel 4 XL'), findsOneWidget);
    expect(find.text(Fr.aboutTagline), findsOneWidget);
    expect(find.text(Fr.gplNote), findsOneWidget);
  });

  test('licensesRegistered', () async {
    TestWidgetsFlutterBinding.ensureInitialized();
    LicenseRegistry.reset(); // seulement les licences de Portail, sans celles de Flutter
    registerLicenses();
    final texts = <String, String>{};
    await for (final l in LicenseRegistry.licenses) {
      for (final p in l.packages) {
        texts[p] = l.paragraphs.map((p) => p.text).join('\n');
      }
    }
    expect(texts.keys.toSet(), libraries.toSet());
    expect(texts['lwext4'], contains('GNU GENERAL PUBLIC LICENSE'));
    expect(texts['ntfs-3g'], contains('GNU GENERAL PUBLIC LICENSE'));
    expect(texts['Media3'], contains('Apache License'));
    expect(texts['Apache Commons Net'], contains('Apache License'));
    expect(texts['Apache Commons Net'], contains('The Apache Software Foundation'));
    expect(texts['JSch'], contains('JCraft'));
    expect(texts['Bouncy Castle'], contains('Legion of the Bouncy Castle'));
    expect(texts['FatFs'], isNot(contains('\r')));
    for (final name in libraries) {
      expect(texts[name]!.length, greaterThan(300), reason: name);
    }
  });

  testWidgets('licensePageOpens', (tester) async {
    await pumpApp(tester, FakeHostApi());
    await homeMenu(tester, Fr.about);
    await tester.tap(find.text(Fr.licensesButton));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 500));
    expect(find.byType(LicensePage), findsOneWidget);
    final page = tester.widget<LicensePage>(find.byType(LicensePage));
    expect([page.applicationName, page.applicationVersion], [Fr.title, '1.4.0']);
    expect(page.applicationIcon, isA<BrandMark>());
  });

  testWidgets('helpEntriesExpand', (tester) async {
    await pumpApp(tester, FakeHostApi());
    await homeMenu(tester, Fr.help);
    expect(find.byType(ExpansionTile), findsNWidgets(8));
    final titles = [
      'Ouvrir un disque chiffré',
      'Pourquoi mon disque s\'ouvre en lecture seule ?',
      'Débrancher sans risque',
      "Voir le disque dans d'autres applications",
      'La corbeille',
      'Le verrouillage automatique',
      'Les dossiers distants (FTP, SFTP)',
      'Formater un disque',
    ];
    expect(Fr.helpEntries.map((e) => e.$1).toList(), titles);
    for (final (title, answer) in Fr.helpEntries) {
      final sentences = RegExp(r'[.!?](\s|$)').allMatches(answer).length; // « Formater… » ne clôt pas
      expect(sentences, inInclusiveRange(1, 3), reason: title);
    }
    final firstAnswer = Fr.helpEntries.first.$2;
    expect(find.text(firstAnswer), findsNothing);
    await tester.tap(find.text(titles.first));
    await tester.pumpAndSettle();
    expect(find.text(firstAnswer), findsOneWidget);
  });

  testWidgets('welcomeShownOnce', (tester) async {
    final api = firstLaunch();
    await pumpApp(tester, api);
    expect(find.byType(WelcomeScreen), findsOneWidget);
    expect(find.byType(HomeScreen), findsNothing);
    expect(find.text(Fr.welcomeTitle), findsOneWidget);
    expect(tester.widget<BrandMark>(find.byType(BrandMark)).size, 96);
    for (final point in [Fr.welcomeDisks, Fr.welcomePrivacy, Fr.welcomeAccess]) {
      expect(find.text(point), findsOneWidget, reason: point);
    }
    await tester.scrollUntilVisible(find.text(Fr.start), 100);
    await tester.tap(find.text(Fr.start));
    await tester.pumpAndSettle();
    expect(find.byType(HomeScreen), findsOneWidget);
    expect(find.byType(WelcomeScreen), findsNothing);
    expect(api.uiPrefs.welcomeSeen, isTrue);
    expect([api.uiPrefs.theme, api.uiPrefs.haptics], ['system', true]);

    // lancement suivant : l'accueil directement
    await tester.pumpWidget(const SizedBox());
    await pumpApp(tester, api);
    expect(find.byType(HomeScreen), findsOneWidget);
    expect(find.byType(WelcomeScreen), findsNothing);
  });

  testWidgets('welcomeAllowAccessCallsApi', (tester) async {
    final api = firstLaunch()..allFilesAccess = false;
    await pumpApp(tester, api);
    await tester.scrollUntilVisible(find.text(Fr.grantAccess), 100);
    await tester.tap(find.text(Fr.grantAccess));
    await tester.pump();
    expect(api.accessRequests, 1);
    expect(find.byType(WelcomeScreen), findsOneWidget); // l'utilisateur revient de l'écran Android, puis commence
  });

  testWidgets('laterKeepsUsbUsable', (tester) async {
    final api = firstLaunch()
      ..allFilesAccess = false
      ..disks = [disk([part()])];
    await pumpApp(tester, api);
    await tester.scrollUntilVisible(find.text(Fr.start), 100);
    await tester.tap(find.text(Fr.start));
    await tester.pumpAndSettle();
    expect(api.accessRequests, 0);
    expect(find.byType(HomeScreen), findsOneWidget);
    expect(find.widgetWithText(StatusBanner, Fr.allFilesTitle), findsOneWidget);
    await tester.scrollUntilVisible(find.text(Fr.unlock), 100);
    expect(find.text(Fr.usbDisks), findsOneWidget);
    expect(find.text(Fr.unlock), findsOneWidget);
  });
}
