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

const _lampId = 'loading-lamp';
const _emptyManual = '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual"}';

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

/// Holds the `wispstatus` section read while [holdStatus] is set and the
/// CHAR_WISP_CLAIMS read while [holdClaims] is set, and counts `wisppalette`
/// reads.
class _HeldReadsBle extends InMemoryBleClient {
  Completer<void>? holdStatus;
  Completer<void>? holdClaims;
  int paletteReads = 0;

  @override
  Future<Uint8List> readSection(String deviceId, String name) async {
    if (name == 'wisppalette') paletteReads++;
    if (name == 'wispstatus') await holdStatus?.future;
    return super.readSection(deviceId, name);
  }

  @override
  Future<Uint8List> read(String d, String s, String c) async {
    if (c == BleUuids.wispClaims) await holdClaims?.future;
    return super.read(d, s, c);
  }
}

/// When the manual-palette editor leaves "Reading palette from wisp…" for a
/// wisp whose Manual palette is empty, whose status never changes the palette
/// id.
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

  void notify(InMemoryBleClient ble, String json) => ble.simulateNotify(
      _lampId, BleUuids.controlService, BleUuids.wispStatus, _b(json));

  test('a notify during build reads the palette build may skip as busy',
      () async {
    final ble = _HeldReadsBle();
    await ble.connect(_lampId);
    ble.seedSection(_lampId, 'wispstatus', _b(_emptyManual));
    ble.holdStatus = Completer<void>();
    ble.holdClaims = Completer<void>();
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    final built = c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();

    // A notify lands during build's status read. Unless it reads the palette
    // itself, the claims read it starts makes build skip the palette read.
    notify(ble, _emptyManual);
    await pumpEventQueue();
    ble.holdStatus!.complete();
    await pumpEventQueue();
    await Future<void>.delayed(const Duration(milliseconds: 100));
    ble.holdClaims!.complete();
    await built;
    await pumpEventQueue();

    final n = c.read(wispNotifierProvider(_lampId).notifier);
    expect(ble.paletteReads, greaterThan(0));
    expect(n.paletteLoading, isFalse);
  });

  test('a palette skipped behind a status re-read is read when that re-read '
      'lands with the same palette id', () async {
    const palette = [LampColor(r: 9, g: 80, b: 200, w: 0)];
    final manual = '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual",'
        '"paletteIdPrefix":"${WispRepository.paletteIdHash(palette)}"}';
    final ble = _HeldReadsBle();
    await ble.connect(_lampId);
    ble.seedSection(_lampId, 'wispstatus', _b(manual));
    ble.seedSection(_lampId, 'wisppalette',
        Uint8List.fromList([for (final x in palette) ...[x.r, x.g, x.b, x.w]]));
    ble.holdStatus = Completer<void>();
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    final built = c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();

    // An empty notify from a lamp that serves the status whole starts a
    // status re-read, held with build's own status read.
    notify(ble, '{"configLastSeenMs":5}');
    await pumpEventQueue();
    ble.holdStatus!.complete();
    await built;
    await pumpEventQueue();

    final n = c.read(wispNotifierProvider(_lampId).notifier);
    expect(n.paletteLoading, isFalse);
    expect(n.savedManualPalette, palette);
  });

  test('an empty status frame starts no palette read', () async {
    final ble = _HeldReadsBle();
    await ble.connect(_lampId);
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();

    // flutter_blue_plus replays an empty value on subscribe after a
    // disconnect, before the lamp answers reads.
    ble.simulateNotify(
        _lampId, BleUuids.controlService, BleUuids.wispStatus, Uint8List(0));
    await pumpEventQueue();

    expect(ble.paletteReads, 0);
  });

  test('an empty palette read for a status that names a palette leaves it '
      'loading until a read returns it', () async {
    const palette = [
      LampColor(r: 200, g: 0, b: 30, w: 0),
      LampColor(r: 0, g: 60, b: 90, w: 15),
    ];
    final manual = '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual",'
        '"paletteIdPrefix":"${WispRepository.paletteIdHash(palette)}"}';
    final ble = _HeldReadsBle();
    await ble.connect(_lampId);
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    final n = c.read(wispNotifierProvider(_lampId).notifier);

    // The lamp answers the palette read empty, as before auth.
    notify(ble, manual);
    await pumpEventQueue();
    expect(ble.paletteReads, 1);
    expect(n.paletteLoading, isTrue,
        reason: 'an empty editor would let Save overwrite the palette');

    ble.seedSection(_lampId, 'wisppalette',
        Uint8List.fromList([for (final x in palette) ...[x.r, x.g, x.b, x.w]]));
    notify(ble, manual);
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse);
    expect(n.savedManualPalette, palette);
  });

  test('an Osc wisp with no manual palette reads it once', () async {
    const osc = '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"osc",'
        '"paletteIdPrefix":"0a1b2c3d"}';
    final ble = _HeldReadsBle();
    await ble.connect(_lampId);
    ble.seedSection(_lampId, 'wispstatus', _b(osc));
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    final n = c.read(wispNotifierProvider(_lampId).notifier);
    expect(ble.paletteReads, 1);

    for (var i = 0; i < 3; i++) {
      notify(ble, osc);
      await pumpEventQueue();
    }

    expect(n.paletteLoading, isFalse);
    expect(ble.paletteReads, 1,
        reason: 'a read per status would hold off the claims and config '
            'reads');
  });

  test('a palette read empty in Off is read again when Manual names one',
      () async {
    const palette = [
      LampColor(r: 10, g: 20, b: 30, w: 0),
      LampColor(r: 250, g: 0, b: 5, w: 40),
    ];
    final manual = '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual",'
        '"paletteIdPrefix":"${WispRepository.paletteIdHash(palette)}"}';
    final ble = _HeldReadsBle();
    await ble.connect(_lampId);
    ble.seedSection(_lampId, 'wispstatus',
        _b('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"off"}'));
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    final n = c.read(wispNotifierProvider(_lampId).notifier);
    expect(n.paletteLoading, isFalse);

    // The lamp's palette cache is still empty when the wisp goes Manual.
    notify(ble, manual);
    await pumpEventQueue();
    expect(n.paletteLoading, isTrue,
        reason: 'an empty editor would let Save overwrite the palette');

    ble.seedSection(_lampId, 'wisppalette',
        Uint8List.fromList([for (final x in palette) ...[x.r, x.g, x.b, x.w]]));
    notify(ble, manual);
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse);
    expect(n.savedManualPalette, palette);
  });

  test('choosing Manual after an empty read in Off shows the loading line '
      'until a read for Manual lands', () async {
    const palette = [
      LampColor(r: 33, g: 0, b: 99, w: 0),
      LampColor(r: 0, g: 140, b: 70, w: 25),
    ];
    final ble = _HeldReadsBle();
    await ble.connect(_lampId);
    ble.seedSection(_lampId, 'wispstatus',
        _b('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"off"}'));
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    final n = c.read(wispNotifierProvider(_lampId).notifier);
    expect(n.paletteLoading, isFalse);

    final source = n.setSource(WispSourceMode.manual);
    expect(n.paletteLoading, isTrue,
        reason: 'an empty editor would let Save overwrite the palette');

    ble.seedSection(_lampId, 'wisppalette',
        Uint8List.fromList([for (final x in palette) ...[x.r, x.g, x.b, x.w]]));
    notify(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual",'
        '"paletteIdPrefix":"${WispRepository.paletteIdHash(palette)}"}');
    await source;
    await pumpEventQueue();
    expect(n.paletteLoading, isFalse);
    expect(n.savedManualPalette, palette);
  });

  test('choosing Manual keeps a palette already held, and re-tapping '
      'Manual keeps an empty one', () async {
    const palette = [LampColor(r: 5, g: 6, b: 7, w: 8)];
    final ble = _HeldReadsBle();
    await ble.connect(_lampId);
    ble.seedSection(_lampId, 'wispstatus',
        _b('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"off"}'));
    ble.seedSection(_lampId, 'wisppalette',
        Uint8List.fromList([for (final x in palette) ...[x.r, x.g, x.b, x.w]]));
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    final n = c.read(wispNotifierProvider(_lampId).notifier);
    expect(n.savedManualPalette, palette);

    unawaited(n.setSource(WispSourceMode.manual).catchError((Object _) {}));
    expect(n.paletteLoading, isFalse, reason: 'the palette is held');

    final empty = _HeldReadsBle();
    await empty.connect(_lampId);
    empty.seedSection(_lampId, 'wispstatus', _b(_emptyManual));
    final c2 = makeContainer(empty);
    c2.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c2.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    final n2 = c2.read(wispNotifierProvider(_lampId).notifier);
    expect(n2.paletteLoading, isFalse);

    unawaited(n2.setSource(WispSourceMode.manual).catchError((Object _) {}));
    expect(n2.paletteLoading, isFalse,
        reason: 'the wisp is already Manual with an empty palette');

    final osc = _HeldReadsBle();
    await osc.connect(_lampId);
    osc.seedSection(_lampId, 'wispstatus',
        _b('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"osc"}'));
    final c3 = makeContainer(osc);
    c3.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c3.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    final n3 = c3.read(wispNotifierProvider(_lampId).notifier);
    expect(n3.paletteLoading, isFalse);

    unawaited(n3.setSource(WispSourceMode.off).catchError((Object _) {}));
    expect(n3.paletteLoading, isFalse,
        reason: 'only choosing Manual returns the palette to unread');
  });

  test('a palette never read is read on the next status, whatever its '
      'palette id', () async {
    final ble = _HeldReadsBle();
    await ble.connect(_lampId);
    // No status to read: build's status read throws and build returns before
    // its palette read.
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    expect(ble.paletteReads, 0);

    notify(ble, _emptyManual);
    await pumpEventQueue();

    final n = c.read(wispNotifierProvider(_lampId).notifier);
    expect(c.read(wispNotifierProvider(_lampId)).value!.present, isTrue);
    expect(ble.paletteReads, 1);
    expect(n.paletteLoading, isFalse);
  });
}
