// What an open lamp's link costs: one reconnect after a save that reboots the
// lamp, a one-byte liveness probe, and one read of the `lamp` section per
// connect.

import 'dart:async';
import 'dart:convert';
import 'dart:math';
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
  Duration cycleTakes = Duration.zero;
  Duration authTakes = Duration.zero;
  Duration sectionTakes = Duration.zero;

  /// False makes a settings_blob write land without restarting the lamp.
  bool rebootOnSave = true;

  /// False for a lamp from before GATT schema v1.
  bool servesSchemaVersion = true;

  /// Drops the link as the app next subscribes to state notifies.
  bool dropOnStateSubscribe = false;

  /// When each connect was asked for.
  final connectCalls = <Duration>[];

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

  /// Withholds the connected edge from watchers, as the phone's BLE stack
  /// sometimes does.
  bool missConnectEdges = false;

  @override
  Stream<bool> watchConnected(String deviceId) =>
      super.watchConnected(deviceId).where((up) => !(up && missConnectEdges));

  int sectionReads(String name) =>
      ops.where((o) => o == 'section $name').length;

  void _reboot() {
    unawaited(disconnect(_id));
    _upAt = elapsed() + rebootTakes;
  }

  @override
  Future<void> connect(String deviceId) async {
    connectCalls.add(elapsed());
    await Future<void>.delayed(connectTakes);
    if (!_up) throw TimeoutException('connect timed out');
    connects++;
    await super.connect(deviceId);
  }

  @override
  Future<void> cycleAdapter(String deviceId) async {
    cycles++;
    await super.cycleAdapter(deviceId);
    await Future<void>.delayed(cycleTakes);
  }

  @override
  Future<Uint8List> read(String d, String s, String c) {
    ops.add('read $c');
    // The phone finds a missing characteristic in its discovered services,
    // before any radio op.
    if (c == _schemaVersionUuid && !servesSchemaVersion) throw BleNotFound(c);
    if (zombie) throw BleDisconnectedException(d);
    return super.read(d, s, c);
  }

  @override
  Future<Uint8List> readSection(String deviceId, String name) async {
    ops.add('section $name');
    if (zombie) throw BleDisconnectedException(deviceId);
    await Future<void>.delayed(sectionTakes);
    return super.readSection(deviceId, name);
  }

  @override
  Stream<Uint8List> subscribeNotifyOnly(String d, String s, String c) {
    final stream = super.subscribeNotifyOnly(d, s, c);
    if (c == BleUuids.stateNotify && dropOnStateSubscribe) {
      dropOnStateSubscribe = false;
      unawaited(disconnect(d));
    }
    return stream;
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
    if (c == BleUuids.auth) await Future<void>.delayed(authTakes);
    await super.write(
      d,
      s,
      c,
      v,
      withoutResponse: withoutResponse,
      allowLongWrite: allowLongWrite,
    );
    if (c == BleUuids.settingsBlob && rebootOnSave) {
      if (dropAfterSave == Duration.zero) {
        _reboot();
      } else {
        Timer(dropAfterSave, _reboot);
      }
    }
  }
}

class _Rig {
  _Rig(this.async, this.c, this.ble, this.states);
  final FakeAsync async;
  final ProviderContainer c;
  final _Lamp ble;

  ControlNotifier get control => c.read(controlNotifierProvider(_id).notifier);
  AsyncValue<ControlState> get state => c.read(controlNotifierProvider(_id));
  bool get connected => state.value?.connected == true;

  /// Every state the notifier emitted, in order.
  final List<AsyncValue<ControlState>> states;

  void probe() {
    final life = c.read(appLifecycleStateProvider.notifier);
    life.set(AppLifecycleState.paused);
    async.flushMicrotasks();
    life.set(AppLifecycleState.resumed);
    async.flushMicrotasks();
  }
}

_Rig _open(
  FakeAsync async, {
  String lampJson = _lampJson,
  int? schema = 4,
  String password = '',
}) {
  final ble = _Lamp(() => async.elapsed)..servesSchemaVersion = schema != null;
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
      .add(InventoryLamp(id: _id, name: 'jacko', controlPassword: password));
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
  final states = <AsyncValue<ControlState>>[];
  c.listen(controlNotifierProvider(_id), (_, next) => states.add(next));
  c.listen(firmwareNotifierProvider(_id), (_, _) {});
  async.elapse(const Duration(seconds: 2));
  return _Rig(async, c, ble, states);
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

    test('the link dropping just after the reload restores it climbs the '
        'ladder', () {
      fakeAsync((async) {
        final r = _open(async);
        r.ble.dropOnStateSubscribe = true;
        r.control.setLampPassword('');
        async.elapse(const Duration(seconds: 30));
        expect(r.ble.dropOnStateSubscribe, isFalse);
        expect(r.connected, isTrue);
        _close(r);
      });
    });

    for (final missEdge in [false, true]) {
      test('a long ladder step pending when the save starts never fires '
          '(connect edge missed: $missEdge)', () {
        fakeAsync((async) {
          final r = _open(async);
          r.ble
            ..rebootTakes = const Duration(seconds: 10)
            ..missConnectEdges = missEdge;
          r.ble._reboot();
          // Four failed steps leave the next one 8 s out.
          async.elapse(const Duration(seconds: 10));
          expect(r.state.value!.reconnectAttempt, greaterThanOrEqualTo(4));
          final connects = r.ble.connects;
          final cycles = r.ble.cycles;
          final s = r.state.value!;
          r.control.applyAdvancedLedsAndReboot(base: s.base, shade: s.shade);
          async.elapse(const Duration(minutes: 2));

          expect(r.ble.connects - connects, 1);
          expect(r.ble.cycles - cycles, 0);
          expect(r.connected, isTrue);
          _close(r);
        });
      });
    }

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

  group('a ladder try under way when a save starts leaves the link alone', () {
    const pw = 'hunter22';
    const authWrite = 'write ${BleUuids.auth}';

    /// Drops the link unprompted, lets the ladder run for [lead], then saves.
    /// Returns the index into [_Rig.states] and [_Lamp.ops] at the save.
    (int, int) dropThenSave(_Rig r, Duration lead) {
      r.ble.disconnect(_id);
      r.async.elapse(lead);
      final marks = (r.states.length, r.ble.ops.length);
      final s = r.state.value!;
      r.control.applyAdvancedLedsAndReboot(base: s.base, shade: s.shade);
      return marks;
    }

    test('while connecting', () {
      fakeAsync((async) {
        final r = _open(async, password: pw);
        r.ble.connectTakes = const Duration(seconds: 3);
        // The ladder's first try starts its slow connect at 500 ms.
        final (_, opsAt) = dropThenSave(r, const Duration(milliseconds: 700));
        async.elapse(const Duration(minutes: 2));

        final after = r.ble.ops.skip(opsAt);
        expect(after.where((o) => o == authWrite), hasLength(1));
        expect(after.where((o) => o == 'section lamp'), hasLength(1));
        expect(r.ble.cycles, 0);
        expect(r.connected, isTrue);
        _close(r);
      });
    });

    test('while authenticating', () {
      fakeAsync((async) {
        final r = _open(async, password: pw);
        r.ble
          ..authTakes = const Duration(seconds: 1)
          ..rebootOnSave = false;
        // Connected at 1 s, authenticating until 2 s.
        final (_, opsAt) = dropThenSave(r, const Duration(milliseconds: 1300));
        async.elapse(const Duration(minutes: 2));

        final after = r.ble.ops.skip(opsAt);
        expect(after.where((o) => o == 'section lamp'), hasLength(1));
        expect(r.connected, isTrue);
        _close(r);
      });
    });

    test('while reading sections', () {
      fakeAsync((async) {
        final r = _open(async, password: pw);
        r.ble
          ..sectionTakes = const Duration(milliseconds: 300)
          ..rebootOnSave = false;
        // Reading from 1 s to about 2.8 s.
        final (statesAt, _) = dropThenSave(
          r,
          const Duration(milliseconds: 1500),
        );
        async.elapse(const Duration(minutes: 2));

        final restored = r.states
            .skip(statesAt)
            .where((s) => s.hasValue && !s.isLoading && s.value!.connected);
        expect(restored, hasLength(1));
        expect(r.connected, isTrue);
        _close(r);
      });
    });

    test('while reading a lamp section the lamp refuses', () {
      fakeAsync((async) {
        final r = _open(async, password: pw);
        r.ble
          ..sectionTakes = const Duration(milliseconds: 300)
          ..rebootOnSave = false
          ..seedSection(_id, 'lamp', Uint8List(0));
        // The lamp section read runs from 1 s to 1.3 s.
        final (statesAt, _) = dropThenSave(
          r,
          const Duration(milliseconds: 1100),
        );
        async.elapse(const Duration(minutes: 2));

        final prompts = r.states
            .skip(statesAt)
            .where((s) => s.error is LampAuthRequiredException);
        expect(prompts, hasLength(1));
        _close(r);
      });
    });

    test('while cycling the adapter', () {
      fakeAsync((async) {
        final r = _open(async, password: pw);
        r.ble
          ..rebootTakes = const Duration(seconds: 20)
          ..cycleTakes = const Duration(milliseconds: 1500);
        r.ble._reboot();
        // The third try cycles from 4.5 s to 6 s.
        async.elapse(const Duration(milliseconds: 4600));
        expect(r.ble.cycles, 1);
        async.elapse(const Duration(milliseconds: 400));
        final savedAt = async.elapsed;
        final s = r.state.value!;
        r.control.applyAdvancedLedsAndReboot(base: s.base, shade: s.shade);
        async.elapse(const Duration(minutes: 2));

        final duringGrace = r.ble.connectCalls.where(
          (t) => t >= savedAt && t < savedAt + const Duration(seconds: 2),
        );
        expect(duringGrace, isEmpty);
        expect(r.connected, isTrue);
        _close(r);
      });
    });

    test('when its connect fails', () {
      fakeAsync((async) {
        final r = _open(async, password: pw);
        r.ble
          ..rebootTakes = const Duration(seconds: 20)
          ..connectTakes = const Duration(seconds: 3);
        r.ble._reboot();
        // The first try's connect runs from 0.5 s and fails at 3.5 s.
        async.elapse(const Duration(seconds: 1));
        final attempt = r.state.value!.reconnectAttempt;
        final statesAt = r.states.length;
        final s = r.state.value!;
        r.control.applyAdvancedLedsAndReboot(base: s.base, shade: s.shade);
        async.elapse(const Duration(minutes: 3));

        final climbed = r.states
            .skip(statesAt)
            .where((s) => (s.value?.reconnectAttempt ?? 0) > attempt);
        expect(climbed, isEmpty);
        expect(r.connected, isTrue);
        _close(r);
      });
    });

    group('outlasting the save', () {
      /// Drops the link unprompted; the ladder's third try cycles the adapter
      /// from 4.5 s for [cycleTakes]. A save at 5.5 s, once the lamp is up,
      /// reloads it at about 8 s unless [lampDownFor] keeps it away.
      _Rig saveDuringCycle(
        FakeAsync async, {
        required Duration cycleTakes,
        Duration lampDownFor = const Duration(seconds: 5),
      }) {
        final r = _open(async, password: pw);
        r.ble
          ..rebootTakes = lampDownFor
          ..cycleTakes = cycleTakes
          ..rebootOnSave = false;
        r.ble._reboot();
        async.elapse(const Duration(milliseconds: 5500));
        expect(r.ble.cycles, 1);
        final s = r.state.value!;
        r.control.applyAdvancedLedsAndReboot(base: s.base, shade: s.shade);
        return r;
      }

      test('it climbs the ladder for a drop after the save restored the '
          'link', () {
        fakeAsync((async) {
          final r = saveDuringCycle(
            async,
            cycleTakes: const Duration(seconds: 10),
          );
          async.elapse(const Duration(seconds: 3));
          expect(r.connected, isTrue);
          // The drop's first step fires at 9.5 s, while the try still cycles.
          r.ble.disconnect(_id);
          async.elapse(const Duration(minutes: 1));

          expect(r.connected, isTrue);
          _close(r);
        });
      });

      test('it leaves a step the drop already armed to run', () {
        fakeAsync((async) {
          final r = saveDuringCycle(
            async,
            cycleTakes: const Duration(seconds: 10),
          );
          // The try stands down at 14.5 s, with the drop's step due 14.7 s.
          async.elapse(const Duration(milliseconds: 8700));
          final statesAt = r.states.length;
          r.ble.disconnect(_id);
          async.elapse(const Duration(minutes: 1));

          final attempts = r.states
              .skip(statesAt)
              .map((s) => s.value?.reconnectAttempt ?? 0);
          expect(attempts.reduce(max), 1);
          expect(r.connected, isTrue);
          _close(r);
        });
      });

      test('it leaves the error of a save that gave up', () {
        fakeAsync((async) {
          final r = saveDuringCycle(
            async,
            cycleTakes: const Duration(seconds: 40),
            lampDownFor: const Duration(minutes: 5),
          );
          // The save gives up at about 25 s; the try stands down at 44.5 s.
          async.elapse(const Duration(seconds: 30));
          expect(r.state.hasError, isTrue);
          async.elapse(const Duration(seconds: 20));

          expect(r.state.hasError, isTrue);
          _close(r);
        });
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

        fw.emit(
          const FirmwareFailed(
            reason: 'lost it',
            failure: FirmwareFailure.linkLost,
          ),
        );
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
