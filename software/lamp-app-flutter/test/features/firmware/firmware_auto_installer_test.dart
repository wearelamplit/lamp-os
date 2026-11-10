// The failed-push line promises "Trying again next time you connect". This
// pins that promise to the auto-installer.

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/firmware/application/cached_firmware_notifier.dart';
import 'package:lamp_app/features/firmware/application/firmware_auto_installer.dart';
import 'package:lamp_app/features/firmware/application/firmware_notifier.dart';
import 'package:lamp_app/features/firmware/data/cached_firmware.dart';
import 'package:lamp_app/features/firmware/data/firmware_release_client.dart';
import 'package:lamp_app/features/firmware/domain/firmware_state.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';

const _id = 'lamp-auto';

const _newer = CachedFirmware(
  lampType: 'standard',
  channel: FirmwareChannel.beta,
  version: 5,
  byteLength: 1300000,
  fetchedAtMs: 0,
);

/// Every push it is asked for fails.
class _FailingPush extends FirmwareNotifier {
  int pushes = 0;

  @override
  FirmwareState build(String deviceId) => const FirmwareIdle();

  @override
  Future<void> installFromCache({
    required String lampType,
    required FirmwareChannel channel,
  }) async {
    pushes++;
    state = const FirmwareFailed(
      reason: 'lost the lamp',
      failure: FirmwareFailure.linkLost,
    );
  }
}

class _Cache extends CachedFirmwareNotifier {
  @override
  Future<Map<String, CachedFirmware>> build() async => {_newer.key: _newer};

  @override
  Future<void> syncForInventory(List<InventoryLamp> inventory) async {}
}

Future<void> _until(bool Function() done) async {
  for (var i = 0; i < 100 && !done(); i++) {
    await Future<void>.delayed(const Duration(milliseconds: 50));
  }
}

void main() {
  test('a failed push is tried again on the next connect, and only then',
      () async {
    SharedPreferences.setMockInitialValues({});
    final ble = InMemoryBleClient();
    await seedControlBle(
      ble,
      deviceId: _id,
      lampType: 'standard',
      fwVersion: 4,
      fwChannel: 'standard-beta',
    );
    final c = ProviderContainer(
      overrides: [
        bleClientProvider.overrideWithValue(ble),
        cachedFirmwareNotifierProvider.overrideWith(_Cache.new),
        firmwareNotifierProvider.overrideWith2((_) => _FailingPush()),
      ],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(inventoryNotifierProvider.notifier).add(
          const InventoryLamp(id: _id, name: 'jacko', controlPassword: 'x'),
        );
    final push = c.read(firmwareNotifierProvider(_id).notifier) as _FailingPush;
    c.listen(firmwareAutoInstallerProvider(_id), (_, _) {});
    bool connected() =>
        c.read(controlNotifierProvider(_id)).value?.connected == true;

    await _until(() => push.pushes > 0);
    expect(push.pushes, 1);
    // The cache refreshing re-evaluates the lamp.
    c.invalidate(cachedFirmwareNotifierProvider);
    await Future<void>.delayed(const Duration(milliseconds: 300));
    expect(push.pushes, 1, reason: 'no retry while the link stays up');

    await ble.disconnect(_id);
    await _until(() => !connected());
    await _until(connected);
    await _until(() => push.pushes > 1);
    expect(push.pushes, 2);
  });
}
