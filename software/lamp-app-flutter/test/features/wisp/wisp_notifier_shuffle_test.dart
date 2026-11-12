import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';

const _lampId = 'shuffle-lamp';
const _wispMac = 'AA:BB:CC:DD:EE:FF';
const _lampA = [0x02, 0, 0, 0, 0, 0x0A];
const _lampB = [0x02, 0, 0, 0, 0, 0x0B];

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

String _status(int seed) =>
    '{"wispMac":"$_wispMac","source":"manual","shuffleSeed":$seed}';

// CHAR_WISP_CLAIMS with the paint section: lamp A painted `a`, lamp B `b`,
// each colour on every base and shade channel.
Uint8List _claims(int a, int b) => Uint8List.fromList([
      2,
      ..._lampA,
      ..._lampB,
      a, a, a, a, a, a,
      b, b, b, b, b, b,
      1, a, a,
      1, b, b,
    ]);

/// When the Shuffle spinner stops: only once the chips show the shuffle,
/// never on a drift step that changes a chip before the wisp re-rolls.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUp(() => SharedPreferences.setMockInitialValues({}));

  late InMemoryBleClient ble;
  late ProviderContainer c;

  Future<void> setClaims(Uint8List blob) => ble.write(
      _lampId, BleUuids.controlService, BleUuids.wispClaims, blob);

  void notifyStatus(int seed) => ble.simulateNotify(
      _lampId, BleUuids.controlService, BleUuids.wispStatus, _b(_status(seed)));

  setUp(() async {
    ble = InMemoryBleClient();
    await ble.connect(_lampId);
    ble.seedSection(_lampId, 'wispstatus', _b(_status(7)));
    await setClaims(_claims(10, 20));
    c = ProviderContainer(overrides: [bleClientProvider.overrideWithValue(ble)]);
    addTearDown(c.dispose);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
  });

  test('a drift step landing before the shuffle keeps the spinner going',
      () async {
    final n = c.read(wispNotifierProvider(_lampId).notifier);
    expect(n.livePaint.length, 2);

    // Lamp B's drift step reaches the cache before the wisp has the op.
    await setClaims(_claims(10, 90));
    await n.shuffle();
    await pumpEventQueue();
    expect(n.colorsRecalculating, isTrue,
        reason: 'the wisp has not re-rolled; seed still 7');

    // The wisp re-rolls: its status reports seed 8, the cache the new colours.
    await setClaims(_claims(40, 50));
    notifyStatus(8);
    await pumpEventQueue();
    expect(n.colorsRecalculating, isFalse);
    expect(n.livePaint.values.map((p) => p.base.r).toSet(), {40, 50});
  });

  test('a status with no known settings is not a moved seed', () async {
    final n = c.read(wispNotifierProvider(_lampId).notifier);
    await setClaims(_claims(10, 90));
    await n.shuffle();
    // A settings-free status from another wisp reads its seed as the default.
    ble.simulateNotify(_lampId, BleUuids.controlService, BleUuids.wispStatus,
        _b('{"wispMac":"11:22:33:44:55:66","configSplit":true,'
            '"source":"manual"}'));
    await pumpEventQueue();
    expect(n.colorsRecalculating, isTrue);
  });

  test('a seed that moved with no chip change waits for one', () async {
    final n = c.read(wispNotifierProvider(_lampId).notifier);
    await n.shuffle();
    notifyStatus(8);
    await pumpEventQueue();
    expect(n.colorsRecalculating, isTrue,
        reason: 'the chips still show the pre-shuffle colours');
    await setClaims(_claims(40, 50));
    notifyStatus(8);
    await pumpEventQueue();
    expect(n.colorsRecalculating, isFalse);
  });
}
