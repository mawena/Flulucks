import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';

const _images = {'jpg', 'jpeg', 'png', 'gif', 'webp', 'heic', 'heif', 'bmp'};
const _videos = {'mp4', 'mkv', 'avi', 'webm', 'mov', 'm4v', '3gp'};
const _audio = {'mp3', 'flac', 'ogg', 'm4a', 'wav', 'opus', 'aac'};

String _ext(String name) {
  final i = name.lastIndexOf('.');
  return i <= 0 ? '' : name.substring(i + 1).toLowerCase();
}

/// Vrai pour une photo ou une vidéo (vignette demandée à Kotlin).
bool hasThumbnail(EntryMsg e) => e.type == 'file' && (_images.contains(_ext(e.name)) || _videos.contains(_ext(e.name)));

/// Icône du type d'élément.
IconData entryIcon(EntryMsg e) {
  if (e.type == 'dir') return Icons.folder;
  if (e.type == 'link') return Icons.link;
  final x = _ext(e.name);
  if (_images.contains(x)) return Icons.image;
  if (_videos.contains(x)) return Icons.movie;
  if (_audio.contains(x)) return Icons.audiotrack;
  if (x == 'pdf' || x == 'txt' || x == 'epub' || x == 'doc' || x == 'docx') return Icons.description;
  return Icons.insert_drive_file;
}

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

/// Vignette d'une photo ou vidéo, ou l'icône du type (en attendant, ou en cas d'échec).
class EntryThumb extends StatelessWidget {
  const EntryThumb({super.key, required this.state, required this.cache, required this.sourceId, required this.entry, this.size = 40});

  final AppState state;
  final ThumbCache cache;
  final String sourceId;
  final EntryMsg entry;
  final double size;

  @override
  Widget build(BuildContext context) {
    final icon = Icon(entryIcon(entry), size: size * 0.8);
    if (!hasThumbnail(entry)) return SizedBox(width: size, height: size, child: Center(child: icon));
    return SizedBox(
      width: size,
      height: size,
      child: FutureBuilder<Uint8List?>(
        future: cache.get(state, sourceId, entry),
        builder: (context, snap) {
          final b = snap.data;
          if (b == null) return Center(child: icon);
          return ClipRRect(
            borderRadius: BorderRadius.circular(4),
            child: Image.memory(b, fit: BoxFit.cover, gaplessPlayback: true, errorBuilder: (_, _, _) => Center(child: icon)),
          );
        },
      ),
    );
  }
}
