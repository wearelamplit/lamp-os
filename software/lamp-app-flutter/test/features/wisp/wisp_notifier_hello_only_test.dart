import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';

const _lampId = 'hello-only-lamp';
const _a = 'AA:BB:CC:DD:EE:FF';
const _bMac = '11:22:33:44:55:66';

const _p = [
  LampColor(r: 200, g: 0, b: 30, w: 0),
  LampColor(r: 0, g: 60, b: 90, w: 15),
];
const _q = [LampColor(r: 5, g: 6, b: 7, w: 0)];

final _defaultSeqStart = WispNotifier.debugSeqStart;

/// The lamp's merge when it holds a HELLO from [mac] and no status: no
/// `source`, no settings, no `manualId`.
String _helloOnly(String mac) =>
    '{"wispMac":"$mac","wispVersion":66048,"helloFlags":0}';

/// A full status from a wisp that does not state its palette.
String _legacy(String mac, List<LampColor> p) => jsonEncode({
  'wispMac': mac,
  'source': 'manual',
  if (p.isNotEmpty) 'paletteIdPrefix': WispRepository.paletteIdHash(p),
});

/// A full status from a wisp that states its palette (`manualId`).
String _stating(String mac, List<LampColor> p) => jsonEncode({
  'wispMac': mac,
  'source': 'manual',
  'manualId': WispRepository.statusManualId(p),
  if (p.isNotEmpty) 'paletteIdPrefix': WispRepository.paletteIdHash(p),
});

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));
Uint8List _bytes(List<LampColor> p) => Uint8List.fromList([
  for (final c in p) ...[c.r, c.g, c.b, c.w],
]);

class _PaletteBle extends InMemoryBleClient {
  int paletteReads = 0;
  Completer<void>? holdPalette;
  Completer<void>? holdPaletteWrite;

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
    if (utf8.decode(v, allowMalformed: true).contains('setManualPalette')) {
      await holdPaletteWrite?.future;
    }
  }

  @override
  Future<Uint8List> readSection(String deviceId, String name) async {
    if (name == 'wisppalette') {
      paletteReads++;
      await holdPalette?.future;
    }
    return super.readSection(deviceId, name);
  }
}

/// Until the app holds a full status from the wisp it shows, the lamp's
/// palette cache may be empty or another wisp's, so the Manual palette stays
/// unknown and the editor cannot save over the wisp's own.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUp(() => SharedPreferences.setMockInitialValues({}));

  void notify(InMemoryBleClient ble, String json) => ble.simulateNotify(
    _lampId,
    BleUuids.controlService,
    BleUuids.wispStatus,
    _b(json),
  );

  void seedPalette(InMemoryBleClient ble, List<LampColor> p) =>
      ble.seedSection(_lampId, 'wisppalette', _bytes(p));

  Future<WispNotifier> open(_PaletteBle ble, String initial) async {
    await ble.connect(_lampId);
    ble.seedSection(_lampId, 'wispstatus', _b(initial));
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    return c.read(wispNotifierProvider(_lampId).notifier);
  }

  // The lamp just booted: it holds the wisp's HELLO and whatever palette
  // reached it, an empty cache included.
  for (final (name, full) in [
    ('a wisp that does not state its palette', _legacy),
    ('a wisp that states its palette', _stating),
  ]) {
    for (final cached in [const <LampColor>[], _q]) {
      test('$name: a HELLO-only status at open leaves the palette unknown '
          '(${cached.length}-color lamp cache)', () async {
        final ble = _PaletteBle();
        seedPalette(ble, cached);
        final n = await open(ble, _helloOnly(_a));
        expect(n.paletteLoading, isTrue);

        seedPalette(ble, _p);
        notify(ble, full(_a, _p));
        await pumpEventQueue();
        expect(n.paletteLoading, isFalse);
        expect(n.savedManualPalette, _p);
      });
    }
  }

  // The lamp adopts wisp B and serves only B's HELLO.
  test('display switch to a HELLO-only wisp returns the palette to unknown',
      () async {
    final ble = _PaletteBle();
    seedPalette(ble, _p);
    final n = await open(ble, _legacy(_a, _p));
    expect(n.paletteLoading, isFalse);

    seedPalette(ble, const []);
    notify(ble, _helloOnly(_bMac));
    await pumpEventQueue();
    expect(n.paletteLoading, isTrue);

    seedPalette(ble, _q);
    notify(ble, _legacy(_bMac, _q));
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse);
    expect(n.savedManualPalette, _q);
  });

  // A read started for a full status lands once the lamp serves only a
  // HELLO, from that wisp (the lamp lost its status) or another one.
  for (final (kind, full) in [
    ('a wisp that does not state its palette', _legacy),
    ('a wisp that states its palette', _stating),
  ]) {
    for (final (name, mac) in [('the same wisp', _a), ('another wisp', _bMac)]) {
      test('$kind: a read landing after a HELLO-only status from $name does '
          'not count', () async {
        final ble = _PaletteBle();
        seedPalette(ble, const []);
        final n = await open(ble, full(_a, const []));
        expect(n.paletteLoading, isFalse);

        seedPalette(ble, _p);
        ble.holdPalette = Completer<void>();
        final before = ble.paletteReads;
        notify(ble, full(_a, _p));
        await pumpEventQueue();
        expect(ble.paletteReads, before + 1, reason: 'no read in flight');

        notify(ble, _helloOnly(mac));
        await pumpEventQueue();
        ble.holdPalette!.complete();
        ble.holdPalette = null;
        await pumpEventQueue();

        expect(n.paletteLoading, isTrue);
      });
    }
  }

  // The lamp relays wisp B's full status by the time a read started for A
  // lands.
  test('a read landing after a full status from another wisp does not count',
      () async {
    final ble = _PaletteBle();
    seedPalette(ble, const []);
    final n = await open(ble, _legacy(_a, const []));
    expect(n.paletteLoading, isFalse);

    seedPalette(ble, _p);
    ble.holdPalette = Completer<void>();
    notify(ble, _legacy(_a, _p));
    await pumpEventQueue();
    notify(ble, _legacy(_bMac, _q));
    await pumpEventQueue();
    ble.holdPalette!.complete();
    ble.holdPalette = null;
    await pumpEventQueue();

    expect(n.paletteLoading, isTrue);
  });

  test('a HELLO-only status starts no palette read', () async {
    final ble = _PaletteBle();
    seedPalette(ble, _p);
    final n = await open(ble, _legacy(_a, _p));
    final reads = ble.paletteReads;

    notify(ble, _helloOnly(_a));
    await pumpEventQueue();

    expect(n.paletteLoading, isTrue);
    expect(ble.paletteReads, reads);
  });

  // The save's confirming status arrives, then the lamp loses that status
  // and serves the wisp's HELLO only, before the write's ack.
  for (final (name, full, seq) in [
    ('a wisp that does not state its palette', _legacy, 0),
    ('a wisp that states its palette', _stating, 77),
  ]) {
    test('$name: a save confirmed after a HELLO-only status leaves the '
        'palette unknown', () async {
      WispNotifier.debugSeqStart = () => 77;
      addTearDown(() => WispNotifier.debugSeqStart = _defaultSeqStart);
      final ble = _PaletteBle();
      seedPalette(ble, _p);
      final n = await open(ble, full(_a, _p));
      expect(n.paletteLoading, isFalse);

      ble.holdPaletteWrite = Completer<void>();
      n.setManualPaletteDraft(_q);
      final result = n.setManualPalette().then<Object?>(
        (_) => null,
        onError: (Object e) => e,
      );
      await pumpEventQueue();
      final echo = jsonDecode(full(_a, _q)) as Map<String, dynamic>;
      if (seq != 0) echo['manualSeq'] = seq;
      notify(ble, jsonEncode(echo));
      await pumpEventQueue();
      notify(ble, _helloOnly(_a));
      await pumpEventQueue();
      ble.holdPaletteWrite!.complete();

      expect(await result, isNull);
      expect(n.paletteLoading, isTrue);
    });
  }

  // The wisp's first full status lands while the open's read, started for
  // the HELLO-only status, is still in flight.
  for (final (name, full) in [
    ('a wisp that does not state its palette', _legacy),
    ('a wisp that states its palette', _stating),
  ]) {
    test('$name: a read started for a HELLO-only status never counts',
        () async {
      final ble = _PaletteBle()..holdPalette = Completer<void>();
      // The cache already holds what the wisp then states, so only the
      // read's own status can rule it out.
      seedPalette(ble, _p);
      await ble.connect(_lampId);
      ble.seedSection(_lampId, 'wispstatus', _b(_helloOnly(_a)));
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

      notify(ble, full(_a, _p));
      await pumpEventQueue();
      ble.holdPalette!.complete();
      ble.holdPalette = null;
      await built;
      await pumpEventQueue();

      final n = c.read(wispNotifierProvider(_lampId).notifier);
      expect(n.paletteLoading, isTrue);
    });
  }

  // The lamp switches display wisp while the palette sheet is open.
  test('a save after a HELLO-only status sends nothing', () async {
    final ble = _PaletteBle();
    seedPalette(ble, _p);
    final n = await open(ble, _legacy(_a, _p));
    expect(n.paletteLoading, isFalse);
    n.setManualPaletteDraft(_q);

    notify(ble, _helloOnly(_bMac));
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
