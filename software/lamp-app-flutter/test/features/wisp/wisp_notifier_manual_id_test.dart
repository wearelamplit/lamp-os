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
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';

const _lampId = 'manual-id-lamp';
const _a = 'AA:BB:CC:DD:EE:FF';
const _bMac = '11:22:33:44:55:66';

const _x = [
  LampColor(r: 200, g: 0, b: 30, w: 0),
  LampColor(r: 0, g: 60, b: 90, w: 15),
];
const _y = [LampColor(r: 5, g: 6, b: 7, w: 0)];
const _z = [
  LampColor(r: 1, g: 1, b: 1, w: 1),
  LampColor(r: 2, g: 2, b: 2, w: 0),
  LampColor(r: 3, g: 3, b: 3, w: 3),
];

String _id(List<LampColor> p) => WispRepository.statusManualId(p);

/// A status as a wisp that reports `manualId` sends it.
String _status({
  String mac = _a,
  String source = 'manual',
  required String manualId,
  String prefix = '',
}) => jsonEncode({
  'wispMac': mac,
  'source': source,
  'manualId': manualId,
  if (prefix.isNotEmpty) 'paletteIdPrefix': prefix,
});

/// The lamp's merge when it holds a HELLO from [mac] and no status.
String _helloOnly(String mac) =>
    '{"wispMac":"$mac","wispVersion":66048,"helloFlags":0}';

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));
Uint8List _bytes(List<LampColor> p) => Uint8List.fromList([
  for (final c in p) ...[c.r, c.g, c.b, c.w],
]);

/// Counts `wisppalette` reads, holds them while [holdPalette] is set (the
/// value served is the one seeded when the hold releases), and fails them
/// while [failPalette] is set. Holds the claims read while [holdClaims] is
/// set.
class _PaletteBle extends InMemoryBleClient {
  int paletteReads = 0;
  Completer<void>? holdPalette;
  Completer<void>? holdClaims;
  bool failPalette = false;

  @override
  Future<Uint8List> read(String d, String s, String c) async {
    if (c == BleUuids.wispClaims) await holdClaims?.future;
    return super.read(d, s, c);
  }

  @override
  Future<Uint8List> readSection(String deviceId, String name) async {
    if (name == 'wisppalette') {
      paletteReads++;
      await holdPalette?.future;
      if (failPalette) throw Exception('palette read failed');
    }
    return super.readSection(deviceId, name);
  }
}

/// The Manual palette editor for a wisp whose status says which palette it
/// stores (`manualId`): known only when a read returns that palette.
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

  /// Build the notifier against [ble]. With [initial] the lamp serves that
  /// status; without, build's status read finds none and reads no palette.
  Future<WispNotifier> open(_PaletteBle ble, {String? initial}) async {
    await ble.connect(_lampId);
    if (initial != null) ble.seedSection(_lampId, 'wispstatus', _b(initial));
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    return c.read(wispNotifierProvider(_lampId).notifier);
  }

  test('explicit empty: a palette emptied elsewhere shows empty', () async {
    final ble = _PaletteBle();
    seedPalette(ble, _x);
    final n = await open(
      ble,
      initial: _status(manualId: _id(_x), prefix: _id(_x)),
    );
    expect(n.savedManualPalette, _x);

    seedPalette(ble, const []);
    notify(ble, _status(manualId: ''));
    await pumpEventQueue();

    expect(n.savedManualPalette, isEmpty);
    expect(n.draftManualPalette, isEmpty);
    expect(n.paletteLoading, isFalse);
  });

  test('late empty: an empty id the lamp\'s palette contradicts never marks '
      'the palette empty', () async {
    final ble = _PaletteBle();
    seedPalette(ble, _x);
    final n = await open(
      ble,
      initial: _status(manualId: _id(_x), prefix: _id(_x)),
    );

    // An older status reaches the app after a newer one; the lamp still
    // serves the newer palette.
    notify(ble, _status(manualId: ''));
    await pumpEventQueue();
    expect(n.savedManualPalette, _x);
    expect(n.paletteLoading, isTrue);

    notify(ble, _status(manualId: _id(_x), prefix: _id(_x)));
    await pumpEventQueue();
    expect(n.savedManualPalette, _x);
    expect(n.paletteLoading, isFalse);
  });

  test(
    'failed read: a palette read that fails never counts as empty',
    () async {
      final ble = _PaletteBle()..failPalette = true;
      final n = await open(ble, initial: _status(manualId: ''));
      expect(ble.paletteReads, 1);
      expect(n.paletteLoading, isTrue);

      ble.failPalette = false;
      notify(ble, _status(manualId: ''));
      await pumpEventQueue();
      expect(n.paletteLoading, isFalse);
    },
  );

  test('N1: an empty read in flight when Manual is tapped leaves the palette '
      'loading until a read returns it', () async {
    final ble = _PaletteBle();
    final n = await open(ble);
    expect(ble.paletteReads, 0);

    // The lamp's palette cache is not filled yet when the Osc read lands.
    ble.holdPalette = Completer<void>();
    notify(ble, _status(source: 'osc', manualId: _id(_x), prefix: 'deadbeef'));
    await pumpEventQueue();
    expect(ble.paletteReads, 1);
    final source = n.setSource(WispSourceMode.manual).catchError((Object _) {});
    await pumpEventQueue();
    ble.holdPalette!.complete();
    ble.holdPalette = null;
    await pumpEventQueue();
    expect(
      n.paletteLoading,
      isTrue,
      reason: 'an empty editor would let Save overwrite the palette',
    );

    seedPalette(ble, _x);
    notify(ble, _status(manualId: _id(_x), prefix: _id(_x)));
    await source;
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse);
    expect(n.savedManualPalette, _x);
  });

  test('stale read: a read of another palette is not the wisp\'s', () async {
    final ble = _PaletteBle();
    seedPalette(ble, _y);
    final n = await open(
      ble,
      initial: _status(manualId: _id(_x), prefix: _id(_x)),
    );
    expect(n.paletteLoading, isTrue);
    expect(n.savedManualPalette, isNot(_y));

    seedPalette(ble, _x);
    notify(ble, _status(manualId: _id(_x), prefix: _id(_x)));
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse);
    expect(n.savedManualPalette, _x);
  });

  test(
    'judged at completion: a read is held to the id heard when it lands',
    () async {
      final ble = _PaletteBle();
      final n = await open(ble);
      seedPalette(ble, _x);
      ble.holdPalette = Completer<void>();
      notify(ble, _status(manualId: _id(_x), prefix: _id(_x)));
      await pumpEventQueue();
      expect(ble.paletteReads, 1);

      // The wisp moves to Z while the read of X is held.
      notify(ble, _status(manualId: _id(_z), prefix: _id(_z)));
      await pumpEventQueue();
      ble.holdPalette!.complete();
      ble.holdPalette = null;
      await pumpEventQueue();

      expect(n.paletteLoading, isTrue);
      expect(n.savedManualPalette, isNot(_x));
    },
  );

  test('no extra reads: a known palette is not re-read when the wisp moves '
      'to Osc or Off, whatever the Osc palette id', () async {
    final ble = _PaletteBle();
    seedPalette(ble, _x);
    final n = await open(
      ble,
      initial: _status(manualId: _id(_x), prefix: _id(_x)),
    );
    expect(n.paletteLoading, isFalse);
    expect(ble.paletteReads, 1);

    notify(ble, _status(source: 'osc', manualId: _id(_x), prefix: 'deadbeef'));
    await pumpEventQueue();
    notify(ble, _status(source: 'off', manualId: _id(_x)));
    await pumpEventQueue();

    expect(ble.paletteReads, 1);
    expect(n.paletteLoading, isFalse);
    expect(n.savedManualPalette, _x);
  });

  test(
    'hello-only: a status without manualId from that wisp decides nothing',
    () async {
      final ble = _PaletteBle();
      seedPalette(ble, _y);
      final n = await open(
        ble,
        initial: _status(manualId: _id(_x), prefix: _id(_x)),
      );
      expect(n.paletteLoading, isTrue);
      expect(ble.paletteReads, 1);

      // The lamp rebooted: it holds the wisp's HELLO and no status yet.
      notify(ble, _helloOnly(_a));
      await pumpEventQueue();

      expect(ble.paletteReads, 1);
      expect(n.paletteLoading, isTrue);
      expect(n.savedManualPalette, isNot(_y));
    },
  );

  test('display switch: another wisp takes the lamp, and the palette held '
      'is no longer its', () async {
    final ble = _PaletteBle();
    seedPalette(ble, _x);
    final n = await open(
      ble,
      initial: _status(manualId: _id(_x), prefix: _id(_x)),
    );
    expect(n.paletteLoading, isFalse);

    // The lamp adopts wisp B: it clears its caches and serves B's HELLO only.
    seedPalette(ble, const []);
    notify(ble, _helloOnly(_bMac));
    await pumpEventQueue();
    expect(n.paletteLoading, isTrue);

    seedPalette(ble, _y);
    notify(ble, _status(mac: _bMac, manualId: _id(_y), prefix: _id(_y)));
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse);
    expect(n.savedManualPalette, _y);
  });

  test('reconnect: the palette is unknown from a reconnect until the wisp '
      'next states it', () async {
    final ble = _PaletteBle();
    seedPalette(ble, _x);
    final n = await open(
      ble,
      initial: _status(manualId: _id(_x), prefix: _id(_x)),
    );
    expect(n.paletteLoading, isFalse);

    await ble.disconnect(_lampId);
    await pumpEventQueue();
    await ble.connect(_lampId);
    await pumpEventQueue();
    expect(
      n.paletteLoading,
      isTrue,
      reason: 'another phone may have changed the palette meanwhile',
    );

    notify(ble, _helloOnly(_a));
    await pumpEventQueue();
    expect(n.paletteLoading, isTrue);

    notify(ble, _status(manualId: _id(_x), prefix: _id(_x)));
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse);
    expect(n.savedManualPalette, _x);
  });

  test('reconnect: a read in flight across a reconnect does not count',
      () async {
    final ble = _PaletteBle()..failPalette = true;
    final n = await open(ble,
        initial: _status(manualId: _id(_x), prefix: _id(_x)));
    expect(n.paletteLoading, isTrue);

    ble.failPalette = false;
    seedPalette(ble, _x);
    ble.holdPalette = Completer<void>();
    notify(ble, _status(manualId: _id(_x), prefix: _id(_x)));
    await pumpEventQueue();
    expect(ble.paletteReads, 2);

    await ble.disconnect(_lampId);
    await pumpEventQueue();
    await ble.connect(_lampId);
    await pumpEventQueue();
    ble.holdPalette!.complete();
    ble.holdPalette = null;
    await pumpEventQueue();

    expect(n.paletteLoading, isTrue);
    expect(n.savedManualPalette, isEmpty);
  });

  test('a new palette stated while another read is busy is not the one held',
      () async {
    final ble = _PaletteBle()..holdClaims = Completer<void>();
    seedPalette(ble, _x);
    final n = await open(ble,
        initial: _status(manualId: _id(_x), prefix: _id(_x)));
    expect(n.paletteLoading, isFalse);

    // Another phone saved Y; its status lands while the claims read holds
    // the link, so no palette read can start.
    seedPalette(ble, _y);
    notify(ble, _status(manualId: _id(_y), prefix: _id(_y)));
    await pumpEventQueue();
    expect(ble.paletteReads, 1);
    expect(n.paletteLoading, isTrue);

    ble.holdClaims!.complete();
    ble.holdClaims = null;
    await pumpEventQueue();
    notify(ble, _status(manualId: _id(_y), prefix: _id(_y)));
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse);
    expect(n.savedManualPalette, _y);
  });

  test('a new palette stated in a status the source-write guard drops is not '
      'the one held', () async {
    final ble = _PaletteBle();
    seedPalette(ble, _x);
    final n = await open(ble,
        initial: _status(manualId: _id(_x), prefix: _id(_x)));
    expect(n.paletteLoading, isFalse);

    unawaited(n.setSource(WispSourceMode.osc).catchError((Object _) {}));
    await pumpEventQueue();
    // Still Manual, so the guard drops it as a pre-write echo.
    notify(ble, _status(manualId: _id(_y), prefix: _id(_y)));
    await pumpEventQueue();
    expect(n.paletteLoading, isTrue);
  });

  test('a read that lands after the lamp switched wisps does not count',
      () async {
    final ble = _PaletteBle();
    final n = await open(ble);
    seedPalette(ble, _x);
    ble.holdPalette = Completer<void>();
    notify(ble, _status(manualId: _id(_x), prefix: _id(_x)));
    await pumpEventQueue();
    expect(ble.paletteReads, 1);

    notify(ble, _helloOnly(_bMac));
    await pumpEventQueue();
    ble.holdPalette!.complete();
    ble.holdPalette = null;
    await pumpEventQueue();

    expect(n.paletteLoading, isTrue);
    expect(n.savedManualPalette, isEmpty);
  });

  test('choosing Manual keeps a stated empty palette known', () async {
    final ble = _PaletteBle();
    final n = await open(ble, initial: _status(source: 'off', manualId: ''));
    expect(n.paletteLoading, isFalse);

    unawaited(n.setSource(WispSourceMode.manual).catchError((Object _) {}));
    expect(n.paletteLoading, isFalse,
        reason: 'the wisp stated its Manual palette empty, and a read agreed');
  });

  test('a palette known before any wisp was heard is not the first stating '
      'wisp\'s', () async {
    final ble = _PaletteBle()..holdClaims = Completer<void>();
    final n = await open(ble, initial: '{}');

    // The lamp's first wisp states Y while the claims read holds the link.
    seedPalette(ble, _y);
    notify(ble, _status(manualId: _id(_y), prefix: _id(_y)));
    await pumpEventQueue();
    expect(ble.paletteReads, 1);
    expect(n.paletteLoading, isTrue);
  }, timeout: const Timeout(Duration(seconds: 20)));
}
