import 'package:flutter/material.dart';

/// Jetons de la charte (phase 11) : aucune marge, aucun rayon ni aucune durée n'est codé en dur ailleurs.

/// Espacements, sur une grille de 4 dp.
abstract final class Space {
  static const xs = 4.0, s = 8.0, m = 12.0, l = 16.0, xl = 24.0, xxl = 32.0;
}

/// Rayons : puces et vignettes, champs et tuiles, cartes, dialogues et feuilles.
abstract final class Radii {
  static const s = 8.0, m = 12.0, l = 16.0, xl = 28.0;
}

/// Durées et courbes. Toute animation passe par [of], qui la coupe si Android coupe les animations.
abstract final class Motion {
  /// Petits changements d'état (sélection d'une tuile).
  static const short = Duration(milliseconds: 150);

  /// Fondus entre états, apparitions.
  static const medium = Duration(milliseconds: 250);

  /// Pages.
  static const long = Duration(milliseconds: 400);

  static const standard = Easing.standard, enter = Easing.emphasizedDecelerate, exit = Easing.emphasizedAccelerate;

  /// [d], ou une durée nulle si les animations sont coupées.
  static Duration of(BuildContext context, Duration d) => MediaQuery.disableAnimationsOf(context) ? Duration.zero : d;
}

/// Largeurs de mise en page : compact < 600 dp, moyen jusqu'à 839 dp, étendu au-delà.
abstract final class Layout {
  static const medium = 600.0, expanded = 840.0;

  /// Largeur maximale des listes, et des formulaires.
  static const maxList = 840.0, maxForm = 640.0;

  /// Grille d'éléments : 3 colonnes sur le Pixel 4 XL (412 dp).
  static const gridMaxExtent = 132.0, gridSpacing = 8.0, gridAspect = 0.78;

  /// Cible tactile minimale.
  static const minTouch = 48.0;
}

enum WindowClass { compact, medium, expanded }

WindowClass windowClassOf(BuildContext context) {
  final w = MediaQuery.sizeOf(context).width;
  if (w < Layout.medium) return WindowClass.compact;
  if (w < Layout.expanded) return WindowClass.medium;
  return WindowClass.expanded;
}

/// Chiffres tabulaires (tailles, pourcentages, durées, compteurs) : les colonnes ne dansent pas.
TextStyle numeric(TextStyle s) =>
    s.copyWith(fontFeatures: [...?s.fontFeatures, const FontFeature.tabularFigures()]);
