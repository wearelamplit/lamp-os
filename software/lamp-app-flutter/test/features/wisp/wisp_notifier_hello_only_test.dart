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
  for (final (name, mac) in [('the same wisp', _a), ('another wisp', _bMac)]) {
    test('a read landing after a HELLO-only status from $name does not count',
        () async {
      final ble = _PaletteBle();
      seedPalette(ble, const []);
      final n = await open(ble, _legacy(_a, const []));
      expect(n.paletteLoading, isFalse);

      seedPalette(ble, _p);
      ble.holdPalette = Completer<void>();
      notify(ble, _legacy(_a, _p));
      await pumpEventQueue();

      notify(ble, _helloOnly(mac));
      await pumpEventQueue();
      ble.holdPalette!.complete();
      ble.holdPalette = null;
      await pumpEventQueue();

      expect(n.paletteLoading, isTrue);
    });
  }
}
