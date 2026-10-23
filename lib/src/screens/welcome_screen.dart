import 'package:flutter/material.dart';

import '../state/app_state.dart';
import '../state/prefs.dart';
import '../strings_fr.dart';
import '../ui/tokens.dart';
import '../ui/widgets.dart';
import '../widgets/brand_mark.dart';

/// Bienvenue, montrée une seule fois (premier lancement, ou mise à jour depuis la v1.3.0) : ce que fait Portail, la
/// confidentialité de la phrase de passe et l'accès aux fichiers, facultatif pour les disques USB.
class WelcomeScreen extends StatelessWidget {
  const WelcomeScreen({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Scaffold(
      body: SafeArea(
        child: AdaptiveBody(
          maxWidth: Layout.maxForm,
          child: ListenableBuilder(
            listenable: state, // accès accordé au retour de l'écran Android
            builder: (context, _) => ListView(padding: const EdgeInsets.all(Space.xl), children: [
              const SizedBox(height: Space.xxl),
              const Center(child: BrandMark(size: 96)),
              const SizedBox(height: Space.xl),
              Semantics(
                header: true,
                child: Text(Fr.welcomeTitle, style: theme.textTheme.headlineMedium, textAlign: TextAlign.center),
              ),
              const SizedBox(height: Space.xxl),
              const _Point(icon: Icons.usb_rounded, text: Fr.welcomeDisks),
              const _Point(icon: Icons.lock_rounded, text: Fr.welcomePrivacy),
              const _Point(icon: Icons.folder_rounded, text: Fr.welcomeAccess),
              const SizedBox(height: Space.xl),
              if (state.allFilesAccess)
                const Center(
                  child: StatusChip(label: Fr.allFilesGranted, icon: Icons.check_rounded, tone: ChipTone.good),
                )
              else
                OutlinedButton(onPressed: state.requestAllFilesAccess, child: const Text(Fr.grantAccess)),
              const SizedBox(height: Space.m),
              FilledButton(
                // sans l'accès, les disques USB restent utilisables : la carte d'accès de l'accueil le redemande
                onPressed: () => state.setUiPrefs(state.uiPrefs.copyWith(welcomeSeen: true)),
                child: const Text(Fr.start),
              ),
            ]),
          ),
        ),
      ),
    );
  }
}

/// Un point de la bienvenue : icône dans une pastille, puis le texte.
class _Point extends StatelessWidget {
  const _Point({required this.icon, required this.text});

  final IconData icon;
  final String text;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final s = theme.colorScheme;
    return Padding(
      padding: const EdgeInsets.only(bottom: Space.l),
      child: Row(children: [
        ExcludeSemantics(
          child: DecoratedBox(
            decoration: BoxDecoration(color: s.primaryContainer, borderRadius: BorderRadius.circular(Radii.m)),
            child: Padding(
              padding: const EdgeInsets.all(Space.m),
              child: Icon(icon, color: s.onPrimaryContainer),
            ),
          ),
        ),
        const SizedBox(width: Space.l),
        Expanded(child: Text(text, style: theme.textTheme.bodyLarge)),
      ]),
    );
  }
}
