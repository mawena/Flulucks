import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/app.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/screens/explorer_screen.dart';
import 'package:fluluks/src/screens/server_form_screen.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/widgets/source_tile.dart';

import 'fake_host_api.dart';

RemoteServerMsg server({bool connected = false, String protocol = 'sftp', String? hostKey}) => RemoteServerMsg(
      id: 's1',
      protocol: protocol,
      name: 'Maison',
      host: '192.168.0.10',
      port: protocol == 'ftp' ? 21 : 22,
      user: 'charles',
      startPath: '/home/charles',
      remember: false,
      hostKey: hostKey,
      hasPassword: false,
      connected: connected,
    );

SourceMsg remoteSrc() => SourceMsg(
      id: 'remote:s1',
      stableKey: 'remote:s1',
      kind: 'remote',
      label: 'Maison',
      totalBytes: -1,
      freeBytes: -1,
      readOnly: false,
      roReason: '',
      encrypted: false,
    );

FakeHostApi api0({RemoteServerMsg? s}) => FakeHostApi()
  ..servers = [s ?? server()]
  ..dirs['remote:s1'] = {
    '/home/charles': [EntryMsg(name: 'film.mkv', path: '/home/charles/film.mkv', type: 'file', size: 10, mtime: 0)],
  };

Future<AppState> pump(WidgetTester tester, FakeHostApi api) async {
  tester.view.physicalSize = const Size(1080, 2400);
  tester.view.devicePixelRatio = 2.5;
  addTearDown(tester.view.reset);
  final state = AppState(api);
  await state.refresh();
  await tester.pumpWidget(FluLuksApp(state: state));
  await tester.pumpAndSettle();
  return state;
}

/// Liste défilante du formulaire de serveur (l'accueil reste dessous).
final formScroll = find.descendant(of: find.byType(ServerFormScreen), matching: find.byType(Scrollable)).first;

Future<void> showServer(WidgetTester tester) async {
  await tester.scrollUntilVisible(find.text('Maison'), 200);
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('homeListsServers', (tester) async {
    await pump(tester, api0());
    await tester.scrollUntilVisible(find.text('Dossiers distants'), 200);
    await showServer(tester);
    expect(find.text('SFTP · charles@192.168.0.10:22'), findsOneWidget);
    expect(find.text('Ajouter un serveur'), findsOneWidget);
  });

  testWidgets('formDefaultsAndFtpWarning', (tester) async {
    final api = api0()..servers = [];
    await pump(tester, api);
    await tester.scrollUntilVisible(find.text('Ajouter un serveur'), 200);
    await tester.tap(find.text('Ajouter un serveur'));
    await tester.pumpAndSettle();
    expect(find.byType(ServerFormScreen), findsOneWidget);
    expect(find.widgetWithText(TextField, '22'), findsOneWidget); // SFTP par défaut
    expect(find.textContaining('circulent en clair'), findsNothing);
    await tester.tap(find.text('FTP'));
    await tester.pumpAndSettle();
    expect(find.widgetWithText(TextField, '21'), findsOneWidget);
    expect(find.textContaining('circulent en clair'), findsOneWidget);
    expect(tester.widget<CheckboxListTile>(find.byType(CheckboxListTile)).value, isFalse); // « Mémoriser » décoché
    await tester.enterText(find.widgetWithText(TextField, 'Adresse'), 'ftp.exemple.fr');
    await tester.enterText(find.widgetWithText(TextField, 'Mot de passe'), 'secret');
    await tester.scrollUntilVisible(find.text('Enregistrer'), 200, scrollable: formScroll);
    await tester.tap(find.text('Enregistrer'));
    await tester.pumpAndSettle();
    final (msg, pw) = api.savedServers.single;
    expect([msg.protocol, msg.host, msg.port, msg.remember, msg.id], ['ftp', 'ftp.exemple.fr', 21, false, '']);
    expect(pw, 'secret');
    expect(find.byType(ServerFormScreen), findsNothing);
  });

  testWidgets('connectAsksPasswordWhenMissing', (tester) async {
    final api = api0()
      ..connect = (id, pw) {
        if (pw == null) throw PlatformException(code: 'auth', message: 'Mot de passe requis', details: true);
        if (utf8.decode(pw) != 'bon') throw PlatformException(code: 'auth', message: 'refusé', details: false);
        return remoteSrc();
      };
    await pump(tester, api);
    await showServer(tester);
    await tester.tap(find.text('Maison'));
    await tester.pumpAndSettle();
    expect(find.text('Mot de passe de Maison'), findsOneWidget);
    await tester.enterText(find.byType(TextField), 'faux');
    await tester.tap(find.text('Se connecter'));
    await tester.pumpAndSettle();
    expect(find.text('Mot de passe refusé'), findsOneWidget); // redemandé
    await tester.enterText(find.byType(TextField), 'bon');
    await tester.tap(find.text('Se connecter'));
    await tester.pumpAndSettle();
    expect(find.byType(ExplorerScreen), findsOneWidget);
    expect(find.text('film.mkv'), findsOneWidget); // ouvert au dossier de départ
    expect(api.connectPasswords, [null, 'faux', 'bon']);
  });

  testWidgets('hostKeyDialogThenTrust', (tester) async {
    var trusted = false;
    final api = api0()
      ..connect = (id, pw) {
        if (!trusted) throw PlatformException(code: 'hostkey-unknown', message: 'Clé inconnue', details: 'ssh-ed25519 SHA256:abc');
        return remoteSrc();
      }
      ..onTrust = (id, fp) => trusted = true;
    await pump(tester, api);
    await showServer(tester);
    await tester.tap(find.text('Maison'));
    await tester.pumpAndSettle();
    expect(find.textContaining('ssh-ed25519 SHA256:abc'), findsOneWidget);
    await tester.tap(find.text('Faire confiance'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('trustHostKey s1 ssh-ed25519 SHA256:abc'));
    expect(find.byType(ExplorerScreen), findsOneWidget);
  });

  testWidgets('hostKeyChangedNoTrustButton', (tester) async {
    final api = api0()
      ..connect = (id, pw) => throw PlatformException(
          code: 'hostkey-changed', message: 'La clé du serveur a changé : connexion refusée.', details: null);
    await pump(tester, api);
    await showServer(tester);
    await tester.tap(find.text('Maison'));
    await tester.pumpAndSettle();
    expect(find.text('La clé du serveur a changé : connexion refusée.'), findsOneWidget);
    expect(find.text('Faire confiance'), findsNothing);
    expect(find.byType(ExplorerScreen), findsNothing);
  });

  testWidgets('connectedServerDisconnects', (tester) async {
    final api = api0(s: server(connected: true))..sources = [remoteSrc()];
    await pump(tester, api);
    await showServer(tester);
    await tester.tap(find.byTooltip('Déconnecter'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('disconnectServer s1'));
    // un serveur connecté ne figure pas parmi les stockages du téléphone
    expect(find.text('Stockage'), findsNothing);
  });

  testWidgets('editForgetsHostKeyAndDeletes', (tester) async {
    final api = api0(s: server(hostKey: 'ssh-ed25519 SHA256:abc'));
    await pump(tester, api);
    await showServer(tester);
    await tester.tap(find.byTooltip('Modifier'));
    await tester.pumpAndSettle();
    expect(find.text('ssh-ed25519 SHA256:abc'), findsOneWidget);
    await tester.scrollUntilVisible(find.text("Oublier l'empreinte"), 200, scrollable: formScroll);
    await tester.tap(find.text("Oublier l'empreinte"));
    await tester.pumpAndSettle();
    expect(api.calls, contains('forgetHostKey s1'));
    await tester.tap(find.byTooltip('Supprimer le serveur'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Supprimer'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('deleteServer s1'));
  });

  testWidgets('remoteDeleteIsPermanentAndNoTrash', (tester) async {
    final api = api0(s: server()..startPath = '/')
      ..connect = ((id, pw) => remoteSrc())
      ..sources = [remoteSrc()];
    api.dirs['remote:s1']!['/'] = [EntryMsg(name: 'a.txt', path: '/a.txt', type: 'file', size: 1, mtime: 0)];
    await pump(tester, api);
    await showServer(tester);
    await tester.tap(find.text('Maison'));
    await tester.pumpAndSettle();
    await tester.tap(find.byTooltip('Trier par'));
    await tester.pumpAndSettle();
    expect(find.text('Corbeille'), findsNothing); // pas de corbeille distante
    await tester.tapAt(const Offset(10, 10));
    await tester.pumpAndSettle();
    await tester.longPress(find.text('a.txt'));
    await tester.pumpAndSettle();
    await tester.tap(find.byTooltip('Supprimer'));
    await tester.pumpAndSettle();
    expect(find.textContaining('Pas de corbeille sur un dossier distant'), findsOneWidget);
    expect(find.text('Supprimer définitivement'), findsNothing);
    await tester.tap(find.widgetWithText(FilledButton, 'Supprimer'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('delete remote:s1 /a.txt'));
  });

  testWidgets('remoteTileUnknownSpace', (tester) async {
    await tester.pumpWidget(MaterialApp(home: Scaffold(body: SourceTile(source: remoteSrc(), onTap: null))));
    expect(find.text('Espace inconnu'), findsOneWidget);
    expect(find.byType(LinearProgressIndicator), findsNothing);
    expect(find.byIcon(Icons.cloud_outlined), findsOneWidget);
  });

  testWidgets('sftpKeyImportedAndRemoved', (tester) async {
    final api = api0()..importedKey = 'ssh-ed25519 SHA256:clé';
    await pump(tester, api);
    await showServer(tester);
    await tester.tap(find.byTooltip('Modifier'));
    await tester.pumpAndSettle();
    await tester.scrollUntilVisible(find.text('Importer une clé'), 200, scrollable: formScroll);
    expect(find.text('Aucune\u00A0: connexion par mot de passe'), findsOneWidget);
    await tester.tap(find.text('Importer une clé'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('importPrivateKey s1'));
    expect(find.text('ssh-ed25519 SHA256:clé'), findsOneWidget);
    expect(find.widgetWithText(TextField, 'Phrase de la clé'), findsOneWidget);
    await tester.scrollUntilVisible(find.text('Retirer la clé'), 200, scrollable: formScroll);
    await tester.tap(find.text('Retirer la clé'));
    await tester.pumpAndSettle();
    expect(api.calls, contains('forgetPrivateKey s1'));
    expect(find.text('Aucune\u00A0: connexion par mot de passe'), findsOneWidget);
  });

  testWidgets('keySectionOnlyForSavedSftp', (tester) async {
    final api = api0(s: server(protocol: 'ftp'));
    await pump(tester, api);
    await showServer(tester);
    await tester.tap(find.byTooltip('Modifier'));
    await tester.pumpAndSettle();
    expect(find.text('Importer une clé'), findsNothing); // FTP : pas de clé
    await tester.tap(find.text('SFTP'));
    await tester.pumpAndSettle();
    await tester.scrollUntilVisible(find.text('Importer une clé'), 200, scrollable: formScroll);
    expect(find.text('Importer une clé'), findsOneWidget);
  });
}
