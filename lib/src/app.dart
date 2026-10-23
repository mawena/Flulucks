import 'package:flutter/material.dart';

import 'bridge/fluluks_api.g.dart';
import 'licenses.dart';
import 'screens/home_screen.dart';
import 'screens/welcome_screen.dart';
import 'state/app_state.dart';
import 'strings_fr.dart';
import 'ui/theme.dart';
import 'ui/tokens.dart';
import 'widgets/mini_player.dart';

class FluLuksApp extends StatefulWidget {
  const FluLuksApp({super.key, required this.state});

  final AppState state;

  @override
  State<FluLuksApp> createState() => _FluLuksAppState();
}

class _FluLuksAppState extends State<FluLuksApp> {
  final _navigatorKey = GlobalKey<NavigatorState>();

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: Fr.title,
      theme: portailTheme(Brightness.light, haptics: widget.state.uiPrefs.haptics),
      darkTheme: portailTheme(Brightness.dark, haptics: widget.state.uiPrefs.haptics),
      themeMode: themeModeOf(widget.state.uiPrefs.theme),
      navigatorKey: _navigatorKey,
      // musique en cours : barre réduite sous tous les écrans
      builder: (context, child) => PlayerFrame(
        state: widget.state,
        navigatorKey: _navigatorKey,
        child: child!,
      ),
      localizationsDelegates: const [FrMaterialLocalizations.delegate, FrWidgetsLocalizations.delegate],
      // bienvenue tant qu'elle n'a pas été vue, puis l'accueil
      home: ValueListenableBuilder<UiPrefsMsg>(
        valueListenable: widget.state.uiPrefsListenable,
        builder: (context, prefs, _) => AnimatedSwitcher(
          duration: Motion.of(context, Motion.medium),
          child: prefs.welcomeSeen
              ? HomeScreen(key: const ValueKey('home'), state: widget.state)
              : WelcomeScreen(key: const ValueKey('welcome'), state: widget.state),
        ),
      ),
    );
  }
}
