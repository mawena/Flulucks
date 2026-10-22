import '../bridge/fluluks_api.g.dart';

/// Tri de l'explorateur : dossiers toujours en tête, puis selon [sort] (`name`, `size`, `date`).
/// Le nom se compare sans casse ; la taille et la date se départagent par le nom (toujours croissant).
List<EntryMsg> sortEntries(List<EntryMsg> entries, String sort, bool ascending) {
  int byName(EntryMsg a, EntryMsg b) {
    final c = a.name.toLowerCase().compareTo(b.name.toLowerCase());
    return c != 0 ? c : a.name.compareTo(b.name);
  }

  int key(EntryMsg a, EntryMsg b) => switch (sort) {
        'size' => a.size.compareTo(b.size),
        'date' => a.mtime.compareTo(b.mtime),
        _ => byName(a, b),
      };

  final out = List<EntryMsg>.of(entries);
  out.sort((a, b) {
    final da = a.type == 'dir', db = b.type == 'dir';
    if (da != db) return da ? -1 : 1;
    final c = key(a, b);
    if (c != 0) return ascending ? c : -c;
    return byName(a, b);
  });
  return out;
}
