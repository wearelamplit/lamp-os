import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';

const _lampId = 'test-lamp';
const _mac = 'AA:BB:CC:DD:EE:FF';

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

String _config({int px = 60, int brightness = 80, int opSeq = 0}) =>
    '{"char":"wispConfig","ledType":"GRBW","px":$px,"brightness":$brightness,'
    '"driftIntervalMs":90000,"driftFadePct":40,"shuffleSeed":7,"opSeq":$opSeq}';

/// Section-vs-fallback selection and the config merge. A lamp that serves
/// `wispstatus` answers it with at least `"{}"`; the fake's unseeded section
/// returns 0 bytes, the same as a lamp that predates it.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUp(() => SharedPreferences.setMockInitialValues({}));

  Future<InMemoryBleClient> connectedBle({String? directStatus}) async {
    final ble = InMemoryBleClient();
    await ble.connect(_lampId);
    if (directStatus != null) {
      await ble.write(
        _lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        _b(directStatus),
      );
    }
    return ble;
  }

  ProviderContainer makeContainer(InMemoryBleClient ble) {
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    return c;
  }

  Future<void> settle() => Future<void>.delayed(Duration.zero);

  // Wait until the sealed op has gone out, i.e. its baseline is fixed.
  Future<void> opSent(InMemoryBleClient ble) async {
    for (var i = 0; i < 200; i++) {
      if (ble.writesTo(_lampId, BleUuids.wispOp).isNotEmpty) return;
      await Future<void>.delayed(const Duration(milliseconds: 5));
    }
    fail('the sealed op was never written');
  }

  // Build and keep the auto-dispose provider alive, then let build's
  // trailing claims read finish so a re-read the test triggers is not
  // skipped as busy.
  Future<void> built(ProviderContainer c) async {
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await settle();
  }

  group('WispRepository section selection', () {
    test('reads the wispstatus section when the lamp serves it', () async {
      final ble = await connectedBle(
        directStatus: '{"wispMac":"$_mac","name":"direct"}',
      );
      ble.seedSection(
        _lampId,
        'wispstatus',
        _b('{"wispMac":"$_mac","name":"section"}'),
      );
      final repo = WispRepository(ble, _lampId);

      final s = await repo.readStatus();

      expect(s.name, 'section');
      expect(repo.wispSectionsSupported, isTrue);
    });

    test(
      'falls back to CHAR_WISP_STATUS on a lamp without the section',
      () async {
        final ble = await connectedBle(
          directStatus: '{"wispMac":"$_mac","name":"direct"}',
        );
        final repo = WispRepository(ble, _lampId);

        final s = await repo.readStatus();

        expect(s.name, 'direct');
        expect(repo.wispSectionsSupported, isFalse);
        expect(await repo.readConfig(), isNull);
      },
    );

    test('"{}" from wispconfig (none cached yet) reads as no config', () async {
      final ble = await connectedBle();
      ble.seedSection(_lampId, 'wispstatus', _b('{}'));
      ble.seedSection(_lampId, 'wispconfig', _b('{}'));
      final repo = WispRepository(ble, _lampId);

      await repo.readStatus();

      expect(repo.wispSectionsSupported, isTrue);
      expect(await repo.readConfig(), isNull);
    });

    test(
      'pollConfig writes a plaintext pollConfig wispOp even with a password',
      () async {
        final ble = await connectedBle();
        final repo = WispRepository(ble, _lampId)..updatePassword('secret');

        await repo.pollConfig();

        final written = ble.writesTo(_lampId, BleUuids.wispOp).single;
        final decoded =
            jsonDecode(utf8.decode(written)) as Map<String, dynamic>;
        expect(decoded['char'], 'wispOp');
        expect(decoded['op'], 'pollConfig');
      },
    );
  });

  group('WispNotifier config merge', () {
    test('build merges the wispconfig section over the status', () async {
      final ble = await connectedBle();
      ble.seedSection(
        _lampId,
        'wispstatus',
        _b(
          '{"wispMac":"$_mac","configSplit":true,"source":"osc",'
          '"configLastSeenMs":100}',
        ),
      );
      ble.seedSection(_lampId, 'wispconfig', _b(_config()));
      final c = makeContainer(ble);

      final s = await c.read(wispNotifierProvider(_lampId).future);

      expect(s.configKnown, isTrue);
      expect(s.pixelCount, 60);
      expect(s.brightness, 80);
      expect(s.ledType, 'GRBW');
    });

    // The status says its config lives elsewhere and the lamp has none yet:
    // the forms must see "unknown", not the parser's defaults.
    test('split status with no config cached stays unknown', () async {
      final ble = await connectedBle();
      ble.seedSection(
        _lampId,
        'wispstatus',
        _b('{"wispMac":"$_mac","configSplit":true,"source":"osc"}'),
      );
      ble.seedSection(_lampId, 'wispconfig', _b('{}'));
      final c = makeContainer(ble);

      final s = await c.read(wispNotifierProvider(_lampId).future);

      expect(s.present, isTrue);
      expect(s.configKnown, isFalse);
    });

    test('a new configLastSeenMs on a notify re-reads the section', () async {
      final ble = await connectedBle();
      ble.seedSection(
        _lampId,
        'wispstatus',
        _b(
          '{"wispMac":"$_mac","configSplit":true,"source":"osc",'
          '"configLastSeenMs":100}',
        ),
      );
      ble.seedSection(_lampId, 'wispconfig', _b(_config(brightness: 80)));
      final c = makeContainer(ble);
      await built(c);

      ble.seedSection(_lampId, 'wispconfig', _b(_config(brightness: 25)));
      ble.simulateNotify(
        _lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        _b(
          '{"wispMac":"$_mac","configSplit":true,"source":"osc",'
          '"configLastSeenMs":200}',
        ),
      );
      await settle();
      await settle();

      expect(c.read(wispNotifierProvider(_lampId)).value!.brightness, 25);
    });

    // A rival wisp took the slot: the old wisp's config must not dress it.
    test('a held config never merges onto another wisp', () async {
      final ble = await connectedBle();
      ble.seedSection(
        _lampId,
        'wispstatus',
        _b('{"wispMac":"$_mac","configSplit":true,"source":"osc","configLastSeenMs":100}'),
      );
      ble.seedSection(_lampId, 'wispconfig', _b(_config(px: 60)));
      final c = makeContainer(ble);
      await built(c);

      ble.seedSection(_lampId, 'wispconfig', _b('{}'));
      ble.simulateNotify(
        _lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        _b(
          '{"wispMac":"11:22:33:44:55:66","configSplit":true,'
          '"source":"osc"}',
        ),
      );
      await settle();

      final s = c.read(wispNotifierProvider(_lampId)).value!;
      expect(s.wispMac, '11:22:33:44:55:66');
      expect(s.configKnown, isFalse);
    });

    // An empty or MTU-truncated notify on a lamp with the section: the
    // update is recovered from the section instead of dropped.
    test('an unparseable notify re-reads the wispstatus section', () async {
      final ble = await connectedBle();
      ble.seedSection(
        _lampId,
        'wispstatus',
        _b('{"wispMac":"$_mac","configSplit":true,"source":"off"}'),
      );
      final c = makeContainer(ble);
      await built(c);

      ble.seedSection(
        _lampId,
        'wispstatus',
        _b('{"wispMac":"$_mac","configSplit":true,"source":"manual","name":"whole"}'),
      );
      ble.simulateNotify(
        _lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        _b('{"wispMac":"$_mac","sour'),
      );
      await settle();
      await settle();

      final s = c.read(wispNotifierProvider(_lampId)).value!;
      expect(s.name, 'whole');
    });

    // opSeq rides the config: a sealed op confirms off a config re-read.
    test('a sealed password op confirms from the config section', () async {
      final ble = await connectedBle();
      ble.seedSection(
        _lampId,
        'wispstatus',
        _b(
          '{"wispMac":"$_mac","configSplit":true,"source":"osc",'
          '"hasPassword":true,"configLastSeenMs":100}',
        ),
      );
      ble.seedSection(_lampId, 'wispconfig', _b(_config(opSeq: 4)));
      final c = makeContainer(ble);
      await built(c);
      final n = c.read(wispNotifierProvider(_lampId).notifier);

      final future = n.setPassword('newpass', currentPassword: 'oldpass');
      await opSent(ble);
      ble.seedSection(_lampId, 'wispconfig', _b(_config(opSeq: 5)));
      ble.simulateNotify(
        _lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        _b(
          '{"wispMac":"$_mac","configSplit":true,"source":"osc",'
          '"hasPassword":true,"configLastSeenMs":200}',
        ),
      );

      expect(await future, isTrue);
    });

    // A config read already in flight when the op starts lands opSeq 6. The
    // op waits it out and reads its own baseline (6), rather than skipping
    // as busy and baselining from the 4 it held, which the in-flight read's
    // 6 would then falsely confirm.
    test('a password op waits for an in-flight config read', () async {
      final ble = _HeldConfigBle();
      await ble.connect(_lampId);
      const split =
          '{"wispMac":"$_mac","configSplit":true,"source":"osc",'
          '"hasPassword":true,"configLastSeenMs":100}';
      ble.seedSection(_lampId, 'wispstatus', _b(split));
      ble.seedSection(_lampId, 'wispconfig', _b(_config(opSeq: 4)));
      final c = makeContainer(ble);
      await built(c);
      final n = c.read(wispNotifierProvider(_lampId).notifier);

      ble.holdConfig = Completer<void>();
      ble.seedSection(_lampId, 'wispconfig', _b(_config(opSeq: 6)));
      ble.simulateNotify(
        _lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        _b(
          split.replaceFirst(
            '"configLastSeenMs":100',
            '"configLastSeenMs":200',
          ),
        ),
      );
      await settle();

      final future = n.setPassword('newpass', currentPassword: 'wrongpass');
      await settle();
      ble.holdConfig!.complete();
      ble.holdConfig = null;

      expect(await future, isFalse);
    });

    // Every read at build landed before the auth handshake, so the sections
    // looked absent. A notify carrying configLastSeenMs, which only a lamp
    // with the sections merges, turns them on and the config is read.
    test('a notify with configLastSeenMs enables the sections', () async {
      final ble = await connectedBle(directStatus: '{"wispMac":"$_mac"}');
      final c = makeContainer(ble);
      await built(c);

      ble.seedSection(_lampId, 'wispconfig', _b(_config(px: 44)));
      ble.simulateNotify(
        _lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        _b(
          '{"wispMac":"$_mac","configSplit":true,"source":"osc",'
          '"configLastSeenMs":300}',
        ),
      );
      await settle();
      await settle();

      final s = c.read(wispNotifierProvider(_lampId)).value!;
      expect(s.configKnown, isTrue);
      expect(s.pixelCount, 44);
    });

    // A split status with nothing to read: refuse rather than baseline from
    // a placeholder 0.
    test('a split status with no config refuses a sealed op', () async {
      final ble = await connectedBle();
      ble.seedSection(
        _lampId,
        'wispstatus',
        _b(
          '{"wispMac":"$_mac","configSplit":true,"source":"osc",'
          '"hasPassword":true}',
        ),
      );
      ble.seedSection(_lampId, 'wispconfig', _b('{}'));
      final c = makeContainer(ble);
      await built(c);
      final n = c.read(wispNotifierProvider(_lampId).notifier);

      await expectLater(
        n.setPassword('newpass', currentPassword: 'oldpass'),
        throwsStateError,
      );
      expect(ble.writesTo(_lampId, BleUuids.wispOp), isEmpty);
    });

    test('pollConfig writes once inside the rate-limit window', () async {
      final ble = await connectedBle(directStatus: '{"wispMac":"$_mac"}');
      final c = makeContainer(ble);
      await built(c);
      final n = c.read(wispNotifierProvider(_lampId).notifier);

      await n.pollConfig();
      await n.pollConfig();

      final ops = ble
          .writesTo(_lampId, BleUuids.wispOp)
          .map(
            (w) => (jsonDecode(utf8.decode(w)) as Map<String, dynamic>)['op'],
          )
          .toList();
      expect(ops, ['pollConfig']);
    });
  });
}

/// Holds every `wispconfig` read while [holdConfig] is set, so a test can
/// leave a config read in flight.
class _HeldConfigBle extends InMemoryBleClient {
  Completer<void>? holdConfig;

  @override
  Future<Uint8List> readSection(String deviceId, String name) async {
    final hold = holdConfig;
    if (name == 'wispconfig' && hold != null) await hold.future;
    return super.readSection(deviceId, name);
  }
}
