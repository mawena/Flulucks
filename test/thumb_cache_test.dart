import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/state/app_state.dart';
import 'package:fluluks/src/widgets/thumb.dart';

import 'fake_host_api.dart';

/// Vignettes qui ne répondent qu'à la demande du test.
class SlowThumbs extends FakeHostApi {
  final pending = <String, Completer<Uint8List?>>{};

  @override
  Future<Uint8List?> thumbnail(String sourceId, String path, int sizePx) {
    thumbRequests.add(path);
    return (pending[path] = Completer<Uint8List?>()).future;
  }
}

EntryMsg photo(int i) => EntryMsg(name: 'p$i.jpg', path: '/p$i.jpg', type: 'file', size: 10, mtime: 1);

void main() {
  // Relecture finale : un dossier de milliers de photos ne doit pas noyer Kotlin de demandes.
  test('thumbnailsAtMostThreeInFlightNewestFirst', () async {
    final api = SlowThumbs();
    final state = AppState(api);
    final cache = ThumbCache();
    final futures = [for (var i = 0; i < 10; i++) cache.get(state, 'v1', photo(i))];
    await Future<void>.delayed(Duration.zero);
    expect(api.thumbRequests, ['/p0.jpg', '/p1.jpg', '/p2.jpg']);
    api.pending['/p0.jpg']!.complete(null);
    await Future<void>.delayed(Duration.zero);
    expect(api.thumbRequests.last, '/p9.jpg'); // la plus récente : celle qu'on regarde
    expect(api.thumbRequests.length, 4);
    expect(await futures[0], isNull);
  });

  test('disposeDropsQueuedRequests', () async {
    final api = SlowThumbs();
    final state = AppState(api);
    final cache = ThumbCache();
    final queued = [for (var i = 0; i < 6; i++) cache.get(state, 'v1', photo(i))];
    await Future<void>.delayed(Duration.zero);
    cache.dispose();
    for (final c in api.pending.values) {
      c.complete(null);
    }
    await Future<void>.delayed(Duration.zero);
    expect(api.thumbRequests.length, 3); // les 3 en attente ne partent jamais
    expect(await queued[5], isNull);
  });
}
