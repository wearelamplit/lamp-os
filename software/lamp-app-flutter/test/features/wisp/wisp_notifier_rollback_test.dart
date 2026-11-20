import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../_support/in_memory_ble_client.dart';
import '../../_support/wisp_palette_ble.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';

/// When a wispOp write rejects, optimistic state must roll back to its
/// pre-call value. Failed writes have no incoming notify to reconcile
/// the chip / pill / radio.
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
    test('setOscRole reverts optimistic role on BLE failure', () async {
      final ble = InMemoryBleClient();
      await primeStatus(
        ble,
        '{"wispMac":"AA:BB:CC:DD:EE:FF","configSplit":true,"source":"osc",'
        '"oscRole":"secondary"}',
      );
      final c = makeContainer(ble);

      final initial = await c.read(wispNotifierProvider(lampId).future);
      expect(initial.oscRole, OscRole.secondary);

      // Tear the link down so the next write rejects.
      await ble.disconnect(lampId);

      final n = c.read(wispNotifierProvider(lampId).notifier);
      await expectLater(
        () => n.setOscRole(OscRole.fill),
        throwsA(isA<Object>()),
      );

      final after = c.read(wispNotifierProvider(lampId)).value!;
      expect(after.oscRole, OscRole.secondary,
          reason: 'optimistic role flip reverted');
    });

    // The op reaches the wisp and its echo lands, but the write call reports
    // an error (a lost response).
    test('setSource sends no second copy of an op the wisp echoed before its '
        'write reported an error', () async {
      final ble = PaletteBle(lampId);
      final (_, n) = await openWisp(ble, '{"wispMac":"$wispA","configSplit":true,"source":"osc"}');

      ble.holdSourceWrite = Completer<void>();
      final result = n.setSource(WispSourceMode.off).then<Object?>(
            (_) => null,
            onError: (Object e) => e,
          );
      await pumpEventQueue();
      ble.notifyStatus('{"wispMac":"$wispA","configSplit":true,"source":"off"}');
      await pumpEventQueue();
      ble.holdSourceWrite!.completeError(Exception('response lost'));
      ble.holdSourceWrite = null;

      expect(await result, isNull);
      expect(n.state.value!.source, WispSourceMode.off);
      final sourceWrites = ble
          .writesTo(lampId, BleUuids.wispOp)
          .where((w) => utf8.decode(w).contains('setSource'));
      expect(sourceWrites, hasLength(1), reason: 'the op landed; no resend');
    });

    test('setSource reverts source pill on BLE failure', () async {
      final ble = InMemoryBleClient();
      await primeStatus(
        ble,
        '{"wispMac":"AA:BB:CC:DD:EE:FF","configSplit":true,"source":"osc"}',
      );
      final c = makeContainer(ble);

      final initial = await c.read(wispNotifierProvider(lampId).future);
      expect(initial.source, WispSourceMode.osc);

      await ble.disconnect(lampId);

      final n = c.read(wispNotifierProvider(lampId).notifier);
      await expectLater(
        () => n.setSource(WispSourceMode.off),
        throwsA(isA<Object>()),
      );

      final after = c.read(wispNotifierProvider(lampId)).value!;
      expect(after.source, WispSourceMode.osc,
          reason: 'optimistic source mode flip reverted');
    });
  });
}
