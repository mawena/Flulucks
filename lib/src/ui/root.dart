import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';

/// Racine de l'application, au-dessus de `MaterialApp` : reconstruit l'application quand une préférence
/// d'interface change (thème, retours haptiques) et borne la taille du texte à 200 %, au-delà de laquelle les
/// écrans ne sont pas garantis.
class PortailRoot extends StatelessWidget {
  const PortailRoot({super.key, required this.state, required this.builder});

  final AppState state;

  /// Construit l'application (`FluLuksApp`), relue à chaque changement de préférence d'interface.
  final WidgetBuilder builder;

  @override
  Widget build(BuildContext context) {
    return MediaQuery.withClampedTextScaling(
      maxScaleFactor: 2.0,
      child: ValueListenableBuilder<UiPrefsMsg>(
        valueListenable: state.uiPrefsListenable,
        builder: (context, _, _) => builder(context),
      ),
    );
  }
}
