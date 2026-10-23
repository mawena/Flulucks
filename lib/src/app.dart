import 'package:flutter/material.dart';

import 'screens/home_screen.dart';
import 'state/app_state.dart';
import 'strings_fr.dart';
import 'widgets/mini_player.dart';

class FluLuksApp extends StatefulWidget {
  const FluLuksApp({super.key, required this.state});

  final AppState state;

  @override
  State<FluLuksApp> createState() => _FluLuksAppState();
}

class _FluLuksAppState extends State<FluLuksApp> {
  final _navigatorKey = GlobalKey<NavigatorState>();
  final _messengerKey = GlobalKey<ScaffoldMessengerState>();

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
      navigatorKey: _navigatorKey,
      scaffoldMessengerKey: _messengerKey,
      // musique en cours : barre réduite sous tous les écrans
      builder: (context, child) => PlayerFrame(
        state: widget.state,
        navigatorKey: _navigatorKey,
        messengerKey: _messengerKey,
        child: child!,
      ),
      home: HomeScreen(state: widget.state),
    );
  }
}
