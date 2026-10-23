import 'package:flutter/material.dart';

import '../state/app_state.dart';
import 'transfer_bar.dart';

/// Barres du bas, réunies de haut en bas : action en attente (presse-papiers ou extraction), lecteur réduit,
/// opération en cours. Elles partagent une surface `surfaceContainerHigh`, séparées par un trait, au-dessus de la
/// zone sûre ; rien n'est dessiné quand aucune n'a lieu d'être.
///
/// Le lecteur réduit et l'opération en cours valent pour toute l'application : [PlayerFrame] (`mini_player.dart`)
/// les pose sous le navigateur, donc sous tous les écrans, et se déclare par [DockHost]. Un écran n'y met alors que
/// son action en attente, qui se place juste au-dessus. Sans [DockHost] (écran monté seul), le dock montre aussi
/// l'opération en cours.
class BottomDock extends StatelessWidget {
  const BottomDock({super.key, required this.state, this.pending});

  final AppState state;

  /// Action en attente de l'écran (`ClipboardBar`), montrée tant qu'un presse-papiers ou une archive à extraire
  /// existe.
  final Widget? pending;

  @override
  Widget build(BuildContext context) {
    final hosted = DockHost.maybeOf(context) != null;
    return ListenableBuilder(
      listenable: state,
      builder: (context, _) => DockSurface(children: [
        if (pending != null && (state.clipboard != null || state.pendingExtract != null)) pending!,
        if (!hosted && state.transfer != null) TransferBar(state: state),
      ]),
    );
  }
}

/// Surface commune des barres du bas : un trait au-dessus, un trait entre deux barres, la zone sûre en dessous.
/// Vide : rien du tout (ni marge, ni trait).
class DockSurface extends StatelessWidget {
  const DockSurface({super.key, required this.children});

  final List<Widget> children;

  @override
  Widget build(BuildContext context) {
    if (children.isEmpty) return const SizedBox.shrink();
    return Material(
      color: Theme.of(context).colorScheme.surfaceContainerHigh,
      child: SafeArea(
        top: false,
        child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          for (final c in children) ...[const Divider(height: 1, thickness: 1), c],
        ]),
      ),
    );
  }
}

/// Posé par le cadre de l'application, qui montre lui-même le lecteur réduit et l'opération en cours sous tous les
/// écrans.
class DockHost extends InheritedWidget {
  const DockHost({super.key, required super.child});

  static DockHost? maybeOf(BuildContext context) => context.getInheritedWidgetOfExactType<DockHost>();

  @override
  bool updateShouldNotify(DockHost oldWidget) => false;
}
