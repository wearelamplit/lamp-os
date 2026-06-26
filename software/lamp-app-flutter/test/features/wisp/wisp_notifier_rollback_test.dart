import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';
import 'package:lamp_app/features/wisp/domain/zone_source.dart';

/// Audit perf-M8: when a wispOp write rejects, the optimistic state we
/// painted before the write needs to roll back to its pre-call value.
/// Without rollback, the chip / pill / radio shows the failed selection
/// even though the wisp never received the op — and no notify is coming
/// to reconcile it.
void main() {
  const lampId = 'rollback-lamp';

  ProviderContainer makeContainer(InMemoryBleClient ble) {
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    return c;
  }

  Future<void> primeStatus(InMemoryBleClient ble, String json) async {
    await ble.connect(lampId);
    await ble.write(
      lampId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      Uint8List.fromList(utf8.encode(json)),
    );
  }

  group('WispNotifier rollback on write failure', () {
    test('setZone reverts optimistic zone+source on BLE failure', () async {
      final ble = InMemoryBleClient();
      await primeStatus(
        ble,
        '{"wispMac":"AA:BB:CC:DD:EE:FF","currentZone":3,'
        '"zoneSource":"firstSeen"}',
      );
      final c = makeContainer(ble);

      final initial = await c.read(wispNotifierProvider(lampId).future);
      expect(initial.currentZone, 3);
      expect(initial.zoneSource, ZoneSource.firstSeen);

      // Tear the link down so the next write rejects.
      await ble.disconnect(lampId);

      final n = c.read(wispNotifierProvider(lampId).notifier);
      await expectLater(() => n.setZone(7), throwsA(isA<Object>()));

      final after = c.read(wispNotifierProvider(lampId)).value!;
      expect(after.currentZone, 3, reason: 'optimistic zone bump reverted');
      expect(after.zoneSource, ZoneSource.firstSeen,
          reason: 'optimistic source flip reverted');
    });

    test('clearZone reverts zoneSource on BLE failure', () async {
      final ble = InMemoryBleClient();
      await primeStatus(
        ble,
        '{"wispMac":"AA:BB:CC:DD:EE:FF","currentZone":3,'
        '"zoneSource":"appOp"}',
      );
      final c = makeContainer(ble);

      await c.read(wispNotifierProvider(lampId).future);
      await ble.disconnect(lampId);

      final n = c.read(wispNotifierProvider(lampId).notifier);
      await expectLater(() => n.clearZone(), throwsA(isA<Object>()));

      final after = c.read(wispNotifierProvider(lampId)).value!;
      expect(after.zoneSource, ZoneSource.appOp,
          reason: 'optimistic source flip reverted');
    });

    test('setSource reverts source pill on BLE failure', () async {
      final ble = InMemoryBleClient();
      await primeStatus(
        ble,
        '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"aurora"}',
      );
      final c = makeContainer(ble);

      final initial = await c.read(wispNotifierProvider(lampId).future);
      expect(initial.source, WispSourceMode.aurora);

      await ble.disconnect(lampId);

      final n = c.read(wispNotifierProvider(lampId).notifier);
      await expectLater(
        () => n.setSource(WispSourceMode.off),
        throwsA(isA<Object>()),
      );

      final after = c.read(wispNotifierProvider(lampId)).value!;
      expect(after.source, WispSourceMode.aurora,
          reason: 'optimistic source mode flip reverted');
    });
  });
}
