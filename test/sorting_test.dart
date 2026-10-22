import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/state/sorting.dart';

EntryMsg e(String name, {String type = 'file', int size = 0, int mtime = 0}) =>
    EntryMsg(name: name, path: '/$name', type: type, size: size, mtime: mtime);

List<String> names(List<EntryMsg> l) => l.map((x) => x.name).toList();

void main() {
  test('dirsFirstThenNameCaseInsensitive', () {
    final l = [e('b.txt'), e('A', type: 'dir'), e('a.txt'), e('c', type: 'dir')];
    expect(names(sortEntries(l, 'name', true)), ['A', 'c', 'a.txt', 'b.txt']);
    expect(names(sortEntries(l, 'name', false)), ['c', 'A', 'b.txt', 'a.txt']);
  });

  test('sizeDescendingTiesByName', () {
    final l = [e('b', size: 5), e('a', size: 5), e('c', size: 9), e('D', type: 'dir'), e('z', size: 1)];
    expect(names(sortEntries(l, 'size', false)), ['D', 'c', 'a', 'b', 'z']);
  });

  test('dateAscending', () {
    final l = [e('x', mtime: 30), e('y', mtime: 10), e('w', mtime: 20)];
    expect(names(sortEntries(l, 'date', true)), ['y', 'w', 'x']);
  });

  test('valeur inconnue : par nom', () {
    expect(names(sortEntries([e('b'), e('a')], 'couleur', true)), ['a', 'b']);
  });
}
