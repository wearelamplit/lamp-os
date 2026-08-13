import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/lamp_crypto.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/lamp_shell/application/wifi_notifier.dart';

const _devId = 'lamp-x';

/// Creates a [ProviderContainer] with an in-memory BLE client pre-seeded with
/// [stateJson] for wifiState, and adds an [InventoryLamp] with
/// [controlPassword] so that [WifiNotifier._writeOp] can find the lamp.
Future<ProviderContainer> _seeded(
  String stateJson, {
  String controlPassword = 'secret',
}) async {
  final ble = InMemoryBleClient();
  await ble.connect(_devId);
  await ble.write(
    _devId,
    BleUuids.controlService,
    BleUuids.wifiState,
    Uint8List.fromList(utf8.encode(stateJson)),
  );
  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );
  // Ensure inventory is loaded before we add the lamp.
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(
        InventoryLamp(
            id: _devId, name: 'jacko', controlPassword: controlPassword),
      );
  return c;
}

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  test('build subscribes and parses initial wifiState read', () async {
    final c = await _seeded(
        '{"state":"idle","scanResults":[{"ssid":"home","rssi":-55,"encrypted":true}]}');
    addTearDown(c.dispose);

    final w = await c.read(wifiNotifierProvider(_devId).future);
    expect(w.state, 'idle');
    expect(w.scanResults, hasLength(1));
    expect(w.scanResults.first.ssid, 'home');
    expect(w.scanResults.first.rssi, -55);
    expect(w.scanResults.first.encrypted, isTrue);
  });

  test('scan() writes {op:scan} to wifiOp', () async {
    final c = await _seeded('{"state":"idle"}');
    addTearDown(c.dispose);
    // Prime the provider so the wifiOp characteristic gets written below.
    await c.read(wifiNotifierProvider(_devId).future);

    await c.read(wifiNotifierProvider(_devId).notifier).scan();

    // Verify by reading the characteristic the notifier wrote.
    final ble = c.read(bleClientProvider);
    final written = await ble.read(
        _devId, BleUuids.controlService, BleUuids.wifiOp);
    expect(written[0], LampCrypto.magicCiphertext);
    final plain = await LampCrypto.decryptOpForTesting(
        written,
        password: 'secret',
        saltUuid16: uuidSaltLE16(BleUuids.wifiOp),
        charShortName: 'wifiOp');
    expect(jsonDecode(utf8.decode(plain)), {'op': 'scan'});
  });

  // setHomeSsid() and forget() were removed when home SSID moved to the
  // unified draft model — they now flow through controlNotifier and the
  // global settings_blob save path. wifiOp only carries `scan` now.

  test('notify updates state and preserves scan results', () async {
    final c = await _seeded(
        '{"state":"idle","scanResults":[{"ssid":"home","rssi":-55,"encrypted":true}]}');
    addTearDown(c.dispose);
    // Keep the provider alive so auto-dispose doesn't cancel the subscription
    // before the notify arrives.
    final sub = c.listen(wifiNotifierProvider(_devId), (_, _) {});
    addTearDown(sub.close);
    await c.read(wifiNotifierProvider(_devId).future);

    // Push a notify with state "connected" and NO scanResults. Use
    // simulateNotify rather than write() because production fbp does
    // NOT echo writes to subscribers.
    final ble = c.read(bleClientProvider) as InMemoryBleClient;
    ble.simulateNotify(
      _devId,
      BleUuids.controlService,
      BleUuids.wifiState,
      Uint8List.fromList(utf8.encode(
          '{"state":"connected","ssid":"home","ip":"192.168.1.20"}')),
    );
    // Let the listener fire.
    await Future<void>.delayed(Duration.zero);

    final w = c.read(wifiNotifierProvider(_devId)).value!;
    expect(w.state, 'connected');
    expect(w.ssid, 'home');
    expect(w.ip, '192.168.1.20');
    // Scan results preserved from prior payload.
    expect(w.scanResults, hasLength(1));
    expect(w.scanResults.first.ssid, 'home');
  });

  test('notify with state=failed sets lastError', () async {
    final c = await _seeded('{"state":"connecting"}');
    addTearDown(c.dispose);
    // Keep the provider alive so auto-dispose doesn't cancel the subscription
    // before the notify arrives.
    final sub = c.listen(wifiNotifierProvider(_devId), (_, _) {});
    addTearDown(sub.close);
    await c.read(wifiNotifierProvider(_devId).future);

    final ble = c.read(bleClientProvider) as InMemoryBleClient;
    ble.simulateNotify(
      _devId,
      BleUuids.controlService,
      BleUuids.wifiState,
      Uint8List.fromList(
          utf8.encode('{"state":"failed","lastError":"auth"}')),
    );
    await Future<void>.delayed(Duration.zero);

    final w = c.read(wifiNotifierProvider(_devId)).value!;
    expect(w.state, 'failed');
    expect(w.lastError, 'auth');
  });

  test('falls back to plaintext when controlPassword is null', () async {
    // Seed inventory WITHOUT a controlPassword (factory-default / pre-adoption).
    final ble = InMemoryBleClient();
    await ble.connect(_devId);
    await ble.write(
        _devId, BleUuids.controlService, BleUuids.wifiState,
        Uint8List.fromList(utf8.encode('{"state":"idle"}')));
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    // Inventory is empty initially; add a lamp with NO controlPassword.
    await c.read(inventoryNotifierProvider.future);
    await c.read(inventoryNotifierProvider.notifier).add(
      const InventoryLamp(id: _devId, name: 'naked'),
    );
    // Prime the wifi notifier.
    await c.read(wifiNotifierProvider(_devId).future);

    await c.read(wifiNotifierProvider(_devId).notifier).scan();

    final written = await ble.read(
        _devId, BleUuids.controlService, BleUuids.wifiOp);
    expect(written[0], LampCrypto.magicPlaintext);
    expect(
        jsonDecode(utf8.decode(written.sublist(1))),
        {'op': 'scan'});
  });
}
