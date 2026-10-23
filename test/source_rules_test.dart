import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

/// Règles de style de la phase 11, vérifiées sur les sources de `lib/` : les couleurs viennent du thème, les
/// messages passent par `showMessage`, les durées par `Motion`. Les fichiers générés (`*.g.dart`) et les lignes de
/// commentaire (qui peuvent nommer l'API interdite) sont hors du champ.

/// Une ligne fautive : chemin relatif à la racine du paquet, numéro de ligne (à partir de 1), texte.
typedef Hit = ({String path, int line, String text});

/// Lignes de code des fichiers Dart sous [root] qui correspondent à [pattern], hors de [except] (chemins
/// `lib/...`) et des lignes acceptées par [allow].
List<Hit> scan(
  String root,
  RegExp pattern, {
  Set<String> except = const {},
  bool Function(String path, String line)? allow,
}) {
  final hits = <Hit>[];
  final files = Directory(root)
      .listSync(recursive: true)
      .whereType<File>()
      .where((f) => f.path.endsWith('.dart') && !f.path.endsWith('.g.dart'))
      .toList()
    ..sort((a, b) => a.path.compareTo(b.path));
  for (final f in files) {
    final path = f.path.replaceAll(r'\', '/');
    if (except.contains(path)) continue;
    final lines = f.readAsLinesSync();
    for (var i = 0; i < lines.length; i++) {
      final l = lines[i];
      if (l.trimLeft().startsWith('//')) continue;
      if (!pattern.hasMatch(l)) continue;
      if (allow != null && allow(path, l)) continue;
      hits.add((path: path, line: i + 1, text: l.trim()));
    }
  }
  return hits;
}

/// Échoue en listant chaque ligne fautive, avec la règle à suivre.
void expectNone(List<Hit> hits, String rule) {
  expect(hits, isEmpty, reason: '$rule\n${hits.map((h) => '  ${h.path}:${h.line}: ${h.text}').join('\n')}');
}

void main() {
  test('noHardcodedColors', () {
    expectNone(
      scan(
        'lib',
        RegExp(r'\bColors\.'),
        except: {
          'lib/src/ui/theme.dart',
          // lecteurs plein écran : fond noir quel que soit le thème
          'lib/src/screens/image_viewer_screen.dart',
          'lib/src/screens/now_playing_screen.dart',
        },
      ),
      '`Colors.` hors du thème et des lecteurs plein écran : prendre un rôle du ColorScheme '
      '(withValues(alpha:) pour la transparence).',
    );
    expectNone(
      scan('lib', RegExp(r'\bColor\(0x|\bColor\.from(ARGB|RGBO)\('), except: {'lib/src/ui/theme.dart'}),
      'Couleur en valeur hors de ui/theme.dart : prendre un rôle du ColorScheme.',
    );
  });

  test('noRawSnackBars', () {
    expectNone(
      scan('lib', RegExp(r'\bSnackBar\('), except: {'lib/src/ui/feedback.dart'}),
      '`SnackBar(` hors de ui/feedback.dart : passer par showMessage(...).',
    );
  });

  test('noMagicDurations', () {
    bool allowed(String path, String line) => switch (path) {
          // temporisation de la saisie de recherche
          'lib/src/screens/search_screen.dart' ||
          'lib/src/screens/global_search_screen.dart' =>
            line.contains('debounce = Duration(milliseconds: 400)'),
          // barre d'opération : reste affichée 4 s après la fin
          'lib/src/widgets/transfer_bar.dart' => line.contains('Duration(seconds: 4)'),
          _ => false,
        };
    final pattern = RegExp(r'\bDuration\(\s*(microseconds|milliseconds|seconds|minutes|hours|days)\s*:');
    expectNone(
      [
        ...scan('lib/src/screens', pattern, allow: allowed),
        ...scan('lib/src/widgets', pattern, allow: allowed),
      ],
      'Durée en dur dans un écran ou un widget : Motion.short/medium/long (Motion.of(context, …) pour une animation).',
    );
  });
}
