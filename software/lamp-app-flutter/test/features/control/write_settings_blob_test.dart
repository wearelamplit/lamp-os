import 'dart:convert';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/core/ble/lamp_crypto.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/seed.dart';

const _devId = 'lamp-a';

Future<ProviderContainer> _buildContainer({
  InMemoryBleClient? ble,
  String controlPassword = '',
}) async {
  final client = ble ?? InMemoryBleClient();
  await seedControlBle(client, deviceId: _devId, brightness: 50);

  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(client)],
  );

  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(InventoryLamp(
        id: _devId,
        name: 'test-lamp',
        controlPassword: controlPassword.isEmpty ? null : controlPassword,
      ));

  return c;
}

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  group('controlNotifier.writeSettingsBlob', () {
    test('includes reboot:false flag when reboot:false is passed', () async {
      final ble = InMemoryBleClient();
      final c = await _buildContainer(ble: ble);
      addTearDown(c.dispose);

      // Keep a listener alive so the provider stays connected.
      final sub = c.listen(controlNotifierProvider(_devId), (_, _) {});
      addTearDown(sub.close);
      await c.read(controlNotifierProvider(_devId).future);

      final n = c.read(controlNotifierProvider(_devId).notifier);
      await n.writeSettingsBlob({'lamp': <String, dynamic>{'name': 'foo'}},
          reboot: false);

      final writes = ble.writesTo(_devId, BleUuids.settingsBlob);
      expect(writes.length, 1);

      // No-password path: byte[0] is magicPlaintext (0x01), rest is JSON.
      final write = writes.first;
      expect(write[0], LampCrypto.magicPlaintext);
      final json =
          jsonDecode(utf8.decode(write.sublist(1))) as Map<String, dynamic>;
      expect(json['reboot'], false);
      expect((json['lamp'] as Map<String, dynamic>)['name'], 'foo');
    });

    test('defaults reboot to true when not specified', () async {
      final ble = InMemoryBleClient();
      final c = await _buildContainer(ble: ble);
      addTearDown(c.dispose);

      final sub = c.listen(controlNotifierProvider(_devId), (_, _) {});
      addTearDown(sub.close);
      await c.read(controlNotifierProvider(_devId).future);

      final n = c.read(controlNotifierProvider(_devId).notifier);
      // Reboot:true means firmware disconnects — InMemoryBleClient won't
      // actually throw, so this is a clean write here.
      await n.writeSettingsBlob({'lamp': <String, dynamic>{'name': 'bar'}});

      final writes = ble.writesTo(_devId, BleUuids.settingsBlob);
      expect(writes.length, 1);

      final write = writes.first;
      expect(write[0], LampCrypto.magicPlaintext);
      final json =
          jsonDecode(utf8.decode(write.sublist(1))) as Map<String, dynamic>;
      expect(json['reboot'], true);
      expect((json['lamp'] as Map<String, dynamic>)['name'], 'bar');
    });

    test('reboot flag is merged alongside caller-provided fields', () async {
      final ble = InMemoryBleClient();
      final c = await _buildContainer(ble: ble);
      addTearDown(c.dispose);

      final sub = c.listen(controlNotifierProvider(_devId), (_, _) {});
      addTearDown(sub.close);
      await c.read(controlNotifierProvider(_devId).future);

      final n = c.read(controlNotifierProvider(_devId).notifier);
      await n.writeSettingsBlob(
          {'lamp': <String, dynamic>{'brightness': 75}},
          reboot: false);

      final writes = ble.writesTo(_devId, BleUuids.settingsBlob);
      expect(writes.length, 1);

      final write = writes.first;
      final json =
          jsonDecode(utf8.decode(write.sublist(1))) as Map<String, dynamic>;
      // Both caller field and reboot flag present.
      expect(json.containsKey('lamp'), isTrue);
      expect(json.containsKey('reboot'), isTrue);
      expect(json['reboot'], false);
    });
  });
}
