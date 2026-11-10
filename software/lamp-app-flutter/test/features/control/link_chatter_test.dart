// What an open lamp's link costs: one reconnect after a save that reboots the
// lamp, a one-byte liveness probe, and one read of the `lamp` section per
// connect.

import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:fake_async/fake_async.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/core/lifecycle/app_lifecycle.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/application/control_state.dart';
import 'package:lamp_app/features/control/application/lamp_auth_required_exception.dart';
import 'package:lamp_app/features/firmware/application/firmware_notifier.dart';
import 'package:lamp_app/features/firmware/domain/firmware_state.dart';
import 'package:lamp_app/features/firmware/domain/lsig_footer.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';

const _id = 'lamp-chatter';

// CHAR_SCHEMA_VERSION as gatt_layout.hpp declares it.
const _schemaVersionUuid = '5f64f4ea-d6d9-4a44-9b3f-3a8d6f7e6b40';

/// The real notifier with its push replaced by a test-driven state.
class _DrivenFirmware extends FirmwareNotifier {
  @override
  FirmwareState build(String deviceId) => const FirmwareIdle();

  void emit(FirmwareState s) => state = s;
}

/// A lamp that reboots on a settings_blob write and is unreachable while it
/// boots. A connect takes [connectTakes] and fails as a timeout while the lamp
/// is down. Every GATT op is logged.
class _Lamp extends InMemoryBleClient {
  _Lamp(this.elapsed);
  final Duration Function() elapsed;

  Duration connectTakes = const Duration(milliseconds: 500);
  Duration rebootTakes = const Duration(seconds: 3);

  /// When the lamp drops the link after a settings_blob write lands.
  Duration dropAfterSave = Duration.zero;

  Duration? _upAt;
  bool get _up => _upAt == null || elapsed() >= _upAt!;

  /// Connects made while the lamp was up.
  int connects = 0;
  int cycles = 0;

  /// Every read, write and section read, as `read <char>`, `write <char>` or
  /// `section <name>`.
  final ops = <String>[];

  /// Makes every GATT op throw as a link the phone still lists but is dead.
  bool zombie = false;

  int sectionReads(String name) =>
      ops.where((o) => o == 'section $name').length;

  void _reboot() {
    unawaited(disconnect(_id));
    _upAt = elapsed() + rebootTakes;
  }

  @override
  Future<void> connect(String deviceId) async {
    await Future<void>.delayed(connectTakes);
    if (!_up) throw TimeoutException('connect timed out');
    connects++;
    await super.connect(deviceId);
  }

  @override
  Future<void> cycleAdapter(String deviceId) async {
    cycles++;
    await super.cycleAdapter(deviceId);
  }

  @override
  Future<Uint8List> read(String d, String s, String c) {
    ops.add('read $c');
    if (zombie) throw BleDisconnectedException(d);
    return super.read(d, s, c);
  }

  @override
  Future<Uint8List> readSection(String deviceId, String name) {
    ops.add('section $name');
    if (zombie) throw BleDisconnectedException(deviceId);
    return super.readSection(deviceId, name);
  }

  @override
  Future<void> write(
    String d,
    String s,
    String c,
    Uint8List v, {
    bool withoutResponse = false,
    bool allowLongWrite = false,
  }) async {
    ops.add('write $c');
    if (zombie) throw BleDisconnectedException(d);
    await super.write(
      d,
      s,
      c,
      v,
      withoutResponse: withoutResponse,
      allowLongWrite: allowLongWrite,
    );
    if (c == BleUuids.settingsBlob) {
      if (dropAfterSave == Duration.zero) {
        _reboot();
      } else {
        Timer(dropAfterSave, _reboot);
      }
    }
  }
}

class _Rig {
  _Rig(this.async, this.c, this.ble);
  final FakeAsync async;
  final ProviderContainer c;
  final _Lamp ble;

  ControlNotifier get control => c.read(controlNotifierProvider(_id).notifier);
  AsyncValue<ControlState> get state => c.read(controlNotifierProvider(_id));
  bool get connected => state.value?.connected == true;

  void probe() {
    final life = c.read(appLifecycleStateProvider.notifier);
    life.set(AppLifecycleState.paused);
    async.flushMicrotasks();
    life.set(AppLifecycleState.resumed);
    async.flushMicrotasks();
  }
}

_Rig _open(FakeAsync async, {String lampJson = _lampJson, int? schema = 4}) {
  final ble = _Lamp(() => async.elapsed);
  seedControlBle(ble, deviceId: _id);
  ble.seedSection(_id, 'lamp', Uint8List.fromList(utf8.encode(lampJson)));
  final c = ProviderContainer(
    overrides: [
      bleClientProvider.overrideWithValue(ble),
      firmwareNotifierProvider.overrideWith2((_) => _DrivenFirmware()),
    ],
  );
  c.read(inventoryNotifierProvider.future);
  async.flushMicrotasks();
  c
      .read(inventoryNotifierProvider.notifier)
      .add(const InventoryLamp(id: _id, name: 'jacko', controlPassword: ''));
  async.flushMicrotasks();
  if (schema != null) {
    // A connected write seeds the fake's readable value.
    ble.connect(_id);
    async.elapse(ble.connectTakes);
    ble.write(
      _id,
      BleUuids.controlService,
      _schemaVersionUuid,
      Uint8List.fromList([schema]),
    );
    async.flushMicrotasks();
  }
  c.listen(controlNotifierProvider(_id), (_, _) {});
  c.listen(firmwareNotifierProvider(_id), (_, _) {});
  async.elapse(const Duration(seconds: 2));
  return _Rig(async, c, ble);
}

const _lampJson =
    '{"name":"jacko","brightness":50,"advancedEnabled":false,'
    '"brightnessCeiling":170}';

void _close(_Rig r) {
  r.c.dispose();
  r.async.flushTimers();
}

final _streaming = FirmwareStreaming(
  footer: LsigFooter(
    channel: 'standard-beta',
    version: 0x010301,
    signedRegionLen: 0,
    signature: Uint8List(64),
  ),
  chunksSent: 3,
  totalChunks: 10,
);

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  group('a save that reboots the lamp reconnects once', () {
    for (final (label, dropAfterSave, rebootTakes) in [
      (
        'the link drops as the save lands',
        Duration.zero,
        const Duration(seconds: 3),
      ),
      (
        'the link drops while the save waits out the reboot',
        const Duration(milliseconds: 300),
        const Duration(seconds: 3),
      ),
      ('the reboot is slow', Duration.zero, const Duration(seconds: 7)),
    ]) {
      test('Advanced LEDs: $label', () {
        fakeAsync((async) {
          final r = _open(async);
          expect(r.connected, isTrue);
          r.ble
            ..dropAfterSave = dropAfterSave
            ..rebootTakes = rebootTakes;
          final connectsBefore = r.ble.connects;
          final s = r.state.value!;
          r.control.applyAdvancedLedsAndReboot(base: s.base, shade: s.shade);
          async.elapse(const Duration(minutes: 2));

          expect(r.ble.connects - connectsBefore, 1);
          expect(r.ble.cycles, 0);
          expect(r.ble.isConnected(_id), isTrue);
          expect(r.connected, isTrue);
          expect(r.state.hasError, isFalse, reason: '${r.state.error}');
          _close(r);
        });
      });
    }

    test('password change', () {
      fakeAsync((async) {
        final r = _open(async);
        final connectsBefore = r.ble.connects;
        r.control.setLampPassword('');
        async.elapse(const Duration(minutes: 2));

        expect(r.ble.connects - connectsBefore, 1);
        expect(r.ble.cycles, 0);
        expect(r.connected, isTrue);
        _close(r);
      });
    });

    test('a reconnect already under way when the save starts stands down', () {
      fakeAsync((async) {
        final r = _open(async);
        r.ble.connectTakes = const Duration(seconds: 3);
        r.ble.disconnect(_id);
        // The ladder's first step starts its slow connect at 500 ms.
        async.elapse(const Duration(milliseconds: 700));
        final lampReads = r.ble.sectionReads('lamp');
        final s = r.state.value!;
        r.control.applyAdvancedLedsAndReboot(base: s.base, shade: s.shade);
        async.elapse(const Duration(minutes: 2));

        expect(r.ble.sectionReads('lamp') - lampReads, 1);
        expect(r.ble.cycles, 0);
        expect(r.connected, isTrue);
        _close(r);
      });
    });

    test('an unsolicited drop still climbs the reconnect ladder', () {
      fakeAsync((async) {
        final r = _open(async);
        r.ble.rebootTakes = const Duration(seconds: 3);
        r.ble._reboot();
        async.elapse(const Duration(seconds: 30));
        expect(r.connected, isTrue);
        _close(r);
      });
    });
  });

  group('the link probe', () {
    test('reads one byte of schema version', () {
      fakeAsync((async) {
        final r = _open(async);
        final mark = r.ble.ops.length;
        r.probe();
        expect(r.ble.ops.skip(mark), ['read $_schemaVersionUuid']);
        expect(r.connected, isTrue);
        _close(r);
      });
    });

    test('reads the lamp section on a lamp without a schema version', () {
      fakeAsync((async) {
        final r = _open(async, schema: null);
        final mark = r.ble.ops.length;
        r.probe();
        expect(r.ble.ops.skip(mark), [
          'read $_schemaVersionUuid',
          'section lamp',
        ]);
        expect(r.connected, isTrue);
        _close(r);
      });
    });

    for (final schema in [4, null]) {
      test('catches a dead link in one probe (schema $schema)', () {
        fakeAsync((async) {
          final r = _open(async, schema: schema);
          r.ble.zombie = true;
          r.probe();
          expect(r.connected, isFalse);
          _close(r);
        });
      });
    }

    test('leaves the lamp alone while the app streams it firmware', () {
      fakeAsync((async) {
        final r = _open(async);
        final fw =
            r.c.read(firmwareNotifierProvider(_id).notifier) as _DrivenFirmware;
        fw.emit(_streaming);
        async.flushMicrotasks();
        final mark = r.ble.ops.length;
        r.probe();
        expect(r.ble.ops.skip(mark), isEmpty);

        fw.emit(const FirmwareFailed(reason: 'lost it'));
        async.elapse(const Duration(seconds: 1));
        final after = r.ble.ops.length;
        r.probe();
        expect(r.ble.ops.skip(after), ['read $_schemaVersionUuid']);
        _close(r);
      });
    });
  });

  group('the lamp section', () {
    test('is read once when a lamp opens', () {
      fakeAsync((async) {
        final r = _open(async);
        expect(r.ble.sectionReads('lamp'), 1);
        _close(r);
      });
    });

    test('is read once per reconnect', () {
      fakeAsync((async) {
        final r = _open(async);
        final reads = r.ble.sectionReads('lamp');
        r.ble.disconnect(_id);
        async.elapse(const Duration(seconds: 5));
        expect(r.connected, isTrue);
        expect(r.ble.sectionReads('lamp') - reads, 1);
        _close(r);
      });
    });

    test('read empty on open asks for the password', () {
      fakeAsync((async) {
        final r = _open(async, lampJson: '');
        expect(r.state.error, isA<LampAuthRequiredException>());
        _close(r);
      });
    });

    test('read empty on a reconnect asks for the password and stops the '
        'ladder', () {
      fakeAsync((async) {
        final r = _open(async);
        r.ble.seedSection(_id, 'lamp', Uint8List(0));
        r.ble.disconnect(_id);
        async.elapse(const Duration(seconds: 5));
        expect(r.state.error, isA<LampAuthRequiredException>());
        final connects = r.ble.connects;
        async.elapse(const Duration(minutes: 1));
        expect(r.ble.connects, connects);
        _close(r);
      });
    });

    test('read empty after a save reboots the lamp asks for the password', () {
      fakeAsync((async) {
        final r = _open(async);
        r.ble.seedSection(_id, 'lamp', Uint8List(0));
        r.control.setLampPassword('');
        async.elapse(const Duration(minutes: 2));
        expect(r.state.error, isA<LampAuthRequiredException>());
        _close(r);
      });
    });
  });
}
