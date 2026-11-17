import 'dart:async';

import 'package:fake_async/fake_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/wisp_palette_ble.dart';

const _lampId = 'save-unknown-lamp';

const _p = [
  LampColor(r: 200, g: 0, b: 30, w: 0),
  LampColor(r: 0, g: 60, b: 90, w: 15),
];
const _q = [LampColor(r: 5, g: 6, b: 7, w: 0)];
const _other = [LampColor(r: 90, g: 90, b: 0, w: 40)];

typedef _Full = String Function(String mac, List<LampColor> p, {int? seq});

String _legacy(String mac, List<LampColor> p, {int? seq}) =>
    legacyStatus(mac, p);

const List<(String, _Full)> _kinds = [
  ('a wisp that does not state its palette', _legacy),
  ('a wisp that states its palette', statingStatus),
];

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

  test('after a reconnect the save sends nothing', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, statingStatus(wispA, _p));
    expect(n.paletteLoading, isFalse);
    n.setManualPaletteDraft(_q);

    await ble.disconnect(_lampId);
    await ble.connect(_lampId);
    await pumpEventQueue();

    await expectRefused(ble, n);
  });

  for (final (kind, full) in _kinds) {
    test('$kind: before the first full status\'s read lands the save sends '
        'nothing', () async {
      final ble = PaletteBle(_lampId)..seedPalette(_p);
      final (_, n) = await openWisp(ble, helloOnlyStatus(wispA));
      n.setManualPaletteDraft(_q);

      ble
        ..holdPalette = Completer<void>()
        ..notifyStatus(full(wispA, _p));
      await pumpEventQueue();

      await expectRefused(ble, n);
      ble.holdPalette!.complete();
    });

    // The lamp relays B's full status with no HELLO-only merge between.
    test('$kind: after a display switch the save sends nothing until the new '
        'wisp\'s palette is read', () async {
      final ble = PaletteBle(_lampId)..seedPalette(_p);
      final (_, n) = await openWisp(ble, full(wispA, _p));
      expect(n.paletteLoading, isFalse);
      n.setManualPaletteDraft(_q);

      ble
        ..seedPalette(_other)
        ..holdPalette = Completer<void>()
        ..notifyStatus(full(wispB, _other));
      await pumpEventQueue();

      await expectRefused(ble, n);

      ble.holdPalette!.complete();
      ble.holdPalette = null;
      await pumpEventQueue();
      expect(n.paletteLoading, isFalse);
      expect(n.savedManualPalette, _other);
    });
  }

  test('right after choosing Manual with no palette held the save sends '
      'nothing', () async {
    final ble = PaletteBle(_lampId);
    final (_, n) = await openWisp(ble, '{"wispMac":"$wispA","source":"off"}');
    expect(n.paletteLoading, isFalse);
    unawaited(n.setSource(WispSourceMode.manual).then((_) {}, onError: (_) {}));
    await pumpEventQueue();
    n.setManualPaletteDraft(_q);

    await expectRefused(ble, n);
  });

  // An empty save waits out the read in flight before it sends, and that
  // read lands meanwhile. The tap decides.
  test('a save tapped while the palette is unread sends nothing once the '
      'read lands', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, helloOnlyStatus(wispA));
    n.setManualPaletteDraft(const []);
    ble
      ..holdPalette = Completer<void>()
      ..seedStatus(legacyStatus(wispA, _p))
      ..notifyStatus(legacyStatus(wispA, _p));
    await pumpEventQueue();
    expect(n.paletteLoading, isTrue);

    final result = n.setManualPalette().then<Object?>(
      (_) => null,
      onError: (Object e) => e,
    );
    await pumpEventQueue();
    ble.holdPalette!.complete();
    ble.holdPalette = null;
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse);

    expect(await result, isNotNull);
    expect(ble.paletteWrites, isEmpty);
  });

  // An empty save first re-reads the status; the lamp adopts wisp B and
  // serves its HELLO-only merge meanwhile.
  test('a save tapped as the palette goes unknown sends nothing', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, legacyStatus(wispA, _p));
    n.setManualPaletteDraft(const []);

    ble.holdStatusRead = Completer<void>();
    final result = n.setManualPalette().then<Object?>(
      (_) => null,
      onError: (Object e) => e,
    );
    await pumpEventQueue();
    ble
      ..seedStatus(helloOnlyStatus(wispB))
      ..notifyStatus(helloOnlyStatus(wispB));
    await pumpEventQueue();
    ble.holdStatusRead!.complete();

    expect(await result, isNotNull);
    expect(n.manualPaletteWriteFailed, isTrue);
    expect(ble.paletteWrites, isEmpty);
  });

  for (final (kind, full) in _kinds) {
    test('$kind: a save in flight stops resending once the palette goes '
        'unknown', () async {
      final ble = PaletteBle(_lampId)..seedPalette(_p);
      final (_, n) = await openWisp(ble, full(wispA, _p));
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
  }

  // The lamp moves to wisp B while the first copy is out, B's palette read
  // lands, and the copy fails, so the next attempt would go to B.
  for (final (kind, full) in _kinds) {
    test('$kind: a save sends no further copy after the palette went unknown, '
        'even once it is known again', () async {
      final ble = PaletteBle(_lampId)..seedPalette(_p);
      final (_, n) = await openWisp(ble, full(wispA, _p));
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
        ..notifyStatus(full(wispB, _other));
      await pumpEventQueue();
      expect(n.paletteLoading, isFalse, reason: "B's palette was read");
      ble.holdPaletteWrite!.completeError(Exception('write lost'));
      ble.holdPaletteWrite = null;

      expect(await result, isNotNull);
      expect(ble.paletteWrites, hasLength(1));
      expect(n.manualPaletteWriteFailed, isTrue);
    });
  }

  // An empty save re-reads the status before its first send; the lamp adopts
  // wisp B meanwhile and B's palette is read before the send would go out.
  test('an empty save sends nothing when the palette went unknown during its '
      'status read', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, legacyStatus(wispA, _p));
    n.setManualPaletteDraft(const []);

    ble.holdStatusRead = Completer<void>();
    final result = n.setManualPalette().then<Object?>(
      (_) => null,
      onError: (Object e) => e,
    );
    await pumpEventQueue();
    ble
      ..seedPalette(_other)
      ..seedStatus(legacyStatus(wispB, _other))
      ..notifyStatus(legacyStatus(wispB, _other));
    await pumpEventQueue();
    ble.holdStatusRead!.complete();
    ble.holdStatusRead = null;
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse, reason: "B's palette was read");

    expect(await result, isNotNull);
    expect(ble.paletteWrites, isEmpty);
  });

  // Another phone's palette reaches the wisp while this save's first write is
  // out, and the read it starts finds the lamp's palette cache not yet filled.
  test('a read landing during a save that leaves the palette unknown stops '
      'the resends', () async {
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, legacyStatus(wispA, _p));
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
      ..notifyStatus(legacyStatus(wispA, _other));
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
    final ble = PaletteBle(_lampId)..seedPalette(_p);
    final (_, n) = await openWisp(ble, legacyStatus(wispA, _p));
    n.setManualPaletteDraft(_q);

    ble.holdPaletteWrite = Completer<void>();
    final result = n.setManualPalette().then<Object?>(
      (_) => null,
      onError: (Object e) => e,
    );
    await pumpEventQueue();
    ble
      ..seedPalette(_other)
      ..notifyStatus(legacyStatus(wispA, _other));
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse);
    ble.notifyStatus(legacyStatus(wispA, _q));
    await pumpEventQueue();
    ble.holdPaletteWrite!.complete();

    expect(await result, isNull);
    expect(n.manualPaletteWriteFailed, isFalse);
    expect(ble.paletteWrites, hasLength(1));
  });
}
