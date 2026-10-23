import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'src/app.dart';
import 'src/bridge/fluluks_api.g.dart';
import 'src/licenses.dart';
import 'src/state/app_state.dart';
import 'src/state/host_api.dart';
import 'src/ui/root.dart';
import 'src/ui/theme.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  registerLicenses(); // licences des bibliothèques natives et Android, pour « À propos »
  final state = AppState(PigeonHostApi());
  FluLuksEventsApi.setUp(state); // Kotlin pousse chaque changement
  // thème et bienvenue lus avant la première image : pas d'éclair de bienvenue pour qui l'a déjà vue
  await state.loadUiPrefs();
  state.refresh(); // état initial (un seul rendu : refresh remplace l'état)
  // retour depuis l'écran système « Accès à tous les fichiers », carte SD insérée… : stockages relus
  AppLifecycleListener(onResume: state.refreshSources);
  // bord à bord : le contenu passe sous les barres système transparentes (SafeArea et AppBar gardent la marge)
  SystemChrome.setEnabledSystemUIMode(SystemUiMode.edgeToEdge);
  SystemChrome.setSystemUIOverlayStyle(portailSystemBars);
  runApp(PortailRoot(state: state, builder: (_) => FluLuksApp(state: state)));
}
