// A reconnect after an OTA flash must refresh the inventory record's firmware
// version, so the Social "This lamp" self-card (which reads inventory) matches
// the Info page (which reads live section state). The initial connect already
// mirrors; the regression was the reconnect path not doing the same.

import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';

const _devId = 'lamp-reconnect-fw';

int _invFwVersion(ProviderContainer c) => c
    .read(inventoryNotifierProvider)
    .value!
    .firstWhere((l) => l.id == _devId)
    .fwVersion!;

Future<void> _pump() async {
  for (var i = 0; i < 20; i++) {
    await Future<void>.delayed(Duration.zero);
  }
}

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  test('reconnect after a version bump refreshes inventory + live state',
      () async {
    final ble = InMemoryBleClient();
    await seedControlBle(ble,
        deviceId: _devId, fwVersion: 10205, fwChannel: 'beta');

    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
          id: _devId,
          name: 'test-lamp',
          controlPassword: '',
        ));
    c.listen(controlNotifierProvider(_devId), (_, _) {});
    await c.read(controlNotifierProvider(_devId).future);
    await _pump();

    expect(_invFwVersion(c), 10205);

    // Lamp gets flashed to a newer version while the link is down.
    ble.seedSection(
      _devId,
      'lamp',
      Uint8List.fromList(utf8.encode(
        '{"name":"test-lamp","brightness":50,"advancedEnabled":false,'
        '"brightnessCeiling":170,"fwVersion":10206,"fwChannel":"beta"}',
      )),
    );

    // Drop then restore the link; the restore edge kicks _tryReconnect.
    await ble.disconnect(_devId);
    await _pump();
    await ble.connect(_devId);
    await _pump();

    final live = c.read(controlNotifierProvider(_devId)).value!.lamp.fwVersion;
    expect(live, 10206, reason: 'Info page (live section) should be fresh');
    expect(_invFwVersion(c), 10206,
        reason: 'Social self-card (inventory) should match live version');
  });
}
