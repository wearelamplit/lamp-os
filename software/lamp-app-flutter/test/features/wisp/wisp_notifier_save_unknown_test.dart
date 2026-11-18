import 'dart:async';

import 'package:fake_async/fake_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/wisp_palette_ble.dart';

const _lampId = 'save-unknown-lamp';

const _p = [
  LampColor(r: 200, g: 0, b: 30, w: 0),
  LampColor(r: 0, g: 60, b: 90, w: 15),
];
const _q = [LampColor(r: 5, g: 6, b: 7, w: 0)];
const _other = [LampColor(r: 90, g: 90, b: 0, w: 40)];

final _defaultSeqStart = WispNotifier.debugSeqStart;

/// A Manual palette save goes out only while the palette is known for the
/// wisp the lamp shows; otherwise the editor shows its loading line and the
/// notifier sends nothing.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUp(() => SharedPreferences.setMockInitialValues({}));

  // Saves and expects it refused: an error, the not-saved flag, no write.
  Future<void> expectRefused(PaletteBle ble, WispNotifier n) async {
    expect(n.paletteLoading, isTrue);
    final err = await n.setManualPalette().then<Object?>(
      (_) => null,
      onError: (Object e) => e,
    );
    expect(err, isNotNull);
    expect(n.manualPaletteWriteFailed, isTrue);
    expect(ble.paletteWrites, isEmpty);
  }

  // Another phone may have changed the palette while the link was down.
  test('after a reconnect the save sends nothing until the wisp\'s next '
      'status states its palette', () async {
    WispNotifier.debugSeqStart = () => 77;
    addTearDown(() => WispNotifier.debugSeqStart = _defaultSeqStart);
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, statingStatus(wispA, _p));
    expect(n.paletteLoading, isFalse);
    n.setManualPaletteDraft(_q);

    await ble.disconnect(_lampId);
    await ble.connect(_lampId);
    await pumpEventQueue();

    await expectRefused(ble, n);

    ble
      ..seedPalette(_other)
      ..notifyStatus(statingStatus(wispA, _other));
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse);
    expect(n.savedManualPalette, _other);

    final result = n.setManualPalette().then<Object?>(
      (_) => null,
      onError: (Object e) => e,
    );
    await pumpEventQueue();
    expect(ble.paletteWrites, hasLength(1));
    ble
      ..seedPalette(_q)
      ..notifyStatus(statingStatus(wispA, _q, seq: 77));
    expect(await result, isNull);
  });

  test('a status without manualId before a reconnect lets no save out after '
      'it', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, unstatedStatus(wispA, _p));
    n.setManualPaletteDraft(_q);

    await ble.disconnect(_lampId);
    await ble.connect(_lampId);
    await pumpEventQueue();

    await expectRefused(ble, n);
  });

  // The palette is never guessed from a status that does not state it,
  // whatever the source mode or the read finds.
  for (final source in ['manual', 'off', 'osc']) {
    test('a status in $source without manualId leaves the palette unknown and '
        'the save sends nothing', () async {
      final ble = PaletteBle(_lampId)..seedPalette(_p);
      final status =
          '{"wispMac":"$wispA","configSplit":true,"source":"$source",'
          '"configSplit":true}';
      final (_, n) = await openWisp(ble, status);
      ble.notifyStatus(status);
      await pumpEventQueue();

      expect(ble.paletteReads, greaterThan(0));
      expect(n.savedManualPalette, isEmpty);
      n.setManualPaletteDraft(_q);
      await expectRefused(ble, n);
    });
  }

  // The wisp at that mac now runs firmware that predates `manualId`.
  test('a status without manualId from a wisp whose palette is known returns '
      'it to unknown', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, statingStatus(wispA, _p));
    expect(n.paletteLoading, isFalse);
    n.setManualPaletteDraft(_q);

    ble.notifyStatus(unstatedStatus(wispA, _p));
    await pumpEventQueue();

    await expectRefused(ble, n);
  });

  test('with no wisp shown the save sends nothing', () async {
    final ble = PaletteBle(_lampId);
    final (_, n) = await openWisp(ble, '{}');
    expect(n.paletteLoading, isFalse);
    n.setManualPaletteDraft(_q);

    final err = await n.setManualPalette().then<Object?>(
      (_) => null,
      onError: (Object e) => e,
    );
    expect(err, isNotNull);
    expect(n.manualPaletteWriteFailed, isTrue);
    expect(ble.paletteWrites, isEmpty);
  });

  test('before the first full status\'s read lands the save sends '
      'nothing', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, helloOnlyStatus(wispA));
    n.setManualPaletteDraft(_q);

    ble
      ..holdPalette = Completer<void>()
      ..notifyStatus(statingStatus(wispA, _p));
    await pumpEventQueue();

    await expectRefused(ble, n);
    ble.holdPalette!.complete();
  });

  // The lamp relays B's full status with no HELLO-only merge between.
  test('after a display switch the save sends nothing until the new '
      'wisp\'s palette is read', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, statingStatus(wispA, _p));
    expect(n.paletteLoading, isFalse);
    n.setManualPaletteDraft(_q);

    ble
      ..seedPalette(_other)
      ..holdPalette = Completer<void>()
      ..notifyStatus(statingStatus(wispB, _other));
    await pumpEventQueue();

    await expectRefused(ble, n);

    ble.holdPalette!.complete();
    ble.holdPalette = null;
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse);
    expect(n.savedManualPalette, _other);
  });

  test('a save in flight stops resending once the palette goes '
      'unknown', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, statingStatus(wispA, _p));
    n.setManualPaletteDraft(_q);

    fakeAsync((fa) {
      Object? err;
      n.setManualPalette().catchError((Object e) => err = e);
      fa.flushMicrotasks();
      expect(ble.paletteWrites, hasLength(1));

      ble.notifyStatus(helloOnlyStatus(wispB));
      fa.flushMicrotasks();
      fa.elapse(WispNotifier.writeConfirmBudget);
      fa.flushMicrotasks();

      expect(err, isNotNull);
    });
    expect(ble.paletteWrites, hasLength(1));
    expect(n.manualPaletteWriteFailed, isTrue);
  });

  // The lamp moves to wisp B while the first copy is out, B's palette read
  // lands, and the copy fails, so the next attempt would go to B.
  test('a save sends no further copy after the palette went unknown, '
      'even once it is known again', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, statingStatus(wispA, _p));
    n.setManualPaletteDraft(_q);

    ble.holdPaletteWrite = Completer<void>();
    final result = n.setManualPalette().then<Object?>(
      (_) => null,
      onError: (Object e) => e,
    );
    await pumpEventQueue();
    expect(ble.paletteWrites, hasLength(1));
    ble
      ..seedPalette(_other)
      ..notifyStatus(statingStatus(wispB, _other));
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse, reason: "B's palette was read");
    ble.holdPaletteWrite!.completeError(Exception('write lost'));
    ble.holdPaletteWrite = null;

    expect(await result, isNotNull);
    expect(ble.paletteWrites, hasLength(1));
    expect(n.manualPaletteWriteFailed, isTrue);
  });

  // The op reaches the wisp and its echo lands, but the write call reports an
  // error (a lost response).
  test('a save the wisp echoed confirms even when its write reports '
      'an error', () async {
    WispNotifier.debugSeqStart = () => 77;
    addTearDown(() => WispNotifier.debugSeqStart = _defaultSeqStart);
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, statingStatus(wispA, _p));
    n.setManualPaletteDraft(_q);

    ble.holdPaletteWrite = Completer<void>();
    final result = n.setManualPalette().then<Object?>(
      (_) => null,
      onError: (Object e) => e,
    );
    await pumpEventQueue();
    ble
      ..seedPalette(_q)
      ..notifyStatus(statingStatus(wispA, _q, seq: 77));
    await pumpEventQueue();
    ble.holdPaletteWrite!.completeError(Exception('response lost'));
    ble.holdPaletteWrite = null;

    expect(await result, isNull);
    expect(n.manualPaletteWriteFailed, isFalse);
    expect(ble.paletteWrites, hasLength(1));
  });

  // The write's ack takes longer than the confirm window, and the wisp's echo
  // lands in between. A save of the palette already held states no new id.
  for (final (label, draft) in [
    ('a new palette', _q),
    ('the palette already held', _p),
  ]) {
    test('a save of $label whose echo lands after its confirm window '
        'confirms with no second copy', () async {
      WispNotifier.debugSeqStart = () => 77;
      addTearDown(() => WispNotifier.debugSeqStart = _defaultSeqStart);
      final ble = PaletteBle(_lampId)..seedPalette(_p);
      final (_, n) = await openWisp(ble, statingStatus(wispA, _p));
      n.setManualPaletteDraft(draft);

      ble.holdPaletteWrite = Completer<void>();
      final result = n.setManualPalette().then<Object?>(
        (_) => null,
        onError: (Object e) => e,
      );
      await pumpEventQueue();
      expect(ble.paletteWrites, hasLength(1));
      await Future<void>.delayed(
        WispNotifier.writeConfirmWindow + const Duration(milliseconds: 200),
      );
      ble.notifyStatus(statingStatus(wispA, draft, seq: 77));
      await pumpEventQueue();
      ble.holdPaletteWrite!.complete();
      ble.holdPaletteWrite = null;

      expect(await result, isNull);
      expect(n.manualPaletteWriteFailed, isFalse);
      expect(ble.paletteWrites, hasLength(1));
    });
  }

  // Another phone's palette reaches the wisp while this save's first write is
  // out, and the read it starts finds the lamp's palette cache not yet filled.
  test('a read landing during a save that leaves the palette unknown stops '
      'the resends', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, statingStatus(wispA, _p));
    n.setManualPaletteDraft(_q);

    ble.holdPaletteWrite = Completer<void>();
    final result = n.setManualPalette().then<Object?>(
      (_) => null,
      onError: (Object e) => e,
    );
    await pumpEventQueue();
    expect(ble.paletteWrites, hasLength(1));
    ble
      ..seedPalette(const [])
      ..notifyStatus(statingStatus(wispA, _other));
    await pumpEventQueue();
    expect(n.paletteLoading, isTrue, reason: 'the read found nothing');
    ble.holdPaletteWrite!.complete();
    ble.holdPaletteWrite = null;

    expect(await result, isNotNull);
    expect(ble.paletteWrites, hasLength(1));
    expect(n.manualPaletteWriteFailed, isTrue);
  });

  test('a read landing during a save that finds the palette lets the save '
      'confirm', () async {
    WispNotifier.debugSeqStart = () => 77;
    addTearDown(() => WispNotifier.debugSeqStart = _defaultSeqStart);
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, statingStatus(wispA, _p));
    n.setManualPaletteDraft(_q);

    ble.holdPaletteWrite = Completer<void>();
    final result = n.setManualPalette().then<Object?>(
      (_) => null,
      onError: (Object e) => e,
    );
    await pumpEventQueue();
    ble
      ..seedPalette(_other)
      ..notifyStatus(statingStatus(wispA, _other));
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse);
    ble.notifyStatus(statingStatus(wispA, _q, seq: 77));
    await pumpEventQueue();
    ble.holdPaletteWrite!.complete();

    expect(await result, isNull);
    expect(n.manualPaletteWriteFailed, isFalse);
    expect(ble.paletteWrites, hasLength(1));
  });
}
