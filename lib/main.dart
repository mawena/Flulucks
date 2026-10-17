import 'package:flutter/material.dart';

import 'src/app.dart';
import 'src/bridge/fluluks_api.g.dart';
import 'src/state/app_state.dart';
import 'src/state/host_api.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  final state = AppState(PigeonHostApi());
  FluLuksEventsApi.setUp(state); // Kotlin pousse chaque changement
  state.refresh(); // état initial (un seul rendu : refresh remplace l'état)
  runApp(FluLuksApp(state: state));
}
