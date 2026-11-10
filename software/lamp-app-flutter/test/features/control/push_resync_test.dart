import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/firmware/application/firmware_notifier.dart';
import 'package:lamp_app/features/firmware/domain/firmware_state.dart';
import 'package:lamp_app/features/firmware/domain/lsig_footer.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/lamp_shell/presentation/home_mode_screen.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';

const _id = 'lamp-x';

/// The real notifier with its push replaced by a test-driven state, since a
/// real push needs a release-signed image.
class _DrivenFirmware extends FirmwareNotifier {
  @override
  FirmwareState build(String deviceId) => const FirmwareIdle();

  void emit(FirmwareState s) => state = s;
}

class _CountingBle extends InMemoryBleClient {
  int sectionReads = 0;

  @override
  Future<Uint8List> readSection(String deviceId, String name) {
    sectionReads++;
    return super.readSection(deviceId, name);
  }
}

final _footer = LsigFooter(
  channel: 'standard-beta',
  version: 0x010301,
  signedRegionLen: 0,
  signature: Uint8List(64),
);

final _streaming = FirmwareStreaming(
  footer: _footer,
  chunksSent: 3,
  totalChunks: 10,
);

Future<void> _pump() async {
  for (var i = 0; i < 10; i++) {
    await Future<void>.delayed(const Duration(milliseconds: 20));
  }
}

class _Rig {
  _Rig(this.c, this.ble);
  final ProviderContainer c;
  final _CountingBle ble;

  ControlNotifier get control => c.read(controlNotifierProvider(_id).notifier);
  _DrivenFirmware get firmware =>
      c.read(firmwareNotifierProvider(_id).notifier) as _DrivenFirmware;
  int get brightness =>
      c.read(controlNotifierProvider(_id)).value!.lamp.brightness;

  /// The characteristic written just before the last brightness write.
  String? beforeLastBrightness() {
    final chars = ble.charsWrittenTo(_id);
    final i = chars.lastIndexOf(BleUuids.brightness);
    return i > 0 ? chars[i - 1] : null;
  }

  int get lastFocus => ble.writesTo(_id, BleUuids.homeModeFocus).last.single;
}

Future<_Rig> _rig() async {
  SharedPreferences.setMockInitialValues({});
  final ble = _CountingBle();
  await seedControlBle(ble, deviceId: _id, brightness: 50);
  final c = ProviderContainer(
    overrides: [
      bleClientProvider.overrideWithValue(ble),
      firmwareNotifierProvider.overrideWith2((_) => _DrivenFirmware()),
    ],
  );
  addTearDown(c.dispose);
  await c.read(inventoryNotifierProvider.future);
  await c
      .read(inventoryNotifierProvider.notifier)
      .add(const InventoryLamp(id: _id, name: 'jacko', controlPassword: ''));
  c.listen(controlNotifierProvider(_id), (_, _) {});
  c.listen(firmwareNotifierProvider(_id), (_, _) {});
  await c.read(controlNotifierProvider(_id).future);
  await _pump();
  return _Rig(c, ble);
}

void main() {
  for (final (label, end) in [
    ('fails', const FirmwareFailed(reason: 'lost it') as FirmwareState),
    ('is cancelled', const FirmwareIdle()),
  ]) {
    test(
      'a push that $label re-reads the lamp, dropping edits it never got',
      () async {
        final r = await _rig();
        r.firmware.emit(_streaming);
        await _pump();
        await r.control.setBrightness(90);
        expect(r.brightness, 90);

        r.firmware.emit(end);
        await _pump();
        expect(r.brightness, 50, reason: 'the lamp dropped the write');
      },
    );
  }

  test('a push that succeeds leaves the re-read to the reboot', () async {
    final r = await _rig();
    r.firmware.emit(_streaming);
    await _pump();
    final reads = r.ble.sectionReads;
    r.firmware.emit(FirmwareSucceeded(footer: _footer));
    await _pump();
    expect(r.ble.sectionReads, reads);
  });

  for (final open in [false, true]) {
    test('after a failed push, Home Mode focus (open: $open) goes out just '
        'before each brightness write', () async {
      final r = await _rig();
      r.control.setHomeModeFocus(!open);
      r.firmware.emit(_streaming);
      await _pump();
      r.control.setHomeModeFocus(open);
      r.firmware.emit(const FirmwareFailed(reason: 'x'));
      await _pump();

      for (final level in [60, 70]) {
        await r.control.setBrightness(level);
        await _pump();
        expect(r.beforeLastBrightness(), BleUuids.homeModeFocus);
        expect(r.lastFocus, open ? 1 : 0);
      }
    });
  }

  test('a push-free reconnect ends the focus re-sends', () async {
    final r = await _rig();
    r.firmware.emit(_streaming);
    await _pump();
    r.firmware.emit(const FirmwareFailed(reason: 'x'));
    await _pump();

    await r.ble.disconnect(_id);
    await _pump();
    await r.ble.connect(_id);
    await _pump();
    await r.control.setBrightness(60);
    await _pump();
    expect(r.beforeLastBrightness(), isNot(BleUuids.homeModeFocus));
  });

  test('with no push, a brightness write sends no focus', () async {
    final r = await _rig();
    await r.control.setBrightness(60);
    await _pump();
    expect(r.ble.writesTo(_id, BleUuids.brightness), isNotEmpty);
    expect(r.ble.writesTo(_id, BleUuids.homeModeFocus), isEmpty);
  });

  testWidgets(
    'the Home Mode screen sets focus on open and clears it on leave',
    (tester) async {
      late _Rig r;
      await tester.runAsync(() async => r = await _rig());
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: r.c,
          child: const MaterialApp(home: HomeModeScreen(lampId: _id)),
        ),
      );
      await tester.pump();
      expect(r.lastFocus, 1);
      await tester.pumpWidget(const SizedBox.shrink());
      expect(r.lastFocus, 0);
    },
  );
}
