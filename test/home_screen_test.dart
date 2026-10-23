import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/strings_fr.dart';

import 'fake_host_api.dart';

Future<(AppState, FakeHostApi)> pump(WidgetTester tester, FakeHostApi api) async {
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  return (state, api);
}

Future<void> openSettings(WidgetTester tester) async {
  await tester.tap(find.byTooltip('Réglages'));
  await tester.pumpAndSettle();
}

SourceMsg src(String id, String kind, String label, {String? key}) => SourceMsg(
      id: id,
      stableKey: key ?? id,
      kind: kind,
      label: label,
      totalBytes: 64000000000,
      freeBytes: 16000000000,
      readOnly: false,
      roReason: '',
      encrypted: false,
    );

void main() {
  testWidgets('homeShowsPhoneSdDisksFavorites', (tester) async {
    final api = FakeHostApi()
      ..sources = [src('phone', 'phone', 'Téléphone'), src('sd:AB12', 'sd', 'Carte SD'), src('v1', 'disk', 'Données')]
      ..disks = [disk([part(volumeId: 'v1')])]
      ..volumes = [volume()]
      ..favorites = [FavoriteMsg(key: 'phone', path: '/DCIM', label: 'DCIM', sourceId: 'phone')];
    await pump(tester, api);
    expect(find.text('Stockage'), findsOneWidget);
    expect(find.text('Téléphone'), findsOneWidget);
    expect(find.text('Carte SD'), findsOneWidget);
    expect(find.text('16,0 Go libres sur 64,0 Go'), findsNWidgets(2));
    expect(find.text('Disques USB'), findsOneWidget);
    expect(find.text('Disque USB'), findsOneWidget);
    await tester.scrollUntilVisible(find.text('DCIM'), 100);
    expect(find.text('Favoris'), findsOneWidget);
    await tester.ensureVisible(find.text('DCIM')); // la carte du disque porte un bouton de menu (phase 8)
    await tester.pumpAndSettle();
    await tester.tap(find.text('DCIM'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('listDir phone /DCIM false'));
  });

  testWidgets('accessCardUntilGranted', (tester) async {
    final api = FakeHostApi()
      ..allFilesAccess = false
      ..sources = [src('phone', 'phone', 'Téléphone')];
    final (state, _) = await pump(tester, api);
    expect(find.text('Accès aux fichiers du téléphone'), findsOneWidget);
    await tester.tap(find.text('Autoriser'));
    await tester.pump();
    expect(api.accessRequests, 1);
    await tester.tap(find.text('Téléphone')); // sans accès : pas d'explorateur
    await tester.pumpAndSettle();
    expect(api.calls.where((c) => c.startsWith('listDir')), isEmpty);
    api.allFilesAccess = true;
    await state.refreshSources();
    await tester.pump();
    expect(find.text('Accès aux fichiers du téléphone'), findsNothing);
  });

  testWidgets('browseOnOpenVolume', (tester) async {
    final api = FakeHostApi()
      ..sources = [src('v1', 'disk', 'Données')]
      ..disks = [disk([part(volumeId: 'v1')])]
      ..volumes = [volume()];
    await pump(tester, api);
    await tester.tap(find.text('Parcourir'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('listDir v1 / false'));
    expect(find.text('Données'), findsOneWidget); // titre de l'explorateur à la racine
  });

  testWidgets('unavailableFavoriteGreyed', (tester) async {
    final api = FakeHostApi()
      ..favorites = [FavoriteMsg(key: 'disk:u1', path: '/Films', label: 'Films')];
    await pump(tester, api);
    await tester.scrollUntilVisible(find.text('Films'), 100);
    expect(find.text('Disque non ouvert'), findsOneWidget);
    expect(tester.widget<ListTile>(find.widgetWithText(ListTile, 'Films')).enabled, isFalse);
  });

  testWidgets('settingsReadOnlySwitch', (tester) async {
    final api = FakeHostApi();
    await pump(tester, api);
    expect(find.text('Ouvrir les disques en lecture seule'), findsNothing); // plus sur l'accueil
    await openSettings(tester);
    expect(find.text("S'applique aux prochaines ouvertures de disques"), findsOneWidget);
    await tester.tap(find.text('Ouvrir les disques en lecture seule'));
    await tester.pump();
    expect(api.readOnlyPreference, isTrue);
    await tester.tap(find.text('Afficher les fichiers cachés'));
    await tester.pump();
    expect(api.viewPrefs.showHidden, isTrue);
  });

  testWidgets('settingsAccessButton', (tester) async {
    final api = FakeHostApi()..allFilesAccess = false;
    await pump(tester, api);
    await openSettings(tester);
    await tester.tap(find.descendant(of: find.byType(ListTile), matching: find.text('Autoriser')));
    await tester.pump();
    expect(api.accessRequests, 1);
    api.allFilesAccess = true;
    await tester.pageBack();
    await tester.pumpAndSettle();
  });

  testWidgets('liste vide', (tester) async {
    await pump(tester, FakeHostApi());
    expect(find.text('Portail'), findsOneWidget);
    expect(find.text(Fr.noDiskTitle), findsOneWidget);
  });

  testWidgets('disque LUKS2 : bouton Déverrouiller', (tester) async {
    await pump(tester, FakeHostApi()..disks = [disk([part()])]);
    expect(find.text('Partition 1 · 2,0 Go · LUKS2'), findsOneWidget);
    expect(find.text('Déverrouiller'), findsOneWidget);
  });

  testWidgets('BitLocker non pris en charge, sans bouton', (tester) async {
    await pump(
      tester,
      FakeHostApi()..disks = [disk([part(content: 'BitLocker', supported: false, encrypted: false)])],
    );
    expect(find.text('Non pris en charge (BitLocker)'), findsOneWidget);
    expect(find.byType(FilledButton), findsNothing);
  });

  testWidgets('volume ouvert : Fichiers et Éjecter, jamais Déverrouiller', (tester) async {
    final api = FakeHostApi()
      ..disks = [disk([part(volumeId: 'v1')])]
      ..volumes = [volume(readOnly: true, roReason: 'ouvert en lecture seule à votre demande')];
    await pump(tester, api);
    expect(find.text("Ouvert — visible dans l'application Fichiers"), findsOneWidget);
    expect(find.text('Ouvrir dans Fichiers'), findsOneWidget);
    expect(find.text('Déverrouiller'), findsNothing);
    await tester.tap(find.text('Éjecter'));
    await tester.pump();
    expect(api.ejected, ['v1']);
  });

  testWidgets('partition en clair : Ouvrir appelle mountPlain', (tester) async {
    final api = FakeHostApi()
      ..disks = [disk([part(content: 'ext4', encrypted: false)])];
    await pump(tester, api);
    await tester.tap(find.text('Ouvrir'));
    await tester.pump();
    expect(api.mounted, [('d1', 1, true)]);
  });

  testWidgets('interrupteur lecture seule : écriture non demandée, préférence gardée', (tester) async {
    final api = FakeHostApi()
      ..disks = [
        disk([part(content: 'ext4', encrypted: false), part(index: 2)])
      ];
    await pump(tester, api);
    await openSettings(tester);
    final sw = find.widgetWithText(SwitchListTile, 'Ouvrir les disques en lecture seule');
    expect(tester.widget<SwitchListTile>(sw).value, isFalse);
    await tester.tap(sw);
    await tester.pump();
    expect(api.readOnlyPreference, isTrue);
    expect(tester.widget<SwitchListTile>(sw).value, isTrue);
    await tester.pageBack();
    await tester.pumpAndSettle();
    await tester.tap(find.text('Ouvrir'));
    await tester.pump();
    expect(api.mounted, [('d1', 1, false)]);
    await tester.tap(find.text('Déverrouiller'));
    await tester.pumpAndSettle();
    await tester.enterText(find.byType(TextField), 'bonne');
    await tester.tap(find.descendant(of: find.byType(AlertDialog), matching: find.byType(FilledButton)));
    await tester.pumpAndSettle();
    expect(api.unlockWritable, [false]);
  });

  testWidgets('préférence lue au démarrage', (tester) async {
    await pump(tester, FakeHostApi()..readOnlyPreference = true);
    await openSettings(tester);
    expect(
      tester.widget<SwitchListTile>(find.widgetWithText(SwitchListTile, 'Ouvrir les disques en lecture seule')).value,
      isTrue,
    );
  });

  testWidgets('volume imposé en lecture seule : la raison', (tester) async {
    await pump(
      tester,
      FakeHostApi()
        ..disks = [disk([part(volumeId: 'v1')])]
        ..volumes = [volume(readOnly: true, roReason: 'journal à rejouer : branchez le disque sous Ubuntu')],
    );
    expect(find.text(Fr.readOnly('journal à rejouer : branchez le disque sous Ubuntu')), findsOneWidget);
    expect(find.text("Ouvert — visible dans l'application Fichiers"), findsOneWidget);
  });

  testWidgets('volume en écriture : nouveau texte', (tester) async {
    await pump(
      tester,
      FakeHostApi()
        ..disks = [disk([part(volumeId: 'v1')])]
        ..volumes = [volume()],
    );
    expect(find.text("Ouvert en lecture et écriture — visible dans l'application Fichiers"), findsOneWidget);
    expect(find.textContaining('Lecture seule'), findsNothing);
  });

  testWidgets('éjection en cours : indicateur, boutons désactivés ; puis échec affiché', (tester) async {
    final (state, _) = await pump(
      tester,
      FakeHostApi()
        ..disks = [disk([part(volumeId: 'v1'), part(index: 2, content: 'ext4', encrypted: false)])]
        ..volumes = [volume()],
    );
    state.onEjectStateChanged(true, null);
    await tester.pump();
    expect(find.text('Écriture des données en attente…'), findsOneWidget);
    expect(find.byType(CircularProgressIndicator), findsOneWidget);
    expect(tester.widget<OutlinedButton>(find.widgetWithText(OutlinedButton, 'Éjecter')).onPressed, isNull);
    expect(tester.widget<FilledButton>(find.widgetWithText(FilledButton, 'Ouvrir')).onPressed, isNull);
    state.onEjectStateChanged(false, 'Éjection impossible : volume occupé');
    await tester.pump();
    expect(find.text('Écriture des données en attente…'), findsNothing);
    expect(find.text('Éjection impossible : volume occupé'), findsOneWidget);
    expect(tester.widget<OutlinedButton>(find.widgetWithText(OutlinedButton, 'Éjecter')).onPressed, isNotNull);
  });

  testWidgets('sans permission : Autoriser l\'accès', (tester) async {
    final api = FakeHostApi()
      ..disks = [
        DiskMsg(id: 'd1', name: 'X', sizeBytes: 1, hasPermission: false, partitions: [])
      ];
    await pump(tester, api);
    await tester.tap(find.text("Autoriser l'accès"));
    await tester.pump();
    expect(api.permissionRequests, ['d1']);
  });

  testWidgets("erreur fixe sans permission : pas d'Autoriser l'accès", (tester) async {
    final api = FakeHostApi()
      ..disks = [
        DiskMsg(
            id: 'd1', name: 'X', sizeBytes: 1, hasPermission: false,
            error: "Ce boîtier USB n'est pas pris en charge (UAS seulement)", partitions: [])
      ];
    await pump(tester, api);
    expect(find.text("Ce boîtier USB n'est pas pris en charge (UAS seulement)"), findsOneWidget);
    expect(find.text("Autoriser l'accès"), findsNothing);
    expect(find.text('Analyse du disque…'), findsNothing);
    expect(api.permissionRequests, isEmpty);
  });

  testWidgets('disque en cours d\'analyse : « Analyse du disque… »', (tester) async {
    await pump(
      tester,
      FakeHostApi()
        ..disks = [DiskMsg(id: 'd1', name: 'X', sizeBytes: 0, hasPermission: true, partitions: [])],
    );
    expect(find.text('Analyse du disque…'), findsOneWidget);
    expect(find.text("Autoriser l'accès"), findsNothing);
  });

  testWidgets('avertissement needsRecovery', (tester) async {
    final v = volume()..needsRecovery = true;
    await pump(
      tester,
      FakeHostApi()
        ..disks = [disk([part(volumeId: 'v1')])]
        ..volumes = [v],
    );
    expect(
      find.text(Fr.needsRecovery),
      findsOneWidget,
    );
  });

  testWidgets('disque analysé sans partition : erreur affichée', (tester) async {
    await pump(
      tester,
      FakeHostApi()
        ..disks = [
          DiskMsg(
              id: 'd1', name: 'X', sizeBytes: 1, hasPermission: true,
              error: 'Aucune partition reconnue', partitions: [])
        ],
    );
    expect(find.text('Aucune partition reconnue'), findsOneWidget);
    expect(find.text('Analyse du disque…'), findsNothing);
  });

  test('taille lisible', () {
    expect(Fr.humanSize(-1), 'taille inconnue');
    expect(Fr.humanSize(1500000000), '1,5 Go');
    expect(Fr.humanSize(250000000), '250,0 Mo');
  });

  // Phase 7 : une copie interrompue (explorateur fermé avec son disque) reste signalée sur l'accueil.
  testWidgets('homeShowsInterruptedTransfer', (tester) async {
    final (state, _) = await pump(tester, FakeHostApi());
    state.onTransferChanged(TransferMsg(
        kind: 'move', move: true, currentName: 'a.bin', fileIndex: 1, fileCount: 2, doneBytes: 10, totalBytes: 20,
        finished: true, cancelled: false, error: 'Stockage fermé : opération interrompue', elapsedMs: 1200));
    await tester.pump();
    expect(find.text('Stockage fermé : opération interrompue'), findsOneWidget);
    await tester.pump(const Duration(seconds: 5));
  });
}
