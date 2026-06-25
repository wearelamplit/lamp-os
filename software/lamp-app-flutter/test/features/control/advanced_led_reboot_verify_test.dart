import 'dart:convert';
import 'dart:typed_data';

import 'package:fake_async/fake_async.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/core/ble/lamp_crypto.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/seed.dart';

const _devId = 'lamp-a';

/// Build container + notifier, seeding the BLE fake with the given
/// base/shade pixel counts and byte orders.
Future<({ProviderContainer container, InMemoryBleClient ble})> _build({
  int basePx = 35,
  String baseByteOrder = 'GRBW',
  int baseBpp = 4,
  int shadePx = 38,
  String shadeByteOrder = 'GRBW',
  int shadeBpp = 4,
}) async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(
    ble,
    deviceId: _devId,
    brightness: 50,
    basePx: basePx,
    baseBpp: baseBpp,
    shadePx: shadePx,
    shadeBpp: shadeBpp,
  );

  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );

  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'test-lamp',
        controlPassword: '',
      ));

  c.listen(controlNotifierProvider(_devId), (_, _) {});
  await c.read(controlNotifierProvider(_devId).future);

  return (container: c, ble: ble);
}

/// Re-seed the fake BLE section bytes for a given device after a simulated
/// reboot — this emulates the lamp coming back up with whatever values
/// the firmware actually persisted.
void _reseed(
  InMemoryBleClient ble, {
  int basePx = 35,
  String baseByteOrder = 'GRBW',
  int baseBpp = 4,
  int shadePx = 38,
  String shadeByteOrder = 'GRBW',
  int shadeBpp = 4,
}) {
  ble.seedSection(
    _devId,
    'base',
    Uint8List.fromList(utf8.encode(
      '{"px":$basePx,"ac":0,"bpp":$baseBpp,'
      '"byteOrder":"$baseByteOrder",'
      '"colors":["#300783FF"],"knockout":[]}',
    )),
  );
  ble.seedSection(
    _devId,
    'shade',
    Uint8List.fromList(utf8.encode(
      '{"px":$shadePx,"bpp":$shadeBpp,'
      '"byteOrder":"$shadeByteOrder",'
      '"colors":["#000000FF"]}',
    )),
  );
}

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  group('applyAdvancedLedsAndReboot', () {
    test('returns empty mismatches when reconnect sections match shipped',
        () {
      fakeAsync((async) {
        late ProviderContainer container;
        late InMemoryBleClient ble;

        // Build container synchronously inside fakeAsync.
        _build(basePx: 35, baseByteOrder: 'GRBW', shadePx: 38,
                shadeByteOrder: 'GRBW')
            .then((r) {
          container = r.container;
          ble = r.ble;
        });
        async.flushMicrotasks();

        // Re-seed so that the post-reboot read returns the same values
        // that were shipped — simulating a successful firmware write.
        _reseed(ble, basePx: 35, baseByteOrder: 'GRBW', shadePx: 38,
            shadeByteOrder: 'GRBW');

        final ctrl = container.read(controlNotifierProvider(_devId));
        final state = ctrl.value!;
        final notifier =
            container.read(controlNotifierProvider(_devId).notifier);

        late List<String> result;
        notifier
            .applyAdvancedLedsAndReboot(base: state.base, shade: state.shade)
            .then((r) => result = r);

        // Advance past _awaitReconnectAndReload's 2s initial grace delay
        // plus a little headroom for the retry loop (not needed on first
        // attempt with InMemoryBleClient but included for safety).
        async.elapse(const Duration(seconds: 5));
        async.flushMicrotasks();

        expect(result, isEmpty,
            reason: 'no mismatches when fresh sections round-trip correctly');

        container.dispose();
      });
    });

    test('returns ["base.px"] when fresh.base.px differs from shipped', () {
      fakeAsync((async) {
        late ProviderContainer container;
        late InMemoryBleClient ble;

        _build(basePx: 35, shadePx: 38).then((r) {
          container = r.container;
          ble = r.ble;
        });
        async.flushMicrotasks();

        // Simulate firmware not persisting base.px (comes back as 30).
        _reseed(ble, basePx: 30, shadePx: 38);

        final state = container.read(controlNotifierProvider(_devId)).value!;
        final notifier =
            container.read(controlNotifierProvider(_devId).notifier);

        late List<String> result;
        notifier
            .applyAdvancedLedsAndReboot(base: state.base, shade: state.shade)
            .then((r) => result = r);

        async.elapse(const Duration(seconds: 5));
        async.flushMicrotasks();

        expect(result, contains('base.px'),
            reason: 'base.px mismatch should be reported');

        container.dispose();
      });
    });

    test('returns ["shade.px"] when fresh.shade.px differs from shipped', () {
      fakeAsync((async) {
        late ProviderContainer container;
        late InMemoryBleClient ble;

        _build(basePx: 35, shadePx: 38).then((r) {
          container = r.container;
          ble = r.ble;
        });
        async.flushMicrotasks();

        // Simulate firmware not persisting shade.px.
        _reseed(ble, basePx: 35, shadePx: 20);

        final state = container.read(controlNotifierProvider(_devId)).value!;
        final notifier =
            container.read(controlNotifierProvider(_devId).notifier);

        late List<String> result;
        notifier
            .applyAdvancedLedsAndReboot(base: state.base, shade: state.shade)
            .then((r) => result = r);

        async.elapse(const Duration(seconds: 5));
        async.flushMicrotasks();

        expect(result, contains('shade.px'));

        container.dispose();
      });
    });

    test('writes settings_blob with reboot:true to BLE before reconnecting',
        () {
      fakeAsync((async) {
        late ProviderContainer container;
        late InMemoryBleClient ble;

        _build().then((r) {
          container = r.container;
          ble = r.ble;
        });
        async.flushMicrotasks();

        _reseed(ble);

        final state = container.read(controlNotifierProvider(_devId)).value!;
        final notifier =
            container.read(controlNotifierProvider(_devId).notifier);

        notifier.applyAdvancedLedsAndReboot(
            base: state.base, shade: state.shade);
        async.flushMicrotasks();

        // The settings_blob write should have fired synchronously before
        // the reconnect ladder starts (writeSettingsBlob is awaited first).
        final writes = ble.writesTo(_devId, BleUuids.settingsBlob);
        expect(writes, isNotEmpty,
            reason: 'should write to settings_blob immediately');

        // Verify the written payload includes reboot:true.
        // Payload byte[0] is magicPlaintext (0x01); rest is JSON.
        final payload = writes.first;
        expect(payload[0], LampCrypto.magicPlaintext,
            reason: 'first byte is magicPlaintext (no password set)');
        final json = jsonDecode(utf8.decode(payload.sublist(1)))
            as Map<String, dynamic>;
        expect(json['reboot'], isTrue);
        expect(json['base'], isNotNull);
        expect(json['shade'], isNotNull);

        async.elapse(const Duration(seconds: 5));
        async.flushMicrotasks();
        container.dispose();
      });
    });
  });
}
