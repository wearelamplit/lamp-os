import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';

/// Notifier-level coverage for the Phase-F WiFi config wireup.
///
/// The setWifi path is a pure pass-through to the repository (no
/// optimistic state mutation — see WispNotifier.setWifi doc), so we just
/// need to verify the BLE write produced the expected JSON envelope.
void main() {
  const lampId = 'test-lamp';

  ProviderContainer makeContainer({InMemoryBleClient? ble}) {
    final client = ble ?? InMemoryBleClient();
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(client)],
    );
    addTearDown(c.dispose);
    return c;
  }

  Future<void> primeStatus(
    InMemoryBleClient ble,
    String json,
  ) async {
    await ble.connect(lampId);
    await ble.write(
      lampId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      Uint8List.fromList(utf8.encode(json)),
    );
  }

  group('WispNotifier setWifi', () {
    test('writes a setWifi wispOp with ssid + pw fields', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      await n.setWifi('homenet', 'sup3rs3cret');

      final written = await ble.read(
        lampId,
        BleUuids.controlService,
        BleUuids.wispOp,
      );
      final decoded =
          jsonDecode(utf8.decode(written)) as Map<String, dynamic>;
      expect(decoded['char'], 'wispOp');
      expect(decoded['op'], 'setWifi');
      expect(decoded['ssid'], 'homenet');
      expect(decoded['pw'], 'sup3rs3cret');
    });

    test('does NOT optimistically flip wifiConnected to true', () async {
      // Rationale: a wrong-password / out-of-range save would leave a
      // sticky "Connected" badge with nothing to reconcile it. The
      // chip mirrors only what the wisp reports via wispStatus.
      final ble = InMemoryBleClient();
      await primeStatus(
        ble,
        '{"wispMac":"AA:BB:CC:DD:EE:FF","wifiConnected":false}',
      );
      final c = makeContainer(ble: ble);

      final initial = await c.read(wispNotifierProvider(lampId).future);
      expect(initial.wifiConnected, isFalse);

      final n = c.read(wispNotifierProvider(lampId).notifier);
      await n.setWifi('homenet', 'whatever');

      final after = c.read(wispNotifierProvider(lampId)).value!;
      expect(after.wifiConnected, isFalse,
          reason: 'No optimistic flip; status mirrors what the wisp reports.');
    });

    test('rethrows BLE write failures so the UI can show an error',
        () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      // Force the next write to fail. Disconnecting the fake client
      // surfaces a StateError from the in-memory client's write path,
      // matching how a real BLE write would throw on a torn-down link.
      final c = makeContainer(ble: ble);
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      await ble.disconnect(lampId);
      expect(
        () => n.setWifi('homenet', 'x'),
        throwsA(isA<Object>()),
      );
    });
  });
}
