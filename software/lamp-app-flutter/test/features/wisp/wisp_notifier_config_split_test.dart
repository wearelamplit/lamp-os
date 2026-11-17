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

  // A new configLastSeenMs lands while another read holds the link; the read
  // it owes runs once the link is free, not on the next notify.
  test('skipped read retried once reads go idle', () async {
    final ble = _HeldClaimsBle();
    await ble.connect(_lampId);
    ble.seedSection(_lampId, 'wispstatus', _b(_split(configLastSeenMs: 100)));
    ble.seedSection(_lampId, 'wispconfig', _b(_config(px: 60)));
    final c = makeContainer(ble);
    await built(c);

    ble.holdClaims = Completer<void>();
    notify(ble, _split(configLastSeenMs: 100));
    await settle();
    expect(ble.claimsHeld, isTrue);

    ble.seedSection(_lampId, 'wispconfig', _b(_config(px: 44)));
    notify(ble, _split(configLastSeenMs: 200));
    await settle();
    expect(c.read(wispNotifierProvider(_lampId)).value!.pixelCount, 60);

    ble.holdClaims!.complete();
    ble.holdClaims = null;
    for (var i = 0; i < 10; i++) {
      await settle();
    }

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

/// Holds every CHAR_WISP_CLAIMS read while [holdClaims] is set, so a test can
/// leave a claims read in flight.
class _HeldClaimsBle extends InMemoryBleClient {
  Completer<void>? holdClaims;
  bool claimsHeld = false;

  @override
  Future<Uint8List> read(String d, String s, String c) async {
    final hold = holdClaims;
    if (c == BleUuids.wispClaims && hold != null) {
      claimsHeld = true;
      await hold.future;
      claimsHeld = false;
    }
    return super.read(d, s, c);
  }
}
