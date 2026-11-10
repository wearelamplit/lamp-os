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

const _lampId = 'confirm-lamp';
const _mac = 'AA:BB:CC:DD:EE:FF';
const _rivalMac = '11:22:33:44:55:66';

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

String _config({int brightness = 80}) =>
    '{"char":"wispConfig","ledType":"GRBW","px":60,"brightness":$brightness,'
    '"driftIntervalMs":90000,"driftFadePct":40,"shuffleSeed":7,"opSeq":3}';

/// Which wisp statuses may confirm a palette or source write, how a newer
/// palette save and a second notifier for the same wisp behave, and which
/// statuses and palette reads change the palette a notifier holds.
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

  void notify(InMemoryBleClient ble, String json, {String lampId = _lampId}) {
    ble.simulateNotify(
      lampId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      _b(json),
    );
  }

  void seedPalette(
    InMemoryBleClient ble,
    List<LampColor> colors, {
    String lampId = _lampId,
  }) {
    ble.seedSection(
      lampId,
      'wisppalette',
      Uint8List.fromList([
        for (final c in colors) ...[c.r, c.g, c.b, c.w],
      ]),
    );
  }

  String manualWith(List<LampColor> palette, {String mac = _mac}) =>
      '{"wispMac":"$mac","source":"manual",'
      '"paletteIdPrefix":"${WispRepository.paletteIdHash(palette)}"}';

  List<Map<String, dynamic>> paletteOps(InMemoryBleClient ble) => [
        for (final w in ble.writesTo(_lampId, BleUuids.wispOp))
          if ((jsonDecode(utf8.decode(w)) as Map<String, dynamic>)['op'] ==
              'setManualPalette')
            jsonDecode(utf8.decode(w)) as Map<String, dynamic>,
      ];

  // A wisp in Off, on a lamp serving the sections, with a `wispconfig` read
  // held in flight. setSource(manual) then leaves the held state saying
  // Manual with no palette prefix, which is the app's own optimism. The wisp
  // has said neither.
  test('a config read completing during an empty save confirms neither the '
      'save nor the pending setSource', () async {
    final ble = _HeldConfigBle();
    await ble.connect(_lampId);
    const off = '{"wispMac":"$_mac","configSplit":true,"source":"off"';
    ble.seedSection(_lampId, 'wispstatus', _b('$off,"configLastSeenMs":100}'));
    ble.seedSection(_lampId, 'wispconfig', _b(_config()));
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    final n = c.read(wispNotifierProvider(_lampId).notifier);

    // The save's own status read finds a new config stamp, so the config read
    // it starts is still held once the save has sent.
    ble.holdConfig = Completer<void>();
    ble.seedSection(_lampId, 'wispconfig', _b(_config(brightness: 25)));
    ble.seedSection(_lampId, 'wispstatus', _b('$off,"configLastSeenMs":200}'));

    expect(n.draftManualPalette, isEmpty);
    var saveDone = false;
    final save = n.setManualPalette().whenComplete(() => saveDone = true);
    await pumpEventQueue();
    expect(paletteOps(ble), isNotEmpty, reason: 'the save has sent');
    var sourceDone = false;
    final source =
        n.setSource(WispSourceMode.manual).whenComplete(() => sourceDone = true);
    await pumpEventQueue();

    ble.holdConfig!.complete();
    ble.holdConfig = null;
    await pumpEventQueue();
    expect(c.read(wispNotifierProvider(_lampId)).value!.brightness, 25,
        reason: 'the held config read did complete');
    expect(saveDone, isFalse, reason: 'app-made Manual state is no confirm');
    expect(sourceDone, isFalse, reason: 'app-made Manual state is no confirm');

    notify(ble, '{"wispMac":"$_mac","configSplit":true,"source":"manual",'
        '"configLastSeenMs":200}');
    await source;
    await save;
    expect(n.manualPaletteWriteFailed, isFalse);
  });

  test('an emptied palette is not confirmed by another wisp\'s prefix-less '
      'Manual status', () async {
    const saved = [LampColor(r: 7, g: 8, b: 9, w: 0)];
    final ble = InMemoryBleClient();
    seedPalette(ble, saved);
    await ble.connect(_lampId);
    await ble.write(_lampId, BleUuids.controlService, BleUuids.wispStatus,
        _b(manualWith(saved)));
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    final n = c.read(wispNotifierProvider(_lampId).notifier);
    n.removeManualPaletteColor(0);

    var done = false;
    final save = n.setManualPalette().whenComplete(() => done = true);
    await pumpEventQueue();
    notify(ble, '{"wispMac":"$_rivalMac","source":"manual"}');
    await pumpEventQueue();
    expect(done, isFalse, reason: 'a rival wisp emptied nothing of ours');

    notify(ble, '{"wispMac":"$_mac","source":"manual"}');
    await save;
    expect(n.manualPaletteWriteFailed, isFalse);
  });

  // Runs in real time, since A's first confirm window has to lapse before its
  // loop either resends or sees itself superseded.
  test('a newer palette save supersedes an older one\'s resends', () async {
    const a = [LampColor(r: 200, g: 10, b: 10, w: 0)];
    const b = [LampColor(r: 10, g: 10, b: 200, w: 0)];
    final ble = InMemoryBleClient();
    await ble.connect(_lampId);
    await ble.write(_lampId, BleUuids.controlService, BleUuids.wispStatus,
        _b('{"wispMac":"$_mac","source":"manual"}'));
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    final n = c.read(wispNotifierProvider(_lampId).notifier);

    Object? errA;
    n.setManualPaletteDraft(a);
    final saveA = n.setManualPalette().catchError((Object e) => errA = e);
    await pumpEventQueue();
    n.setManualPaletteDraft(b);
    final saveB = n.setManualPalette();
    await pumpEventQueue();
    final firstB = paletteOps(ble).length - 1;
    expect(paletteOps(ble)[firstB]['colors'], [
      [10, 10, 200]
    ]);

    seedPalette(ble, b);
    notify(ble, manualWith(b));
    await saveB;
    await saveA;

    final afterB = paletteOps(ble).sublist(firstB + 1);
    expect(afterB.where((op) => (op['colors'] as List).first[0] == 200),
        isEmpty,
        reason: 'no write of A follows B');
    expect(errA, isNull, reason: 'a superseded save is not a failure');
    expect(n.savedManualPalette, b);
    expect(n.draftManualPalette, b);
    expect(n.manualPaletteDirty, isFalse);
    expect(n.manualPaletteWriteFailed, isFalse);
  }, timeout: const Timeout(Duration(seconds: 40)));

  // A Manual status without a prefix can be a stale copy, so only a
  // non-empty palette read or the user's own save changes what a notifier
  // holds.
  test('a second notifier keeps its palette after another notifier empties '
      'it', () async {
    const saved = [
      LampColor(r: 1, g: 2, b: 3, w: 0),
      LampColor(r: 4, g: 5, b: 6, w: 0),
    ];
    const lamp2 = 'relay-lamp';
    final ble = InMemoryBleClient();
    for (final lamp in [_lampId, lamp2]) {
      seedPalette(ble, saved, lampId: lamp);
      await ble.connect(lamp);
      await ble.write(lamp, BleUuids.controlService, BleUuids.wispStatus,
          _b(manualWith(saved)));
    }
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    c.listen(wispNotifierProvider(lamp2), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await c.read(wispNotifierProvider(lamp2).future);
    final n1 = c.read(wispNotifierProvider(_lampId).notifier);
    final n2 = c.read(wispNotifierProvider(lamp2).notifier);
    expect(n2.savedManualPalette, saved);

    n1.setManualPaletteDraft(const []);
    final save = n1.setManualPalette();
    await pumpEventQueue();
    // The wisp stored the empty palette; its status reaches both lamps. The
    // lamps' palette caches now hold nothing, which reads as no section.
    for (final lamp in [_lampId, lamp2]) {
      ble.seedSection(lamp, 'wisppalette', Uint8List(0));
      notify(ble, '{"wispMac":"$_mac","source":"manual"}', lampId: lamp);
    }
    await save;
    await pumpEventQueue();

    expect(n1.savedManualPalette, isEmpty);
    expect(c.read(wispNotifierProvider(lamp2)).value!.paletteIdPrefix, isEmpty,
        reason: 'the second notifier took the prefix-less status');
    expect(n2.savedManualPalette, saved);
    expect(n2.draftManualPalette, saved);
    expect(n2.manualPaletteDirty, isFalse);
  });

  test('an empty palette read keeps the saved palette and draft', () async {
    const saved = [LampColor(r: 1, g: 2, b: 3, w: 0)];
    const moved = [LampColor(r: 9, g: 8, b: 7, w: 0)];
    final ble = _HeldConfigBle();
    seedPalette(ble, saved);
    await ble.connect(_lampId);
    await ble.write(_lampId, BleUuids.controlService, BleUuids.wispStatus,
        _b(manualWith(saved)));
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    final n = c.read(wispNotifierProvider(_lampId).notifier);
    expect(n.savedManualPalette, saved);

    // A failed or empty section read decodes to no colours.
    ble.seedSection(_lampId, 'wisppalette', Uint8List(0));
    final readsBefore = ble.paletteReads;
    notify(ble, manualWith(moved));
    await pumpEventQueue();

    expect(ble.paletteReads, greaterThan(readsBefore),
        reason: 'the changed prefix triggered a palette read');
    expect(n.savedManualPalette, saved);
    expect(n.draftManualPalette, saved);
    expect(n.manualPaletteDirty, isFalse);
  });

  test('a status without a palette prefix starts no palette read', () async {
    const saved = [
      LampColor(r: 1, g: 2, b: 3, w: 0),
      LampColor(r: 60, g: 0, b: 90, w: 20),
    ];
    final ble = _HeldConfigBle();
    seedPalette(ble, saved);
    await ble.connect(_lampId);
    await ble.write(_lampId, BleUuids.controlService, BleUuids.wispStatus,
        _b(manualWith(saved)));
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    final n = c.read(wispNotifierProvider(_lampId).notifier);
    expect(n.savedManualPalette, saved);

    final readsBefore = ble.paletteReads;
    for (final source in ['manual', 'off', 'osc']) {
      notify(ble, '{"wispMac":"$_mac","source":"$source"}');
      await pumpEventQueue();
    }

    expect(ble.paletteReads, readsBefore);
    expect(n.savedManualPalette, saved);
  });
}

/// Holds every `wispconfig` read while [holdConfig] is set, so a test can
/// leave a config read in flight, and counts `wisppalette` reads.
class _HeldConfigBle extends InMemoryBleClient {
  Completer<void>? holdConfig;
  int paletteReads = 0;

  @override
  Future<Uint8List> readSection(String deviceId, String name) async {
    if (name == 'wisppalette') paletteReads++;
    final hold = holdConfig;
    if (name == 'wispconfig' && hold != null) await hold.future;
    return super.readSection(deviceId, name);
  }
}
