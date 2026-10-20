import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/state/app_state.dart';

import 'fake_host_api.dart';

SourceMsg source(String id, {String kind = 'phone', String label = 'Téléphone'}) => SourceMsg(
      id: id,
      stableKey: id,
      kind: kind,
      label: label,
      totalBytes: 1000,
      freeBytes: 400,
      readOnly: false,
      roReason: '',
    );

void main() {
  test('refresh charge les stockages et l’accès aux fichiers', () async {
    final api = FakeHostApi()
      ..sources = [source('phone')]
      ..allFilesAccess = false;
    final state = AppState(api);
    await state.refresh();
    expect(state.sources.map((s) => s.id), ['phone']);
    expect(state.allFilesAccess, isFalse);
  });

  test('un changement d’état recharge les stockages', () async {
    final api = FakeHostApi()..sources = [source('phone')];
    final state = AppState(api);
    await state.refresh();
    api.sources = [source('phone'), source('v1', kind: 'disk', label: 'Données')];
    state.onStateChanged(const [], [volume()]);
    await pumpEventQueue();
    expect(state.sources.map((s) => s.id), ['phone', 'v1']);
  });

  test('demande d’accès transmise', () async {
    final api = FakeHostApi();
    final state = AppState(api);
    await state.requestAllFilesAccess();
    expect(api.accessRequests, 1);
  });
}
