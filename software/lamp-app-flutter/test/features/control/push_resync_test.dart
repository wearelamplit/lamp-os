import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/application/lamp_busy_exception.dart';
import 'package:lamp_app/features/firmware/application/firmware_notifier.dart';
import 'package:lamp_app/features/firmware/application/quiet_after_push.dart';
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

  /// Bumped per disconnect: like the lamp, a link drops its subscriptions.
  int _link = 0;

  @override
  Future<void> disconnect(String deviceId) {
    _link++;
    return super.disconnect(deviceId);
  }

  /// A reboot the fake never sees as a disconnect.
  void dropSubscriptions() => _link++;

  /// The `quiet` the lamp reports to a new CHAR_STATE_NOTIFY subscriber on
  /// its next loop tick, as the firmware does. Null reports nothing.
  bool? quietOnSubscribe;

  @override
  Stream<Uint8List> subscribeNotifyOnly(String d, String s, String c) {
    final link = _link;
    final quiet = quietOnSubscribe;
    if (c == BleUuids.stateNotify && quiet != null) {
      Timer.run(() => simulateNotify(d, s, c, _stateBytes(quiet: quiet)));
    }
    return super.subscribeNotifyOnly(d, s, c).where((_) => link == _link);
  }

  /// Writes to a characteristic here wait until its completer completes.
  final held = <String, Completer<void>>{};

  @override
  Future<void> write(
    String d,
    String s,
    String c,
    Uint8List v, {
    bool withoutResponse = false,
    bool allowLongWrite = false,
  }) async {
    await held[c]?.future;
    return super.write(
      d,
      s,
      c,
      v,
      withoutResponse: withoutResponse,
      allowLongWrite: allowLongWrite,
    );
  }

  /// Drops the link once the named section has been read.
  String? disconnectAfterReading;

  @override
  Future<Uint8List> readSection(String deviceId, String name) async {
    sectionReads++;
    final v = await super.readSection(deviceId, name);
    if (name == disconnectAfterReading) {
      disconnectAfterReading = null;
      await disconnect(deviceId);
      await Future<void>.delayed(Duration.zero);
    }
    return v;
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

const _failed = FirmwareFailed(
  reason: 'lost it',
  failure: FirmwareFailure.linkLost,
);

const _declined = FirmwareFailed(reason: 'busy', failure: FirmwareFailure.busy);

Uint8List _stateBytes({
  bool? quiet,
  bool previewActive = false,
  String? greetPeer,
}) => Uint8List.fromList(
  utf8.encode(
    jsonEncode({
      'previewActive': previewActive,
      'greeting': greetPeer == null
          ? {'active': false}
          : {'active': true, 'peer': greetPeer, 'kind': 'warm'},
      'nearbyRev': 0,
      'quiet': ?quiet,
    }),
  ),
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
  bool get frozen => c.read(quietAfterPushProvider(_id));
  bool get connected => c.read(controlNotifierProvider(_id)).value!.connected;

  /// CHAR_STATE_NOTIFY from the lamp. [quiet] null is firmware without the
  /// field.
  void notify({bool? quiet, bool previewActive = false, String? greetPeer}) {
    ble.simulateNotify(
      _id,
      BleUuids.controlService,
      BleUuids.stateNotify,
      _stateBytes(
        quiet: quiet,
        previewActive: previewActive,
        greetPeer: greetPeer,
      ),
    );
  }

  int mark() => ble.writesToDevice(_id).length;

  /// Writes since [mark], each as the characteristic then the payload.
  List<List<Object>> writesSince(int mark) =>
      ble.writesToDevice(_id).skip(mark).toList();

  /// Payloads written to [charUuid] since [mark].
  List<List<Object>> payloadsSince(int mark, String charUuid) => [
    for (final w in writesSince(mark))
      if (w.first == charUuid) w.sublist(1),
  ];

  /// The last Home Mode focus payload since [mark], or null.
  List<Object>? focusSince(int mark) {
    final focus = payloadsSince(mark, BleUuids.homeModeFocus);
    return focus.isEmpty ? null : focus.last;
  }

  Future<void> reconnect() async {
    await ble.disconnect(_id);
    await _pump();
    await ble.connect(_id);
    await _pump();
  }
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

/// Home Mode open, a push the lamp goes quiet for, Home Mode left and a
/// brightness write sent while the lamp drops them, then the push ends.
Future<_Rig> _quietPushEnding(FirmwareState end) async {
  final r = await _rig();
  r.notify(quiet: false);
  r.control.setHomeModeFocus(true);
  r.firmware.emit(_streaming);
  r.notify(quiet: true);
  await _pump();
  r.control.setHomeModeFocus(false);
  await r.control.setBrightness(90);
  await _pump();
  r.firmware.emit(end);
  await _pump();
  return r;
}

void main() {
  group('a lamp that reports quiet', () {
    for (final (label, end) in [
      ('fails', _failed as FirmwareState),
      ('is cancelled', const FirmwareIdle()),
    ]) {
      test('after a push that $label, editing stays frozen until the lamp '
          'listens, then the dropped focus goes out and the lamp is re-read '
          'once', () async {
        final r = await _quietPushEnding(end);
        final reads = r.ble.sectionReads;
        final mark = r.mark();
        expect(r.frozen, isTrue);
        expect(r.writesSince(mark), isEmpty, reason: 'the lamp is quiet');
        expect(r.ble.sectionReads, reads);

        r.notify(quiet: false);
        await _pump();
        expect(r.frozen, isFalse);
        expect(r.focusSince(mark), [0], reason: 'Home Mode was left');
        expect(r.brightness, 50, reason: 'the lamp dropped the write');
        final afterSync = r.ble.sectionReads;
        expect(afterSync, greaterThan(reads));

        final again = r.mark();
        r.notify(quiet: false);
        await _pump();
        expect(r.writesSince(again), isEmpty);
        expect(r.ble.sectionReads, afterSync);
      });
    }

    test('a lamp already listening when the push ends is re-synced then, '
        'never mid-push', () async {
      final r = await _rig();
      r.control.setHomeModeFocus(true);
      r.firmware.emit(_streaming);
      r.notify(quiet: true);
      await _pump();
      final reads = r.ble.sectionReads;
      final mark = r.mark();
      r.notify(quiet: false);
      await _pump();
      expect(r.ble.sectionReads, reads, reason: 'still pushing');
      expect(r.writesSince(mark), isEmpty);

      r.firmware.emit(_failed);
      await _pump();
      expect(r.frozen, isFalse);
      expect(r.focusSince(mark), [1]);
      expect(r.ble.sectionReads, greaterThan(reads));
    });

    test(
      'a session flag dropped mid-push goes out once the lamp listens',
      () async {
        final r = await _rig();
        r.notify(quiet: false);
        r.control.setEditSession(EditSurface.base, true);
        await r.control.setSocialViewActive(true);
        await _pump();
        r.firmware.emit(_streaming);
        r.notify(quiet: true);
        await _pump();
        r.control.setEditSession(EditSurface.base, false);
        await r.control.setSocialViewActive(false);
        await _pump();
        r.firmware.emit(_failed);
        await _pump();

        final mark = r.mark();
        r.notify(quiet: false);
        await _pump();
        final sessions = r.payloadsSince(mark, BleUuids.editSession);
        expect(
          sessions,
          containsAll([
            [EditSurface.base.bit, 0],
            [0x08, 0],
          ]),
        );
      },
    );

    test('the re-read keeps what only the state notify reports', () async {
      final r = await _quietPushEnding(_failed);
      r.notify(quiet: false, previewActive: true, greetPeer: 'AA:BB');
      await _pump();
      final s = r.c.read(controlNotifierProvider(_id)).value!;
      expect(s.previewActive, isTrue);
      expect(s.greeting?.peer, 'AA:BB');
    });

    test('a link lost during the re-read stays lost', () async {
      final r = await _quietPushEnding(_failed);
      r.ble.disconnectAfterReading = 'expr';
      r.notify(quiet: false);
      await Future<void>.delayed(const Duration(milliseconds: 100));
      expect(r.connected, isFalse);
    });

    test('a push lost with the link stays frozen across the reconnect until '
        'the lamp listens, then Home Mode focus goes back', () async {
      final r = await _rig();
      r.notify(quiet: false);
      r.control.setHomeModeFocus(true);
      r.firmware.emit(_streaming);
      r.notify(quiet: true);
      await _pump();
      await r.ble.disconnect(_id);
      await _pump();
      r.firmware.emit(_failed);
      await _pump();
      await r.ble.connect(_id);
      await _pump();
      expect(r.connected, isTrue);

      final mark = r.mark();
      final reads = r.ble.sectionReads;
      r.notify(quiet: true);
      await _pump();
      expect(r.frozen, isTrue, reason: 'the lamp is still quiet');
      expect(r.focusSince(mark), isNull);

      r.notify(quiet: false);
      await _pump();
      expect(r.frozen, isFalse);
      expect(r.focusSince(mark), [1], reason: 'the reconnect cleared it');
      expect(r.ble.sectionReads, reads, reason: 'the reconnect re-read it');
    });
  });

  test('a lamp that stops reporting quiet after a reconnect is treated as one '
      'that never did', () async {
    final r = await _rig();
    r.notify(quiet: false);
    await _pump();
    await r.reconnect();
    r.notify();
    r.firmware.emit(_streaming);
    await _pump();
    r.firmware.emit(_failed);
    await _pump();
    final mark = r.mark();
    await r.control.setBrightness(60);
    await _pump();
    expect(r.writesSince(mark), [
      [BleUuids.homeModeFocus, 0],
      [BleUuids.brightness, 60],
    ]);
  });

  test('state notifies reach the app after a save reboots the lamp', () async {
    final r = await _rig();
    await r.control.setLampPassword('');
    r.ble.dropSubscriptions();
    await Future<void>.delayed(const Duration(seconds: 3));
    expect(r.connected, isTrue);
    r.notify(quiet: false, previewActive: true);
    await _pump();
    expect(r.c.read(controlNotifierProvider(_id)).value!.previewActive, isTrue);
  });

  test(
    'a reconnect sends Home Mode focus back once the lamp listens',
    () async {
      final r = await _rig();
      r.notify(quiet: false);
      r.control.setHomeModeFocus(true);
      await _pump();
      await r.reconnect();
      final mark = r.mark();
      final reads = r.ble.sectionReads;
      r.notify(quiet: false);
      await _pump();
      expect(r.focusSince(mark), [1]);
      expect(r.ble.sectionReads, reads);
    },
  );

  group('push boundaries', () {
    test(
      'a push that fails at the local verify sends and reads nothing',
      () async {
        final r = await _rig();
        r.notify(quiet: false);
        r.control.setHomeModeFocus(true);
        await _pump();
        final reads = r.ble.sectionReads;
        final mark = r.mark();
        r.firmware.emit(const FirmwareVerifying());
        await _pump();
        r.firmware.emit(_failed);
        await _pump();
        expect(r.ble.sectionReads, reads);
        expect(r.writesSince(mark), isEmpty);
        expect(r.frozen, isFalse);
      },
    );

    test('progress within a push, and dismissing its result, re-sync '
        'nothing', () async {
      for (final quiet in [null, false]) {
        final r = await _rig();
        r.notify(quiet: quiet);
        r.firmware.emit(FirmwareOfferSent(footer: _footer));
        await _pump();
        final reads = r.ble.sectionReads;
        r.firmware.emit(_streaming);
        if (quiet == false) {
          r.notify(quiet: true);
          r.notify(quiet: false);
        }
        await _pump();
        r.firmware.emit(
          FirmwareStreaming(footer: _footer, chunksSent: 4, totalChunks: 10),
        );
        await _pump();
        expect(r.ble.sectionReads, reads, reason: 'still pushing ($quiet)');
        r.firmware.emit(_failed);
        await _pump();
        final afterEnd = r.ble.sectionReads;
        expect(afterEnd, greaterThan(reads));
        r.firmware.emit(const FirmwareIdle());
        await _pump();
        expect(r.ble.sectionReads, afterEnd, reason: 'no push ($quiet)');
      }
    });

    test('a push that succeeds leaves the re-read to the reboot', () async {
      for (final quiet in [null, true]) {
        final r = await _rig();
        r.firmware.emit(_streaming);
        r.notify(quiet: quiet);
        await _pump();
        final reads = r.ble.sectionReads;
        r.firmware.emit(FirmwareSucceeded(footer: _footer));
        await _pump();
        expect(r.ble.sectionReads, reads);
        expect(r.frozen, isFalse);
        r.firmware.emit(const FirmwareIdle());
        await _pump();
        expect(r.ble.sectionReads, reads);
      }
    });
  });

  group('a lamp that never reports quiet', () {
    for (final (label, end) in [
      ('fails', _failed as FirmwareState),
      ('is cancelled', const FirmwareIdle()),
    ]) {
      test(
        'a push that $label re-reads the lamp at once, without a freeze',
        () async {
          final r = await _rig();
          r.notify();
          r.firmware.emit(_streaming);
          await _pump();
          await r.control.setBrightness(90);
          expect(r.brightness, 90);

          r.firmware.emit(end);
          await _pump();
          expect(r.brightness, 50, reason: 'the lamp dropped the write');
          expect(r.frozen, isFalse);
        },
      );
    }

    for (final open in [false, true]) {
      test('after a failed push, Home Mode focus (open: $open) goes out just '
          'before each brightness write', () async {
        final r = await _rig();
        r.control.setHomeModeFocus(!open);
        r.firmware.emit(_streaming);
        await _pump();
        r.control.setHomeModeFocus(open);
        r.firmware.emit(_failed);
        await _pump();

        for (final level in [60, 70]) {
          final mark = r.mark();
          await r.control.setBrightness(level);
          await _pump();
          expect(r.writesSince(mark), [
            [BleUuids.homeModeFocus, open ? 1 : 0],
            [BleUuids.brightness, level],
          ]);
        }
      });
    }

    test('a push-free reconnect ends the focus re-sends', () async {
      final r = await _rig();
      r.firmware.emit(_streaming);
      await _pump();
      r.firmware.emit(_failed);
      await _pump();

      await r.reconnect();
      final mark = r.mark();
      await r.control.setBrightness(60);
      await _pump();
      expect(r.writesSince(mark), [
        [BleUuids.brightness, 60],
      ]);
    });
  });

  test('a notify without quiet re-sends nothing', () async {
    final r = await _rig();
    r.control.setHomeModeFocus(true);
    await r.reconnect();
    final mark = r.mark();
    r.notify();
    await _pump();
    expect(r.writesSince(mark), isEmpty);
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
      expect(r.focusSince(0), [1]);
      await tester.pumpWidget(const SizedBox.shrink());
      expect(r.focusSince(0), [0]);
    },
  );

  testWidgets('a rebuilt notifier sends Home Mode focus back once the lamp '
      'listens', (tester) async {
    late _Rig r;
    await tester.runAsync(() async => r = await _rig());
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: r.c,
        child: const MaterialApp(home: HomeModeScreen(lampId: _id)),
      ),
    );
    await tester.pump();
    await tester.runAsync(() async {
      r.c.invalidate(controlNotifierProvider(_id));
      await r.c.read(controlNotifierProvider(_id).future);
      await _pump();
      final mark = r.mark();
      r.notify(quiet: false);
      await _pump();
      expect(r.focusSince(mark), [1], reason: 'Home Mode is still open');
    });
    await tester.pumpWidget(const SizedBox.shrink());
  });

  group('a lamp that reports quiet with no push in hand', () {
    test('freezes editing, then once it listens sends the held focus and '
        "takes the lamp's word for an edit sent before the freeze", () async {
      final r = await _rig();
      r.control.setHomeModeFocus(true);
      await r.control.setBrightness(90);
      await _pump();
      r.notify(quiet: true);
      await _pump();
      expect(r.frozen, isTrue);

      final reads = r.ble.sectionReads;
      final mark = r.mark();
      r.notify(quiet: false);
      await _pump();
      expect(r.frozen, isFalse);
      expect(r.focusSince(mark), [1]);
      expect(r.brightness, 50, reason: 'the lamp dropped the write');
      expect(r.ble.sectionReads, greaterThan(reads));
    });

    test('during a local verify that then fails, freezes once the verify '
        'ends', () async {
      final r = await _rig();
      r.firmware.emit(const FirmwareVerifying());
      r.notify(quiet: true);
      await _pump();
      r.firmware.emit(
        const FirmwareFailed(
          reason: 'bad image',
          failure: FirmwareFailure.cacheUnverified,
        ),
      );
      await _pump();
      expect(r.frozen, isTrue);
    });

    test(
      'a quiet report while the lamp is re-synced keeps the freeze',
      () async {
        final r = await _rig();
        r.control.setHomeModeFocus(true);
        r.notify(quiet: true);
        await _pump();
        final gate = r.ble.held[BleUuids.homeModeFocus] = Completer<void>();
        r.notify(quiet: false);
        await _pump();
        r.notify(quiet: true);
        await _pump();
        gate.complete();
        await _pump();
        expect(r.frozen, isTrue);
      },
    );
  });

  group('a push the lamp never went quiet for', () {
    test('sends and reads nothing on a lamp that reports quiet', () async {
      final r = await _rig();
      r.notify(quiet: false);
      r.control.setHomeModeFocus(true);
      await _pump();
      final reads = r.ble.sectionReads;
      final mark = r.mark();
      r.firmware.emit(FirmwareOfferSent(footer: _footer));
      await _pump();
      r.firmware.emit(_declined);
      await _pump();
      expect(r.writesSince(mark), isEmpty);
      expect(r.ble.sectionReads, reads);
      expect(r.frozen, isFalse);
    });

    test(
      'still sends flags the new link never got, without a re-read',
      () async {
        final r = await _rig();
        r.control.setHomeModeFocus(true);
        r.firmware.emit(FirmwareOfferSent(footer: _footer));
        r.notify(quiet: false);
        await _pump();
        final reads = r.ble.sectionReads;
        final mark = r.mark();
        r.firmware.emit(_declined);
        await _pump();
        expect(r.focusSince(mark), [1]);
        expect(r.ble.sectionReads, reads);
      },
    );

    test('a declined offer re-reads nothing on a lamp without quiet, and '
        'sends no focus ahead of brightness', () async {
      final r = await _rig();
      r.notify();
      await _pump();
      final reads = r.ble.sectionReads;
      r.firmware.emit(FirmwareOfferSent(footer: _footer));
      await _pump();
      r.firmware.emit(_declined);
      await _pump();
      expect(r.ble.sectionReads, reads);
      final mark = r.mark();
      await r.control.setBrightness(60);
      await _pump();
      expect(r.writesSince(mark), [
        [BleUuids.brightness, 60],
      ]);
    });

    test(
      'an offer left unanswered still re-reads a lamp without quiet',
      () async {
        final r = await _rig();
        r.notify();
        await _pump();
        final reads = r.ble.sectionReads;
        r.firmware.emit(FirmwareOfferSent(footer: _footer));
        await _pump();
        r.firmware.emit(
          const FirmwareFailed(
            reason: 'no accept',
            failure: FirmwareFailure.noAnswer,
          ),
        );
        await _pump();
        expect(r.ble.sectionReads, greaterThan(reads));
      },
    );
  });

  test('after Retry from an error, Home Mode focus goes back when the lamp '
      'reports on subscribe, mid-rebuild included', () async {
    final r = await _rig();
    r.notify(quiet: false);
    r.control.setHomeModeFocus(true);
    await _pump();
    final base = await r.ble.readSection(_id, 'base');
    r.ble.seedSection(_id, 'base', Uint8List.fromList([0x21]));
    await r.reconnect();
    await Future<void>.delayed(const Duration(seconds: 2));
    expect(
      r.c.read(controlNotifierProvider(_id)).error,
      isA<LampBusyException>(),
    );

    r.ble.seedSection(_id, 'base', base);
    r.ble.quietOnSubscribe = false;
    final gate = r.ble.held[BleUuids.expressionTest] = Completer<void>();
    final mark = r.mark();
    r.c.invalidate(controlNotifierProvider(_id));
    final rebuilt = r.c.read(controlNotifierProvider(_id).future);
    await _pump();
    gate.complete();
    await rebuilt;
    await _pump();
    expect(r.focusSince(mark), [1]);
  });

  group('a new link sends a held Social open at once', () {
    Future<_Rig> socialOpenThenQuiet() async {
      final r = await _rig();
      r.notify(quiet: false);
      await r.control.setSocialViewActive(true);
      r.firmware.emit(_streaming);
      r.notify(quiet: true);
      await _pump();
      return r;
    }

    test('after a reconnect while the lamp is quiet', () async {
      final r = await socialOpenThenQuiet();
      await r.ble.disconnect(_id);
      await _pump();
      r.firmware.emit(_failed);
      await _pump();
      final mark = r.mark();
      await r.ble.connect(_id);
      await _pump();
      expect(r.connected, isTrue);
      expect(r.payloadsSince(mark, BleUuids.editSession), [
        [0x08, 1],
      ]);
    });

    test('after a rebuild', () async {
      final r = await _rig();
      await r.control.setSocialViewActive(true);
      final mark = r.mark();
      r.c.invalidate(controlNotifierProvider(_id));
      await r.c.read(controlNotifierProvider(_id).future);
      await _pump();
      expect(r.payloadsSince(mark, BleUuids.editSession), [
        [0x08, 1],
      ]);
    });

    test('after a save reboots the lamp', () async {
      final r = await _rig();
      await r.control.setSocialViewActive(true);
      final mark = r.mark();
      await r.control.setLampPassword('');
      r.ble.dropSubscriptions();
      await Future<void>.delayed(const Duration(seconds: 3));
      expect(r.connected, isTrue);
      expect(r.payloadsSince(mark, BleUuids.editSession), [
        [0x08, 1],
      ]);
    });

    test('but not a held close', () async {
      final r = await _rig();
      await r.control.setSocialViewActive(true);
      await r.control.setSocialViewActive(false);
      final mark = r.mark();
      await r.reconnect();
      expect(r.connected, isTrue);
      expect(r.payloadsSince(mark, BleUuids.editSession), isEmpty);
    });
  });

  test(
    'a notifier disposed mid-build leaves no state listener behind',
    () async {
      final r = await _rig();
      final gate = r.ble.held[BleUuids.expressionTest] = Completer<void>();
      r.c.invalidate(controlNotifierProvider(_id));
      await _pump();
      r.c.dispose();
      await r.ble.connect(_id);
      gate.complete();
      await _pump();
      r.notify(quiet: false);
      await _pump();
    },
  );
}
