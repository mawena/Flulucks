import 'package:flutter/material.dart';

import 'screens/disks_screen.dart';
import 'state/app_state.dart';
import 'strings_fr.dart';

class FluLuksApp extends StatelessWidget {
  const FluLuksApp({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: Fr.title,
      theme: ThemeData(colorSchemeSeed: Colors.indigo, useMaterial3: true),
      darkTheme: ThemeData(
        colorSchemeSeed: Colors.indigo,
        brightness: Brightness.dark,
        useMaterial3: true,
      ),
      themeMode: ThemeMode.system,
      home: DisksScreen(state: state),
    );
  }
}
