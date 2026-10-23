import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../file_types.dart';
import '../state/app_state.dart';
import '../ui/tokens.dart';

/// Cache des vignettes d'un écran : 200 entrées au plus, la plus ancienne sort. Au plus [parallel]
/// demandes à Kotlin à la fois, la plus récente d'abord (la vignette qu'on regarde) ; [dispose] abandonne
/// celles qui attendent encore (dossier quitté).
class ThumbCache {
  static const parallel = 3;

  final _m = <String, Future<Uint8List?>>{};
  final _waiting = <(Completer<Uint8List?>, Future<Uint8List?> Function())>[];
  var _inFlight = 0;
  var _disposed = false;

  Future<Uint8List?> get(AppState state, String sourceId, EntryMsg e) {
    final key = '$sourceId|${e.path}|${e.size}|${e.mtime}';
    final hit = _m.remove(key);
    if (hit != null) return _m[key] = hit;
    if (_m.length >= 200) _m.remove(_m.keys.first);
    final done = Completer<Uint8List?>();
    _waiting.add((done, () => state.api.thumbnail(sourceId, e.path, 256)));
    _pump();
    return _m[key] = done.future;
  }

  void _pump() {
    while (!_disposed && _inFlight < parallel && _waiting.isNotEmpty) {
      final (done, request) = _waiting.removeLast();
      _inFlight++;
      request().catchError((Object _) => null).then((b) {
        _inFlight--;
        done.complete(b);
        _pump();
      });
    }
  }

  void dispose() {
    _disposed = true;
    for (final (done, _) in _waiting) {
      done.complete(null);
    }
    _waiting.clear();
  }
}

/// Icône du type de [entry] dans un carré arrondi teinté par sa couleur de rôle.
class KindIcon extends StatelessWidget {
  const KindIcon({super.key, required this.entry, this.size = 40, this.radius = Radii.s});

  final EntryMsg entry;
  final double size;
  final double radius;

  @override
  Widget build(BuildContext context) {
    final k = kindOf(entry);
    final c = kindColor(k, Theme.of(context).colorScheme);
    return SizedBox.square(
      dimension: size,
      child: DecoratedBox(
        decoration: BoxDecoration(color: c.withValues(alpha: 0.12), borderRadius: BorderRadius.circular(radius)),
        child: Icon(kindIcon(k), color: c, size: size * 0.6),
      ),
    );
  }
}

/// Vignette d'une photo ou vidéo, ou l'icône du type (en attendant, ou en cas d'échec).
class EntryThumb extends StatelessWidget {
  const EntryThumb({super.key, required this.state, required this.cache, required this.sourceId, required this.entry, this.size = 40,
      this.radius = Radii.s});

  final AppState state;
  final ThumbCache cache;
  final String sourceId;
  final EntryMsg entry;
  final double size;

  /// Rayon des coins : 8 en liste, 12 en grille.
  final double radius;

  @override
  Widget build(BuildContext context) {
    final icon = KindIcon(entry: entry, size: size, radius: radius);
    if (!hasThumbnail(entry)) return icon;
    return SizedBox.square(
      dimension: size,
      child: FutureBuilder<Uint8List?>(
        future: cache.get(state, sourceId, entry),
        builder: (context, snap) {
          final b = snap.data;
          if (b == null) return icon;
          return ClipRRect(
            borderRadius: BorderRadius.circular(radius),
            child: Image.memory(b, fit: BoxFit.cover, gaplessPlayback: true, errorBuilder: (_, _, _) => icon),
          );
        },
      ),
    );
  }
}
