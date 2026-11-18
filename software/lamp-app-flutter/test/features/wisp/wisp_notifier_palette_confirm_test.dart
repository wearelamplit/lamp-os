import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/wisp_palette_ble.dart';

const _lampId = 'confirm-lamp';
const _mac = 'AA:BB:CC:DD:EE:FF';
final _defaultSeqStart = WispNotifier.debugSeqStart;

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

  List<Map<String, dynamic>> paletteOps(InMemoryBleClient ble) => [
        for (final w in ble.writesTo(_lampId, BleUuids.wispOp))
          if ((jsonDecode(utf8.decode(w)) as Map<String, dynamic>)['op'] ==
              'setManualPalette')
            jsonDecode(utf8.decode(w)) as Map<String, dynamic>,
      ];

  // A wisp in Off, on a lamp serving the sections, with a `wispconfig` read
  // held in flight. setSource(manual) then leaves the held state saying
  // Manual, which is the app's own optimism. The wisp has said neither that
  // nor the save's echo.
  test('a config read completing during an empty save confirms neither the '
      'save nor the pending setSource', () async {
    WispNotifier.debugSeqStart = () => 5;
    addTearDown(() => WispNotifier.debugSeqStart = _defaultSeqStart);
    final ble = _HeldConfigBle();
    await ble.connect(_lampId);
    const off = '{"wispMac":"$_mac","configSplit":true,"source":"off",'
        '"manualId":""';
    ble.seedSection(_lampId, 'wispstatus', _b('$off,"configLastSeenMs":100}'));
    ble.seedSection(_lampId, 'wispconfig', _b(_config()));
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    final n = c.read(wispNotifierProvider(_lampId).notifier);

    // A status with a new config stamp starts a config read, still held once
    // the save has sent.
    ble.holdConfig = Completer<void>();
    ble.seedSection(_lampId, 'wispconfig', _b(_config(brightness: 25)));
    notify(ble, '$off,"configLastSeenMs":200}');
    await pumpEventQueue();

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
        '"manualId":"","manualSeq":5,"configLastSeenMs":200}');
    await source;
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
        _b('{"wispMac":"$_mac","source":"manual","manualId":""}'));
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
    notify(ble, echoOfLastPaletteSave(ble, _lampId, mac: _mac));
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
}

/// Holds every `wispconfig` read while [holdConfig] is set, so a test can
/// leave a config read in flight.
class _HeldConfigBle extends InMemoryBleClient {
  Completer<void>? holdConfig;

  @override
  Future<Uint8List> readSection(String deviceId, String name) async {
    final hold = holdConfig;
    if (name == 'wispconfig' && hold != null) await hold.future;
    return super.readSection(deviceId, name);
  }
}
