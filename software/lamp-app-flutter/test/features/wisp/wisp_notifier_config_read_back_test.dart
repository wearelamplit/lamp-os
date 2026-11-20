import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/domain/wisp_status.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';

const _lampId = 'read-back-lamp';
const _mac = 'AA:BB:CC:DD:EE:FF';

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

String _config({
  String ledType = 'GRBW',
  int px = 60,
  int brightness = 80,
  int driftIntervalMs = 90000,
  int driftFadePct = 40,
}) =>
    '{"char":"wispConfig","ledType":"$ledType","px":$px,'
    '"brightness":$brightness,"driftIntervalMs":$driftIntervalMs,'
    '"driftFadePct":$driftFadePct,"shuffleSeed":7,"opSeq":0}';

String _split({int configLastSeenMs = 100}) =>
    '{"wispMac":"$_mac","configSplit":true,"source":"osc",'
    '"configLastSeenMs":$configLastSeenMs}';

/// Counts `wispconfig` reads, fails the next [failConfigReads] of them, and
/// holds them while [holdConfig] is set. Holds op writes while [holdOpWrite]
/// is set.
class _ConfigBle extends InMemoryBleClient {
  int configReads = 0;
  int failConfigReads = 0;
  Completer<void>? holdConfig;
  Completer<void>? holdOpWrite;

  @override
  Future<void> write(
    String d,
    String s,
    String c,
    Uint8List v, {
    bool withoutResponse = false,
    bool allowLongWrite = false,
  }) async {
    await super.write(
      d,
      s,
      c,
      v,
      withoutResponse: withoutResponse,
      allowLongWrite: allowLongWrite,
    );
    if (c == BleUuids.wispOp) await holdOpWrite?.future;
  }

  @override
  Future<Uint8List> readSection(String deviceId, String name) async {
    if (name == 'wispconfig') {
      configReads++;
      await holdConfig?.future;
      if (failConfigReads > 0) {
        failConfigReads--;
        throw Exception('config read failed');
      }
    }
    return super.readSection(deviceId, name);
  }
}

/// A settings write the wisp never applies: once its statuses carry no
/// settings, nothing re-sends the config, so the app reads it back a few
/// seconds after the write and shows what the wisp holds.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUp(() => SharedPreferences.setMockInitialValues({}));
  final defaultDelay = WispNotifier.debugConfigReadBackDelay;
  const delay = Duration(milliseconds: 400);
  setUp(() => WispNotifier.debugConfigReadBackDelay = delay);
  tearDown(() => WispNotifier.debugConfigReadBackDelay = defaultDelay);

  void notify(InMemoryBleClient ble, String json) => ble.simulateNotify(
        _lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        _b(json),
      );

  Future<void> wait(Duration d) => Future<void>.delayed(d);

  Future<(ProviderContainer, WispNotifier)> open(_ConfigBle ble) async {
    await ble.connect(_lampId);
    ble.seedSection(_lampId, 'wispstatus', _b(_split()));
    ble.seedSection(_lampId, 'wispconfig', _b(_config()));
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    return (c, c.read(wispNotifierProvider(_lampId).notifier));
  }

  // What the app shows for the wisp once its next status lands: the held
  // config merged over a status without settings.
  Future<WispStatus> shown(ProviderContainer c, _ConfigBle ble) async {
    notify(ble, _split());
    await pumpEventQueue();
    return c.read(wispNotifierProvider(_lampId)).value!;
  }

  final writes = <(String, void Function(WispNotifier), bool Function(WispStatus))>[
    (
      'drift',
      (n) {
        n.setDrift(300000, 70);
        n.flushDrift();
      },
      (s) => s.driftIntervalMs == 90000 && s.driftFadePct == 40,
    ),
    (
      'brightness',
      (n) {
        n.setBrightness(30);
        n.flushBrightness();
      },
      (s) => s.brightness == 80,
    ),
    (
      'LED strip',
      (n) => unawaited(n.setLedStrip('RGB', 30)),
      (s) => s.ledType == 'GRBW' && s.pixelCount == 60,
    ),
  ];

  for (final (name, write, showsWisp) in writes) {
    test('a $name write the wisp never applies is read back', () async {
      final ble = _ConfigBle();
      final (c, n) = await open(ble);

      write(n);
      await pumpEventQueue();
      expect(showsWisp(await shown(c, ble)), isFalse,
          reason: 'the write shows at once');

      await wait(delay * 2);
      expect(showsWisp(await shown(c, ble)), isTrue);
    });
  }

  test('a second write before the read-back moves it past the second',
      () async {
    final ble = _ConfigBle();
    final (c, n) = await open(ble);
    final atOpen = ble.configReads;

    n.setBrightness(30);
    n.flushBrightness();
    await wait(delay * 0.6);
    n.setBrightness(20);
    n.flushBrightness();
    await wait(delay * 0.6);
    expect(ble.configReads, atOpen, reason: 'read back before the second');
    expect((await shown(c, ble)).brightness, 20);

    await wait(delay);
    expect(ble.configReads, greaterThan(atOpen));
    expect((await shown(c, ble)).brightness, 80);
  });

  // The wisp applies a second write while the first's read-back is reading.
  test('a write during the read-back gets a read-back of its own', () async {
    final ble = _ConfigBle();
    final (c, n) = await open(ble);

    n.setBrightness(30);
    n.flushBrightness();
    ble.holdConfig = Completer<void>();
    final atWrite = ble.configReads;
    for (var i = 0; i < 40 && ble.configReads == atWrite; i++) {
      await wait(const Duration(milliseconds: 25));
    }
    expect(ble.configReads, atWrite + 1, reason: 'no read-back started');

    n.setBrightness(20);
    n.flushBrightness();
    await pumpEventQueue();
    ble.holdConfig!.complete();
    ble.holdConfig = null;
    await pumpEventQueue();
    ble.seedSection(_lampId, 'wispconfig', _b(_config(brightness: 20)));

    await wait(delay * 2);
    expect((await shown(c, ble)).brightness, 20);
  });

  test('a failed read-back is retried', () async {
    final ble = _ConfigBle();
    final (c, n) = await open(ble);

    n.setBrightness(30);
    n.flushBrightness();
    ble.failConfigReads = 1;
    await wait(delay * 4);

    expect(ble.failConfigReads, 0, reason: 'no read-back was made');
    expect((await shown(c, ble)).brightness, 80);
  });

  test('a reconnect reads back a write made before the drop', () async {
    WispNotifier.debugConfigReadBackDelay = const Duration(seconds: 5);
    final ble = _ConfigBle();
    final (c, n) = await open(ble);

    n.setBrightness(30);
    n.flushBrightness();
    await pumpEventQueue();
    await ble.disconnect(_lampId);
    await pumpEventQueue();
    await ble.connect(_lampId);
    await wait(const Duration(milliseconds: 1500));

    expect((await shown(c, ble)).brightness, 80);
  });

  // The wisp applied the write and its config reached the lamp before the
  // read-back: the read-back shows the same.
  test('a newer config arriving first is kept', () async {
    final ble = _ConfigBle();
    final (c, n) = await open(ble);

    n.setBrightness(30);
    n.flushBrightness();
    await pumpEventQueue();
    ble.seedSection(_lampId, 'wispconfig', _b(_config(brightness: 30)));
    notify(ble, _split(configLastSeenMs: 200));
    await pumpEventQueue();
    expect(c.read(wispNotifierProvider(_lampId)).value!.brightness, 30);

    await wait(delay * 2);
    expect(c.read(wispNotifierProvider(_lampId)).value!.brightness, 30);
  });

  test('no read-back after dispose', () async {
    final ble = _ConfigBle();
    final (c, n) = await open(ble);

    n.setBrightness(30);
    n.flushBrightness();
    await pumpEventQueue();
    final atDispose = ble.configReads;
    c.dispose();
    await wait(delay * 2);

    expect(ble.configReads, atDispose);
  });

  // The write's ack returns after the user left the lamp.
  test('a write that returns after dispose reads nothing back', () async {
    final ble = _ConfigBle();
    final (c, n) = await open(ble);

    ble.holdOpWrite = Completer<void>();
    final write = n.setLedStrip('RGB', 30);
    await pumpEventQueue();
    final atDispose = ble.configReads;
    c.dispose();
    ble.holdOpWrite!.complete();
    await write;
    await wait(delay * 2);

    expect(ble.configReads, atDispose);
  });
}
