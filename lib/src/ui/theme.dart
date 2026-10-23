import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'tokens.dart';

/// Graine de la charte : fin du dégradé terracotta du logo (assets/logo/portail.svg).
const portailSeed = Color(0xFFC2410C);

final _schemes = <Brightness, ColorScheme>{};
final _themes = <(Brightness, bool), ThemeData>{};

/// Schéma Material 3 tiré du logo ; `fidelity` garde le terracotta reconnaissable en clair comme en sombre.
ColorScheme portailScheme(Brightness b) => _schemes.putIfAbsent(
    b, () => ColorScheme.fromSeed(seedColor: portailSeed, brightness: b, dynamicSchemeVariant: DynamicSchemeVariant.fidelity));

/// `light` ou `dark` ; toute autre valeur suit le système.
ThemeMode themeModeOf(String pref) => switch (pref) {
      'light' => ThemeMode.light,
      'dark' => ThemeMode.dark,
      _ => ThemeMode.system,
    };

/// Barres système transparentes (affichage bord à bord), posées au démarrage.
const portailSystemBars = SystemUiOverlayStyle(
  systemNavigationBarColor: Colors.transparent,
  systemNavigationBarDividerColor: Colors.transparent,
  systemNavigationBarContrastEnforced: false,
);

/// Thème de Portail ; calculé une fois par luminosité et réglage haptique (l'application est reconstruite à
/// chaque changement de préférence).
ThemeData portailTheme(Brightness b, {bool haptics = true}) => _themes.putIfAbsent((b, haptics), () => _build(b, haptics));

ThemeData _build(Brightness b, bool haptics) {
  final s = portailScheme(b);
  final base = ThemeData(colorScheme: s, useMaterial3: true);
  final t = base.textTheme;
  const semibold = FontWeight.w600;
  const tall = Size(64, Layout.minTouch);
  final rounded12 = RoundedRectangleBorder(borderRadius: BorderRadius.circular(Radii.m));
  final iconsDark = b == Brightness.light;
  return base.copyWith(
    scaffoldBackgroundColor: s.surface,
    textTheme: t.copyWith(
      headlineSmall: t.headlineSmall?.copyWith(fontWeight: semibold),
      titleMedium: t.titleMedium?.copyWith(fontWeight: semibold),
      labelLarge: t.labelLarge?.copyWith(fontWeight: semibold),
    ),
    appBarTheme: AppBarTheme(
      backgroundColor: s.surface,
      foregroundColor: s.onSurface,
      surfaceTintColor: s.surfaceTint,
      scrolledUnderElevation: 3,
      systemOverlayStyle: portailSystemBars.copyWith(
        statusBarColor: Colors.transparent,
        statusBarIconBrightness: iconsDark ? Brightness.dark : Brightness.light,
        statusBarBrightness: b,
        systemNavigationBarIconBrightness: iconsDark ? Brightness.dark : Brightness.light,
      ),
    ),
    cardTheme: CardThemeData(
      elevation: 0,
      color: s.surfaceContainerLow,
      margin: EdgeInsets.zero,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(Radii.l)),
    ),
    listTileTheme: ListTileThemeData(enableFeedback: haptics),
    filledButtonTheme: FilledButtonThemeData(style: FilledButton.styleFrom(minimumSize: tall)),
    elevatedButtonTheme: ElevatedButtonThemeData(style: ElevatedButton.styleFrom(minimumSize: tall)),
    outlinedButtonTheme: OutlinedButtonThemeData(style: OutlinedButton.styleFrom(minimumSize: tall)),
    textButtonTheme: TextButtonThemeData(style: TextButton.styleFrom(minimumSize: const Size(48, Layout.minTouch))),
    inputDecorationTheme: InputDecorationThemeData(
      border: OutlineInputBorder(borderRadius: BorderRadius.circular(Radii.m)),
    ),
    dialogTheme: DialogThemeData(shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(Radii.xl))),
    bottomSheetTheme: const BottomSheetThemeData(
      showDragHandle: true,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.vertical(top: Radius.circular(Radii.xl))),
    ),
    snackBarTheme: SnackBarThemeData(behavior: SnackBarBehavior.floating, shape: rounded12),
    progressIndicatorTheme: ProgressIndicatorThemeData(
      linearMinHeight: 6,
      borderRadius: BorderRadius.circular(3),
      // apparence 2024 (piste séparée, bouts arrondis) ; le drapeau est marqué obsolète en attendant d'être le défaut
      // ignore: deprecated_member_use
      year2023: false,
    ),
    pageTransitionsTheme: const PageTransitionsTheme(builders: {
      TargetPlatform.android: FadeForwardsPageTransitionsBuilder(),
    }),
  );
}
