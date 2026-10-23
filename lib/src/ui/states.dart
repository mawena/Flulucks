import 'dart:async';

import 'package:flutter/material.dart';

import '../strings_fr.dart';
import 'tokens.dart';

/// Centre [child] dans la place disponible, en restant défilable : un texte à 200 % ne déborde pas, et un
/// `RefreshIndicator` parent reçoit le geste de tirer. Sans hauteur bornée (dans une liste), simplement centré.
class _CenteredScroll extends StatelessWidget {
  const _CenteredScroll({required this.child});

  final Widget child;

  @override
  Widget build(BuildContext context) {
    final content = Padding(padding: const EdgeInsets.all(Space.xxl), child: child);
    return LayoutBuilder(builder: (context, c) {
      if (!c.hasBoundedHeight) return Center(child: content);
      return SingleChildScrollView(
        physics: const AlwaysScrollableScrollPhysics(),
        child: ConstrainedBox(
          constraints: BoxConstraints(minHeight: c.maxHeight, minWidth: c.maxWidth),
          child: Center(child: content),
        ),
      );
    });
  }
}

/// Icône dans un cercle tonal, au-dessus d'un titre d'état.
class _StateIcon extends StatelessWidget {
  const _StateIcon(this.icon, {this.danger = false});

  final IconData icon;
  final bool danger;

  @override
  Widget build(BuildContext context) {
    final s = Theme.of(context).colorScheme;
    return ExcludeSemantics(
      child: DecoratedBox(
        decoration: BoxDecoration(color: danger ? s.errorContainer : s.surfaceContainerHigh, shape: BoxShape.circle),
        child: Padding(
          padding: const EdgeInsets.all(Space.l),
          child: Icon(icon, size: 32, color: danger ? s.onErrorContainer : s.onSurfaceVariant),
        ),
      ),
    );
  }
}

/// État vide : icône, titre, explication et action facultatives.
class EmptyState extends StatelessWidget {
  const EmptyState({super.key, required this.icon, required this.title, this.message, this.action});

  final IconData icon;
  final String title;
  final String? message;
  final Widget? action;

  @override
  Widget build(BuildContext context) {
    final t = Theme.of(context).textTheme;
    final s = Theme.of(context).colorScheme;
    return _CenteredScroll(
      child: Column(mainAxisSize: MainAxisSize.min, children: [
        _StateIcon(icon),
        const SizedBox(height: Space.l),
        Text(title, style: t.titleMedium, textAlign: TextAlign.center),
        if (message != null) ...[
          const SizedBox(height: Space.s),
          Text(message!, style: t.bodyMedium?.copyWith(color: s.onSurfaceVariant), textAlign: TextAlign.center),
        ],
        if (action != null) ...[const SizedBox(height: Space.xl), action!],
      ]),
    );
  }
}

/// État d'erreur : le message, et « Réessayer » si [onRetry] est donné.
class ErrorState extends StatelessWidget {
  const ErrorState({super.key, required this.message, this.onRetry});

  final String message;
  final VoidCallback? onRetry;

  @override
  Widget build(BuildContext context) {
    final t = Theme.of(context).textTheme;
    return _CenteredScroll(
      child: Column(mainAxisSize: MainAxisSize.min, children: [
        const _StateIcon(Icons.error_outline_rounded, danger: true),
        const SizedBox(height: Space.l),
        Text(message, style: t.bodyLarge, textAlign: TextAlign.center),
        if (onRetry != null) ...[
          const SizedBox(height: Space.xl),
          FilledButton.tonalIcon(
            onPressed: onRetry,
            icon: const Icon(Icons.refresh_rounded),
            label: const Text(Fr.retry),
          ),
        ],
      ]),
    );
  }
}

/// Chargement : rien pendant [delay] (un dossier rapide ne clignote pas), puis l'indicateur, [label] et [action]
/// (Annuler, par exemple).
class DelayedProgress extends StatefulWidget {
  const DelayedProgress({super.key, this.label, this.action, this.delay = const Duration(milliseconds: 200)});

  final String? label;
  final Widget? action;
  final Duration delay;

  @override
  State<DelayedProgress> createState() => _DelayedProgressState();
}

class _DelayedProgressState extends State<DelayedProgress> {
  Timer? _timer;
  bool _visible = false;

  @override
  void initState() {
    super.initState();
    _timer = Timer(widget.delay, () {
      if (mounted) setState(() => _visible = true);
    });
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!_visible) return const SizedBox.shrink();
    return _CenteredScroll(
      child: Column(mainAxisSize: MainAxisSize.min, children: [
        const CircularProgressIndicator(),
        if (widget.label != null) ...[
          const SizedBox(height: Space.l),
          Text(widget.label!, textAlign: TextAlign.center),
        ],
        if (widget.action != null) ...[const SizedBox(height: Space.s), widget.action!],
      ]),
    );
  }
}

/// Fondu croisé entre états (chargement, vide, erreur, contenu) ; immédiat si les animations sont coupées.
/// Deux états se distinguent par leur type, ou par leur clé.
class StateSwitcher extends StatelessWidget {
  const StateSwitcher({super.key, required this.child});

  final Widget child;

  @override
  Widget build(BuildContext context) => AnimatedSwitcher(
        duration: Motion.of(context, Motion.medium),
        switchInCurve: Motion.standard,
        switchOutCurve: Motion.standard,
        child: child,
      );
}
