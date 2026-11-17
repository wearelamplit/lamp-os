import 'dart:async';

import 'package:fake_async/fake_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/wisp_palette_ble.dart';

const _lampId = 'nudge-lamp';

const _p = [
  LampColor(r: 200, g: 0, b: 30, w: 0),
  LampColor(r: 0, g: 60, b: 90, w: 15),
];
const _q = [LampColor(r: 5, g: 6, b: 7, w: 0)];

final _defaultSeqStart = WispNotifier.debugSeqStart;

/// The wisp screen reads state the notifier keeps beside its status (palette
/// known, write failed, claims, live paint, recalculating), so every change
/// to it reaches the screen even when the status it re-emits is unchanged.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUp(() => SharedPreferences.setMockInitialValues({}));

  var emits = 0;
  void onEmit() => emits++;
  setUp(() => emits = 0);

  test('a palette read landing reaches the screen', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, legacyStatus(wispA, _p), onEmit: onEmit);

    ble
      ..seedPalette(_q)
      ..holdPalette = Completer<void>()
      ..notifyStatus(legacyStatus(wispA, _q));
    await pumpEventQueue();
    final before = emits;
    ble.holdPalette!.complete();
    await pumpEventQueue();

    expect(n.savedManualPalette, _q);
    expect(emits, greaterThan(before));
  });

  test('a claims read landing reaches the screen', () async {
    final ble = PaletteBle(_lampId)
      ..seedPalette(_p)
      ..holdClaims = Completer<void>();
    await openWisp(ble, legacyStatus(wispA, _p), onEmit: onEmit);

    final before = emits;
    ble.holdClaims!.complete();
    await pumpEventQueue();

    expect(emits, greaterThan(before));
  });

  test(
    'a reconnect forgetting the stated palette reaches the screen',
    () async {
      final ble = PaletteBle(_lampId)..seedPalette(_p);
      final (_, n) = await openWisp(
        ble,
        statingStatus(wispA, _p),
        onEmit: onEmit,
      );
      expect(n.paletteLoading, isFalse);

      final before = emits;
      await ble.disconnect(_lampId);
      await ble.connect(_lampId);
      await pumpEventQueue();

      expect(n.paletteLoading, isTrue);
      expect(emits, greaterThan(before));
    },
  );

  test('a shuffle tap reaches the screen', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, legacyStatus(wispA, _p), onEmit: onEmit);

    final before = emits;
    await n.shuffle();

    expect(n.colorsRecalculating, isTrue);
    expect(emits, greaterThan(before));
  });

  test('a shuffle hint timing out reaches the screen', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, legacyStatus(wispA, _p), onEmit: onEmit);

    fakeAsync((fa) {
      unawaited(n.shuffle());
      fa.flushMicrotasks();
      final before = emits;
      fa.elapse(const Duration(seconds: 9));
      fa.flushMicrotasks();

      expect(n.colorsRecalculating, isFalse);
      expect(emits, greaterThan(before));
    });
  });

  test('a refused save reaches the screen', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, legacyStatus(wispA, _p), onEmit: onEmit);
    n.setManualPaletteDraft(_q);
    ble.notifyStatus(helloOnlyStatus(wispB));
    await pumpEventQueue();

    final before = emits;
    await n.setManualPalette().then((_) {}, onError: (_) {});

    expect(n.manualPaletteWriteFailed, isTrue);
    expect(emits, greaterThan(before));
  });

  test('a confirmed save reaches the screen', () async {
    WispNotifier.debugSeqStart = () => 77;
    addTearDown(() => WispNotifier.debugSeqStart = _defaultSeqStart);
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(
      ble,
      statingStatus(wispA, _p),
      onEmit: onEmit,
    );

    ble.holdPaletteWrite = Completer<void>();
    n.setManualPaletteDraft(_q);
    final saved = n.setManualPalette();
    await pumpEventQueue();
    ble.notifyStatus(statingStatus(wispA, _q, seq: 77));
    await pumpEventQueue();
    expect(n.paletteLoading, isTrue, reason: 'the echo states a new id');

    final before = emits;
    ble.holdPaletteWrite!.complete();
    await saved;
    await pumpEventQueue();

    expect(n.paletteLoading, isFalse);
    expect(emits, greaterThan(before));
  });

  // The user picks Osc, and inside the source-write window the lamp adopts
  // wisp B and serves B's HELLO-only merge, which the guard drops as a stale
  // echo.
  test(
    'a status the source guard drops still shows the palette unknown',
    () async {
      final ble = PaletteBle(_lampId)..seedPalette(_p);
      final (_, n) = await openWisp(
        ble,
        legacyStatus(wispA, _p),
        onEmit: onEmit,
      );
      expect(n.paletteLoading, isFalse);
      unawaited(n.setSource(WispSourceMode.osc).then((_) {}, onError: (_) {}));
      await pumpEventQueue();

      final before = emits;
      ble.notifyStatus(helloOnlyStatus(wispB));
      await pumpEventQueue();

      expect(n.paletteLoading, isTrue);
      expect(emits, greaterThan(before));
    },
  );
}
