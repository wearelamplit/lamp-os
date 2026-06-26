import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/seed.dart';

const _devId = 'lamp-a';
const _pw = 'hunter2';

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  group('writeSettingsBlob nonce-non-reuse (B.8 regression guard)', () {
    test(
        'two near-simultaneous discrete edits produce distinct ciphertexts',
        () async {
      // Use a PASSWORDED lamp so the encrypted path (AesGcm with random
      // nonce) is exercised. The no-password path is deterministic JSON
      // and would trivially differ due to different map content anyway.
      final ble = InMemoryBleClient();
      await seedControlBle(ble, deviceId: _devId, brightness: 50);

      final c = ProviderContainer(
        overrides: [bleClientProvider.overrideWithValue(ble)],
      );
      addTearDown(c.dispose);

      await c.read(inventoryNotifierProvider.future);
      await c.read(inventoryNotifierProvider.notifier).add(
            const InventoryLamp(
              id: _devId,
              name: 'test-lamp',
              controlPassword: _pw,
            ),
          );

      // Keep a listener alive so the provider stays connected.
      final sub = c.listen(controlNotifierProvider(_devId), (_, _) {});
      addTearDown(sub.close);
      await c.read(controlNotifierProvider(_devId).future);

      final n = c.read(controlNotifierProvider(_devId).notifier);

      // Fire two writeSettingsBlob calls back-to-back.
      // Both use reboot:false so there's no expected disconnect to swallow.
      await Future.wait([
        n.writeSettingsBlob(
          {'lamp': <String, dynamic>{'name': 'A'}},
          reboot: false,
        ),
        n.writeSettingsBlob(
          {'lamp': <String, dynamic>{'socialMode': 1}},
          reboot: false,
        ),
      ]);

      final writes = ble.writesTo(_devId, BleUuids.settingsBlob);
      expect(writes.length, 2,
          reason: 'both writeSettingsBlob calls should produce a write');

      // Distinct ciphertext bytes — if package:cryptography ever switches
      // to deterministic nonces (e.g. counter-based) this test fails
      // loudly, which is exactly the desired forward-defense behaviour.
      expect(
        writes[0],
        isNot(equals(writes[1])),
        reason:
            'ciphertexts must differ: AesGcm must use a fresh random nonce '
            'per call (B.8 invariant). If this fails, a nonce-reuse '
            'vulnerability was introduced.',
      );
    });
  });
}
