import 'package:flutter/material.dart';

import '../strings_fr.dart';
import '../ui/tokens.dart';
import '../ui/widgets.dart';

/// Aide : les questions courantes, chacune dépliable, avec une réponse courte tirée du README.
class HelpScreen extends StatelessWidget {
  const HelpScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Scaffold(
      appBar: AppBar(title: const Text(Fr.help)),
      body: AdaptiveBody(
        child: ListView(padding: const EdgeInsets.symmetric(vertical: Space.s), children: [
          for (final (question, answer) in Fr.helpEntries)
            ExpansionTile(
              title: Text(question, style: theme.textTheme.titleMedium),
              shape: const Border(), // pas de traits au-dessus et au-dessous d'une réponse dépliée
              collapsedShape: const Border(),
              expandedCrossAxisAlignment: CrossAxisAlignment.start,
              childrenPadding: const EdgeInsets.fromLTRB(Space.l, 0, Space.l, Space.l),
              children: [
                Text(answer, style: theme.textTheme.bodyMedium?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
              ],
            ),
        ]),
      ),
    );
  }
}
