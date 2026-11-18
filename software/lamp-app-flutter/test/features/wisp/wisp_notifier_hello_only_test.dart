import 'dart:async';
import 'dart:convert';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/wisp_palette_ble.dart';

const _lampId = 'hello-only-lamp';

const _p = [
  LampColor(r: 200, g: 0, b: 30, w: 0),
  LampColor(r: 0, g: 60, b: 90, w: 15),
];
const _q = [LampColor(r: 5, g: 6, b: 7, w: 0)];

final _defaultSeqStart = WispNotifier.debugSeqStart;

/// Until the app holds a full status from the wisp it shows, the lamp's
/// palette cache may be empty or another wisp's, so the Manual palette stays
/// unknown and the editor cannot save over the wisp's own.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUp(() => SharedPreferences.setMockInitialValues({}));

  // The lamp just booted: it holds the wisp's HELLO and whatever palette
  // reached it, an empty cache included.
  for (final cached in [const <LampColor>[], _q]) {
    test('a HELLO-only status at open leaves the palette unknown '
        '(${cached.length}-color lamp cache)', () async {
      final ble = PaletteBle(_lampId);
      ble.seedPalette(cached);
      final n = (await openWisp(ble, helloOnlyStatus(wispA))).$2;
      expect(n.paletteLoading, isTrue);

      ble.seedPalette(_p);
      ble.notifyStatus(statingStatus(wispA, _p));
      await pumpEventQueue();
      expect(n.paletteLoading, isFalse);
      expect(n.savedManualPalette, _p);
    });
  }

  // The lamp adopts wisp B and serves only B's HELLO.
  test('display switch to a HELLO-only wisp returns the palette to unknown',
      () async {
    final ble = PaletteBle(_lampId);
    ble.seedPalette(_p);
    final n = (await openWisp(ble, statingStatus(wispA, _p))).$2;
    expect(n.paletteLoading, isFalse);

    ble.seedPalette(const []);
    ble.notifyStatus(helloOnlyStatus(wispB));
    await pumpEventQueue();
    expect(n.paletteLoading, isTrue);

    ble.seedPalette(_q);
    ble.notifyStatus(statingStatus(wispB, _q));
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse);
    expect(n.savedManualPalette, _q);
  });

  // A read started for a full status lands once the lamp serves only a
  // HELLO, from that wisp (the lamp lost its status) or another one.
  for (final (name, mac) in [('the same wisp', wispA), ('another wisp', wispB)]) {
    test('a read landing after a HELLO-only status from $name does not count',
        () async {
      final ble = PaletteBle(_lampId);
      ble.seedPalette(const []);
      final n = (await openWisp(ble, statingStatus(wispA, const []))).$2;
      expect(n.paletteLoading, isFalse);

      ble.seedPalette(_p);
      ble.holdPalette = Completer<void>();
      final before = ble.paletteReads;
      ble.notifyStatus(statingStatus(wispA, _p));
      await pumpEventQueue();
      expect(ble.paletteReads, before + 1, reason: 'no read in flight');

      ble.notifyStatus(helloOnlyStatus(mac));
      await pumpEventQueue();
      ble.holdPalette!.complete();
      ble.holdPalette = null;
      await pumpEventQueue();

      expect(n.paletteLoading, isTrue);
    });
  }

  // The lamp relays wisp B's full status by the time a read started for A
  // lands.
  test('a read landing after a full status from another wisp does not count',
      () async {
    final ble = PaletteBle(_lampId);
    ble.seedPalette(const []);
    final n = (await openWisp(ble, statingStatus(wispA, const []))).$2;
    expect(n.paletteLoading, isFalse);

    ble.seedPalette(_p);
    ble.holdPalette = Completer<void>();
    ble.notifyStatus(statingStatus(wispA, _p));
    await pumpEventQueue();
    ble.notifyStatus(statingStatus(wispB, _q));
    await pumpEventQueue();
    ble.holdPalette!.complete();
    ble.holdPalette = null;
    await pumpEventQueue();

    expect(n.paletteLoading, isTrue);
  });

  test('a HELLO-only status starts no palette read', () async {
    final ble = PaletteBle(_lampId);
    ble.seedPalette(_p);
    final n = (await openWisp(ble, statingStatus(wispA, _p))).$2;
    final reads = ble.paletteReads;

    ble.notifyStatus(helloOnlyStatus(wispA));
    await pumpEventQueue();

    expect(n.paletteLoading, isTrue);
    expect(ble.paletteReads, reads);
  });

  // The save's confirming status arrives, then the lamp loses that status
  // and serves the wisp's HELLO only, before the write's ack.
  test('a save confirmed after a HELLO-only status leaves the palette '
      'unknown', () async {
    WispNotifier.debugSeqStart = () => 77;
    addTearDown(() => WispNotifier.debugSeqStart = _defaultSeqStart);
    final ble = PaletteBle(_lampId);
    ble.seedPalette(_p);
    final n = (await openWisp(ble, statingStatus(wispA, _p))).$2;
    expect(n.paletteLoading, isFalse);

    ble.holdPaletteWrite = Completer<void>();
    n.setManualPaletteDraft(_q);
    final result = n.setManualPalette().then<Object?>(
      (_) => null,
      onError: (Object e) => e,
    );
    await pumpEventQueue();
    ble.notifyStatus(statingStatus(wispA, _q, seq: 77));
    await pumpEventQueue();
    ble.notifyStatus(helloOnlyStatus(wispA));
    await pumpEventQueue();
    ble.holdPaletteWrite!.complete();

    expect(await result, isNull);
    expect(n.paletteLoading, isTrue);
  });

  // The wisp's first full status lands while the open's read, started for
  // the HELLO-only status, is still in flight.
  test('a read started for a HELLO-only status never counts', () async {
    final ble = PaletteBle(_lampId)..holdPalette = Completer<void>();
    // The cache already holds what the wisp then states, so only the read's
    // own status can rule it out.
    ble.seedPalette(_p);
    await ble.connect(_lampId);
    ble.seedStatus(helloOnlyStatus(wispA));
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    final built = c.read(wispNotifierProvider(_lampId).future);
    for (var i = 0; i < 50 && ble.paletteReads == 0; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 10));
    }
    expect(ble.paletteReads, 1, reason: 'the open read never started');

    ble.notifyStatus(statingStatus(wispA, _p));
    await pumpEventQueue();
    ble.holdPalette!.complete();
    ble.holdPalette = null;
    await built;
    await pumpEventQueue();

    final n = c.read(wispNotifierProvider(_lampId).notifier);
    expect(n.paletteLoading, isTrue);
  });

  // The lamp switches display wisp while the palette sheet is open.
  test('a save after a HELLO-only status sends nothing', () async {
    final ble = PaletteBle(_lampId);
    ble.seedPalette(_p);
    final n = (await openWisp(ble, statingStatus(wispA, _p))).$2;
    expect(n.paletteLoading, isFalse);
    n.setManualPaletteDraft(_q);

    ble.notifyStatus(helloOnlyStatus(wispB));
    await pumpEventQueue();
    final result = await n.setManualPalette().then<Object?>(
      (_) => null,
      onError: (Object e) => e,
    );

    expect(result, isNotNull);
    expect(n.manualPaletteWriteFailed, isTrue);
    expect(
      ble.writesTo(_lampId, BleUuids.wispOp).where(
          (w) => utf8.decode(w, allowMalformed: true).contains('setManualPalette')),
      isEmpty,
    );
  });
}
