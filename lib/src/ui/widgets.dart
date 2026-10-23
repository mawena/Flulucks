import 'package:flutter/material.dart';

import '../strings_fr.dart';
import 'tokens.dart';

/// Rôle d'un état : neutre, bon (disque ouvert), avertissement (lecture seule, FTP en clair), danger.
enum ChipTone { neutral, good, warning, danger }

/// (fond, texte) d'un ton, pris dans le schéma : jamais de couleur fixe.
(Color, Color) toneColors(ChipTone tone, ColorScheme s) => switch (tone) {
      ChipTone.neutral => (s.surfaceContainerHigh, s.onSurfaceVariant),
      ChipTone.good => (s.secondaryContainer, s.onSecondaryContainer),
      ChipTone.warning => (s.tertiaryContainer, s.onTertiaryContainer),
      ChipTone.danger => (s.errorContainer, s.onErrorContainer),
    };

/// En-tête de section : `titleSmall` couleur `primary`, action facultative à droite ; annoncé comme en-tête.
class SectionHeader extends StatelessWidget {
  const SectionHeader(this.title, {super.key, this.trailing});

  final String title;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: EdgeInsets.fromLTRB(Space.l, Space.xl, trailing == null ? Space.l : Space.xs, Space.s),
      child: Row(children: [
        Expanded(
          child: Semantics(
            header: true,
            child: Text(title, style: theme.textTheme.titleSmall?.copyWith(color: theme.colorScheme.primary)),
          ),
        ),
        ?trailing,
      ]),
    );
  }
}

/// Puce d'état (« Chiffré », « Lecture seule »…), colorée par son rôle.
class StatusChip extends StatelessWidget {
  const StatusChip({super.key, required this.label, this.icon, this.tone = ChipTone.neutral});

  final String label;
  final IconData? icon;
  final ChipTone tone;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final (bg, fg) = toneColors(tone, theme.colorScheme);
    return DecoratedBox(
      decoration: BoxDecoration(color: bg, borderRadius: BorderRadius.circular(Radii.s)),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: Space.s, vertical: Space.xs),
        child: Row(mainAxisSize: MainAxisSize.min, children: [
          if (icon != null) ...[ExcludeSemantics(child: Icon(icon, size: 16, color: fg)), const SizedBox(width: Space.xs)],
          Flexible(child: Text(label, style: theme.textTheme.labelMedium?.copyWith(color: fg))),
        ]),
      ),
    );
  }
}

/// Bandeau d'état dans la page (lecture seule, accès aux fichiers, erreur) : icône, texte, actions sous le texte.
class StatusBanner extends StatelessWidget {
  const StatusBanner({
    super.key,
    required this.icon,
    required this.text,
    this.tone = ChipTone.warning,
    this.actions = const [],
  });

  final IconData icon;
  final String text;
  final ChipTone tone;
  final List<Widget> actions;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final (bg, fg) = toneColors(tone, theme.colorScheme);
    return DecoratedBox(
      decoration: BoxDecoration(color: bg, borderRadius: BorderRadius.circular(Radii.m)),
      child: Padding(
        padding: EdgeInsets.fromLTRB(Space.l, Space.m, Space.l, actions.isEmpty ? Space.m : Space.xs),
        child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, mainAxisSize: MainAxisSize.min, children: [
          Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
            ExcludeSemantics(child: Icon(icon, color: fg)),
            const SizedBox(width: Space.m),
            Expanded(child: Text(text, style: theme.textTheme.bodyMedium?.copyWith(color: fg))),
          ]),
          if (actions.isNotEmpty)
            Align(
              alignment: AlignmentDirectional.centerEnd,
              child: Wrap(spacing: Space.s, children: actions),
            ),
        ]),
      ),
    );
  }
}

/// Jauge d'occupation (0 à 1) : `error` à partir de 90 %, annoncée « 75 % utilisés » par TalkBack.
class UsageBar extends StatelessWidget {
  const UsageBar({super.key, required this.used});

  final double used;

  @override
  Widget build(BuildContext context) {
    final s = Theme.of(context).colorScheme;
    final v = used.clamp(0.0, 1.0);
    // valeur annoncée en texte : le rôle « barre de progression » n'accepte qu'un nombre
    return Semantics(
      container: true,
      value: Fr.usedPercent((v * 100).round()),
      child: ExcludeSemantics(child: LinearProgressIndicator(value: v, color: v >= 0.9 ? s.error : s.primary)),
    );
  }
}

/// Contenu centré, limité à [maxWidth] (840 dp pour les listes, 640 pour les formulaires) sur les grands écrans.
class AdaptiveBody extends StatelessWidget {
  const AdaptiveBody({super.key, required this.child, this.maxWidth = Layout.maxList});

  final Widget child;
  final double maxWidth;

  @override
  Widget build(BuildContext context) => Align(
        alignment: Alignment.topCenter,
        child: ConstrainedBox(constraints: BoxConstraints(maxWidth: maxWidth), child: child),
      );
}
