import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';

const _lampId = 'split-lamp';
const _mac = 'AA:BB:CC:DD:EE:FF';

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

String _config({
  int px = 60,
  int opSeq = 0,
  int driftIntervalMs = 90000,
  int driftFadePct = 40,
}) =>
    '{"char":"wispConfig","ledType":"GRBW","px":$px,"brightness":80,'
    '"driftIntervalMs":$driftIntervalMs,"driftFadePct":$driftFadePct,'
    '"shuffleSeed":7,"opSeq":$opSeq}';

String _split({int? configLastSeenMs, bool hasPassword = false}) =>
    '{"wispMac":"$_mac","configSplit":true,"source":"osc"'
    '${hasPassword ? ',"hasPassword":true' : ''}'
    '${configLastSeenMs != null ? ',"configLastSeenMs":$configLastSeenMs' : ''}}';

/// A status that carries no settings (`configSplit`): the app holds the
/// wisp's settings only through the `wispconfig` section, so it must fetch
/// them without waiting on a lucky notify and never confirm or show from a
/// placeholder.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUp(() => SharedPreferences.setMockInitialValues({}));

  ProviderContainer makeContainer(InMemoryBleClient ble) {
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    return c;
  }

  Future<void> settle() => Future<void>.delayed(Duration.zero);

  Future<void> built(ProviderContainer c) async {
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await settle();
  }

  void notify(InMemoryBleClient ble, String json) => ble.simulateNotify(
        _lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        _b(json),
      );

  List<String> ops(InMemoryBleClient ble) => [
        for (final w in ble.writesTo(_lampId, BleUuids.wispOp))
          if (utf8.decode(w, allowMalformed: true).startsWith('{'))
            (jsonDecode(utf8.decode(w)) as Map<String, dynamic>)['op']
                as String,
      ];

  // The lamp booted after the wisp, or cleared its cache on a display
  // switch, so it holds no config, and no notify follows once one lands.
  test('refill without notify: a missing config is re-polled and re-read',
      () async {
    final ble = InMemoryBleClient();
    await ble.connect(_lampId);
    ble.seedSection(_lampId, 'wispstatus', _b(_split()));
    ble.seedSection(_lampId, 'wispconfig', _b('{}'));
    final c = makeContainer(ble);
    await built(c);
    expect(c.read(wispNotifierProvider(_lampId)).value!.configKnown, isFalse);

    ble.seedSection(_lampId, 'wispconfig', _b(_config(px: 44)));
    for (var i = 0;
        i < 80 && !c.read(wispNotifierProvider(_lampId)).value!.configKnown;
        i++) {
      await Future<void>.delayed(const Duration(milliseconds: 50));
    }

    final s = c.read(wispNotifierProvider(_lampId)).value!;
    expect(s.configKnown, isTrue);
    expect(s.pixelCount, 44);
    expect(ops(ble), contains('pollConfig'));
  });

  // A new configLastSeenMs lands while another read holds the link. The read
  // it owes runs once the link is free, without another notify.
  group('skipped config read retried once reads go idle', () {
    Future<(_HeldBle, ProviderContainer)> heldBuild() async {
      final ble = _HeldBle();
      await ble.connect(_lampId);
      ble.seedSection(
          _lampId, 'wispstatus', _b(_split(configLastSeenMs: 100)));
      ble.seedSection(_lampId, 'wispconfig', _b(_config(px: 60)));
      final c = makeContainer(ble);
      await built(c);
      return (ble, c);
    }

    Future<void> releaseAndExpect44(_HeldBle ble, ProviderContainer c) async {
      expect(ble.held, isTrue, reason: 'the read under test never started');
      ble.seedSection(_lampId, 'wispconfig', _b(_config(px: 44)));
      notify(ble, _split(configLastSeenMs: 300));
      await settle();
      ble.hold!.complete();
      ble.hold = null;
      for (var i = 0; i < 10; i++) {
        await settle();
      }
      expect(c.read(wispNotifierProvider(_lampId)).value!.pixelCount, 44);
    }

    test('behind a claims read', () async {
      final (ble, c) = await heldBuild();
      ble.holdOn(BleUuids.wispClaims);
      notify(ble, _split(configLastSeenMs: 100));
      await settle();
      await releaseAndExpect44(ble, c);
    });

    test('behind a palette read', () async {
      final (ble, c) = await heldBuild();
      ble.holdOn('wisppalette');
      notify(
        ble,
        '{"wispMac":"$_mac","configSplit":true,"source":"manual",'
        '"paletteIdPrefix":"abcdef12","configLastSeenMs":100}',
      );
      await settle();
      await releaseAndExpect44(ble, c);
    });

    test('behind a status re-read', () async {
      final (ble, c) = await heldBuild();
      ble.holdOn('wispstatus');
      notify(ble, '{"wispMac":"$_mac","sour');
      await settle();
      await releaseAndExpect44(ble, c);
    });

    // The read in flight serves the config from before the change.
    test('behind an earlier config read', () async {
      final (ble, c) = await heldBuild();
      ble.seedSection(_lampId, 'wispconfig', _b(_config(px: 50)));
      ble.holdOn('wispconfig');
      notify(ble, _split(configLastSeenMs: 200));
      await settle();
      await releaseAndExpect44(ble, c);
    });
  });

  // Every build read landed before the auth handshake, so nothing showed the
  // lamp serves the sections, and a lamp holding no config merges no
  // configLastSeenMs to show it later.
  test('refill finds the sections when every build read missed them',
      () async {
    final ble = InMemoryBleClient();
    await ble.connect(_lampId);
    await ble.write(
      _lampId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      _b(_split()),
    );
    final c = makeContainer(ble);
    await built(c);
    expect(c.read(wispNotifierProvider(_lampId)).value!.configKnown, isFalse);

    ble.seedSection(_lampId, 'wispstatus', _b(_split()));
    ble.seedSection(_lampId, 'wispconfig', _b(_config(px: 44)));
    for (var i = 0;
        i < 80 && !c.read(wispNotifierProvider(_lampId)).value!.configKnown;
        i++) {
      await Future<void>.delayed(const Duration(milliseconds: 50));
    }

    expect(c.read(wispNotifierProvider(_lampId)).value!.pixelCount, 44);
  });

  // The refill backs off while the config stays missing (1 s, then 2 s, so
  // reads at 1 s and 3 s), and a reconnect starts it again from 1 s.
  test('refill backs off, and a reconnect restarts it', () async {
    final ble = _HeldBle();
    await ble.connect(_lampId);
    ble.seedSection(_lampId, 'wispstatus', _b(_split()));
    ble.seedSection(_lampId, 'wispconfig', _b('{}'));
    final c = makeContainer(ble);
    await built(c);
    final atBuild = ble.configReads;

    await Future<void>.delayed(const Duration(milliseconds: 3500));
    expect(ble.configReads - atBuild, 2);

    await ble.disconnect(_lampId);
    await settle();
    await ble.connect(_lampId);
    ble.seedSection(_lampId, 'wispconfig', _b(_config(px: 44)));
    await Future<void>.delayed(const Duration(milliseconds: 1500));

    expect(c.read(wispNotifierProvider(_lampId)).value!.pixelCount, 44);
  });

  // The drift echo reaches a split status only through the config path. A
  // status merged with the config held before the release must not snap the
  // form back.
  test('drift holds: a split status after a release keeps the new drift',
      () async {
    final ble = InMemoryBleClient();
    await ble.connect(_lampId);
    ble.seedSection(_lampId, 'wispstatus', _b(_split(configLastSeenMs: 100)));
    ble.seedSection(
      _lampId,
      'wispconfig',
      _b(_config(driftIntervalMs: 90000, driftFadePct: 40)),
    );
    final c = makeContainer(ble);
    await built(c);
    final n = c.read(wispNotifierProvider(_lampId).notifier);

    n.setDrift(300000, 70);
    n.flushDrift();
    await settle();
    notify(ble, _split(configLastSeenMs: 100));
    await settle();

    final s = c.read(wispNotifierProvider(_lampId)).value!;
    expect(s.driftIntervalMs, 300000);
    expect(s.driftFadePct, 70);
  });

  // The pre-read came back empty, so the held opSeq (4) may be stale. A
  // config later serving 5 must not confirm a change the wisp never applied.
  test('baseline needs a read: an empty pre-read refuses the change',
      () async {
    final ble = InMemoryBleClient();
    await ble.connect(_lampId);
    ble.seedSection(
      _lampId,
      'wispstatus',
      _b(_split(configLastSeenMs: 100, hasPassword: true)),
    );
    ble.seedSection(_lampId, 'wispconfig', _b(_config(opSeq: 4)));
    final c = makeContainer(ble);
    await built(c);
    final n = c.read(wispNotifierProvider(_lampId).notifier);
    ble.seedSection(_lampId, 'wispconfig', _b('{}'));

    final outcome = n
        .setPassword('newpass', currentPassword: 'wrongpass')
        .then<Object>((ok) => ok, onError: (Object e) => e);
    await Future.any<Object>([
      outcome,
      Future<Object>.delayed(const Duration(milliseconds: 200), () => 0),
    ]);
    ble.seedSection(_lampId, 'wispconfig', _b(_config(opSeq: 5)));
    notify(ble, _split(configLastSeenMs: 200, hasPassword: true));

    final result = await outcome;
    expect(result, isNot(true));
    expect(result, isA<StateError>());
    expect(ops(ble), isNot(contains('setPassword')));
  });

  // After the copy is gone, opSeq reaches the app only through a config
  // read: the op's status burst, the config behind it, the lamp's notify and
  // the page read, plus a lost frame or two.
  test('password confirm via config: an opSeq read 6 s after the send confirms',
      () async {
    final ble = InMemoryBleClient();
    await ble.connect(_lampId);
    ble.seedSection(
      _lampId,
      'wispstatus',
      _b(_split(configLastSeenMs: 100, hasPassword: true)),
    );
    ble.seedSection(_lampId, 'wispconfig', _b(_config(opSeq: 4)));
    final c = makeContainer(ble);
    await built(c);
    final n = c.read(wispNotifierProvider(_lampId).notifier);

    bool? confirmed;
    unawaited(
      n
          .setPassword('newpass', currentPassword: 'oldpass')
          .then((ok) => confirmed = ok),
    );
    await Future<void>.delayed(const Duration(seconds: 6));
    expect(ble.writesTo(_lampId, BleUuids.wispOp), isNotEmpty);
    expect(confirmed, isNull);

    ble.seedSection(_lampId, 'wispconfig', _b(_config(opSeq: 5)));
    notify(ble, _split(configLastSeenMs: 200, hasPassword: true));
    for (var i = 0; i < 20 && confirmed == null; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 10));
    }

    expect(confirmed, isTrue);
  });
}

/// Holds the read of one section or characteristic while [hold] is set, so a
/// test can leave that read in flight. The bytes are taken before the hold,
/// as a read the lamp already answered. Counts `wispconfig` reads.
class _HeldBle extends InMemoryBleClient {
  Completer<void>? hold;
  String? _target;
  bool held = false;
  int configReads = 0;

  void holdOn(String target) {
    _target = target;
    hold = Completer<void>();
  }

  Future<T> _maybeHold<T>(String target, Future<T> Function() read) async {
    T? value;
    Object? error;
    StackTrace? trace;
    try {
      value = await read();
    } catch (e, st) {
      error = e;
      trace = st;
    }
    final h = hold;
    if (h != null && target == _target) {
      held = true;
      await h.future;
    }
    if (error != null) Error.throwWithStackTrace(error, trace!);
    return value as T;
  }

  @override
  Future<Uint8List> read(String d, String s, String c) =>
      _maybeHold(c, () => super.read(d, s, c));

  @override
  Future<Uint8List> readSection(String deviceId, String name) {
    if (name == 'wispconfig') configReads++;
    return _maybeHold(name, () => super.readSection(deviceId, name));
  }
}
