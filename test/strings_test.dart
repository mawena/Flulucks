import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/strings_fr.dart';

/// Texte des littéraux de chaîne d'un source Dart, sans le code interpolé (`${…}`) ni les commentaires : seules
/// les parties visibles comptent (un `a ? b : c` du code n'est pas de la typographie).
List<String> stringLiterals(String src) => (_Scanner(src)..code(0, false)).out;

class _Scanner {
  _Scanner(this.src);

  final String src;
  final out = <String>[];

  /// Chaîne qui commence en [i] (après un éventuel `r`) ; rend l'indice qui suit.
  int string(int i, bool raw) {
    final q = src[i];
    final end = src.startsWith(q * 3, i) ? q * 3 : q;
    i += end.length;
    final buf = StringBuffer();
    while (i < src.length && !src.startsWith(end, i)) {
      final c = src[i];
      if (!raw && c == r'\') {
        buf.write(src.substring(i, i + 2));
        i += 2;
      } else if (!raw && src.startsWith(r'${', i)) {
        out.add(buf.toString());
        buf.clear();
        i = code(i + 2, true);
      } else {
        buf.write(c);
        i++;
      }
    }
    out.add(buf.toString());
    return i + end.length;
  }

  /// Code jusqu'à l'accolade fermante de même profondeur si [inner] ; rend l'indice qui suit.
  int code(int i, bool inner) {
    var depth = 0;
    while (i < src.length) {
      final c = src[i];
      if (src.startsWith('//', i)) {
        i = src.indexOf('\n', i);
        if (i < 0) return src.length;
      } else if (c == "'" || c == '"') {
        i = string(i, false);
      } else if (c == 'r' && i + 1 < src.length && (src[i + 1] == "'" || src[i + 1] == '"') &&
          (i == 0 || !RegExp(r'\w').hasMatch(src[i - 1]))) {
        i = string(i + 1, true);
      } else if (c == '{') {
        depth++;
        i++;
      } else if (c == '}') {
        if (depth == 0 && inner) return i + 1;
        depth--;
        i++;
      } else {
        i++;
      }
    }
    return i;
  }
}

void main() {
  test('pluralFrench', () {
    expect(Fr.plural(0, 'élément', 'éléments'), 'élément');
    expect(Fr.plural(1, 'élément', 'éléments'), 'élément');
    expect(Fr.plural(2, 'élément', 'éléments'), 'éléments');
    expect(Fr.count(0, 'fichier', 'fichiers'), '0 fichier');
    expect(Fr.count(1, 'fichier', 'fichiers'), '1 fichier');
    expect(Fr.count(3, 'fichier', 'fichiers'), '3 fichiers');
  });

  test('humanSizeUnits', () {
    expect(Fr.humanSize(0), '0 octet');
    expect(Fr.humanSize(1), '1 octet');
    expect(Fr.humanSize(512), '512 octets');
    expect(Fr.humanSize(1500), '1,5 ko');
    expect(Fr.humanSize(250000000), '250,0 Mo');
    expect(Fr.humanSize(1500000000), '1,5 Go');
    expect(Fr.humanSize(2000000000000), '2,0 To');
    expect(Fr.humanSize(999960), '1,0 Mo'); // l'arrondi passe à l'unité suivante, jamais « 1000,0 ko »
    expect(Fr.humanSize(-1), 'taille inconnue');
  });

  test('friendlyDateRelative', () {
    final now = DateTime(2026, 10, 23, 15, 0);
    int s(DateTime d) => d.millisecondsSinceEpoch ~/ 1000;
    expect(Fr.friendlyDate(s(DateTime(2026, 10, 23, 14, 5)), now: now), "Aujourd'hui, 14:05");
    expect(Fr.friendlyDate(s(DateTime(2026, 10, 22, 9, 12)), now: now), 'Hier, 09:12');
    expect(Fr.friendlyDate(s(DateTime(2026, 3, 3, 14, 5)), now: now), '3 mars, 14:05');
    expect(Fr.friendlyDate(s(DateTime(2026, 8, 1, 7, 0)), now: now), '1 août, 07:00');
    expect(Fr.friendlyDate(s(DateTime(2025, 3, 3, 14, 5)), now: now), '03/03/2025');
    // 1er janvier : la veille est l'année précédente
    expect(Fr.friendlyDate(s(DateTime(2025, 12, 31, 23, 0)), now: DateTime(2026, 1, 1, 8)), 'Hier, 23:00');
  });

  test('revisedTexts', () {
    const nb = ' ', fine = ' ';
    expect(Fr.clipboard(3, false), '3 éléments à copier');
    expect(Fr.clipboard(1, true), '1 élément à déplacer');
    expect(Fr.trashSummary(2, 3000000), '2 éléments, 3,0 Mo');
    expect(Fr.spaceTotal(100000000, 3), '100,0 Mo — 3 fichiers');
    expect(Fr.transferLine('copy', 'photo.jpg', 2, 5, 34), 'Copie de photo.jpg — 2 sur 5 · 34$fine%');
    expect(Fr.selected(3), '3 sélectionnés');
    expect(Fr.selected(1), '1 sélectionné');
    expect(Fr.deleteOne('a.txt'), 'Supprimer «${nb}a.txt$nb»$fine?');
    expect(Fr.percent(75), '75$fine%');
    expect(Fr.noDiskTitle, 'Aucun disque branché');
    expect(Fr.noDiskBody, 'Branchez un disque USB-C, chiffré LUKS ou non$nb: ext4, NTFS, exFAT et FAT sont reconnus.');
  });

  test('literalScanner', () {
    final l = stringLiterals(r'''
// commentaire d'exemple : ignoré
const a = 'un ${b ? 'deux' : 'trois'} quatre';
final c = r'brut $x';
''');
    expect(l, ['un ', 'deux', 'trois', ' quatre', r'brut $x']);
  });

  test('frenchTypography', () {
    final literals = stringLiterals(File('lib/src/strings_fr.dart').readAsStringSync());
    expect(literals, contains('Portail'));
    const banned = [' ?', ' !', ' :', ' ;', ' %', '« ', ' »', '(s)'];
    final faults = [
      for (final l in literals)
        for (final b in banned)
          if (l.contains(b)) '« $b » dans : $l',
    ];
    expect(faults, isEmpty);
  });
}
