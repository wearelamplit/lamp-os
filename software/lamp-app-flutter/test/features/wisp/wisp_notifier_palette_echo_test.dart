import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';
import 'package:lamp_app/features/wisp/domain/wisp_status.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';

const _lampId = 'palette-echo-lamp';
const _mac = 'AA:BB:CC:DD:EE:FF';

const _x = [
  LampColor(r: 200, g: 0, b: 30, w: 0),
  LampColor(r: 0, g: 60, b: 90, w: 15),
];
const _y = [LampColor(r: 5, g: 6, b: 7, w: 0)];

String _id(List<LampColor> p) => WispRepository.statusManualId(p);

/// A status from a wisp that states its palette and echoes palette saves.
String _status({
  String source = 'manual',
  required String manualId,
  int manualSeq = 0,
  String prefix = '',
  int? statusLastSeenMs,
  int? lastSeenMs,
}) => jsonEncode({
  'wispMac': _mac,
  'source': source,
  'manualId': manualId,
  if (manualSeq != 0) 'manualSeq': manualSeq,
  if (prefix.isNotEmpty) 'paletteIdPrefix': prefix,
  'statusLastSeenMs': ?statusLastSeenMs,
  'lastSeenMs': ?lastSeenMs,
});

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));
Uint8List _bytes(List<LampColor> p) => Uint8List.fromList([
  for (final c in p) ...[c.r, c.g, c.b, c.w],
]);

/// Counts `wispstatus` section reads.
class _CountingBle extends InMemoryBleClient {
  int statusReads = 0;

  @override
  Future<Uint8List> readSection(String deviceId, String name) {
    if (name == 'wispstatus') statusReads++;
    return super.readSection(deviceId, name);
  }
}

/// A Manual palette save on a wisp that echoes the save's `seq`: confirmed
/// only by a status that carries that seq and the palette sent.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUp(() => SharedPreferences.setMockInitialValues({}));
  final defaultSeqStart = WispNotifier.debugSeqStart;
  tearDown(() => WispNotifier.debugSeqStart = defaultSeqStart);

  void notify(InMemoryBleClient ble, String json) => ble.simulateNotify(
    _lampId,
    BleUuids.controlService,
    BleUuids.wispStatus,
    _b(json),
  );

  /// The palette ops written to the lamp, decoded.
  List<Map<String, dynamic>> paletteOps(InMemoryBleClient ble) => [
    for (final w in ble.writesTo(_lampId, BleUuids.wispOp))
      if (utf8.decode(w).contains('setManualPalette'))
        jsonDecode(utf8.decode(w)) as Map<String, dynamic>,
  ];

  /// Build the notifier on a lamp serving [initial] and the palette [held].
  Future<WispNotifier> open(
    _CountingBle ble,
    String initial, {
    List<LampColor> held = _x,
  }) async {
    await ble.connect(_lampId);
    ble.seedSection(_lampId, 'wispstatus', _b(initial));
    ble.seedSection(_lampId, 'wisppalette', _bytes(held));
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    return c.read(wispNotifierProvider(_lampId).notifier);
  }

  /// Start a save of [draft]; the returned getter reports whether it ended,
  /// and the future its error, null when it confirmed.
  (bool Function(), Future<Object?>) save(
    WispNotifier n,
    List<LampColor> draft,
  ) {
    n.setManualPaletteDraft(draft);
    var done = false;
    final result = n
        .setManualPalette()
        .then<Object?>((_) => null, onError: (Object e) => e)
        .whenComplete(() => done = true);
    return (() => done, result);
  }

  test(
    'R4: an empty save waits for its own echo, not a newer-looking status',
    () async {
      WispNotifier.debugSeqStart = () => 777;
      final ble = _CountingBle();
      final n = await open(
        ble,
        _status(
          manualId: _id(_x),
          prefix: _id(_x),
          manualSeq: 5,
          statusLastSeenMs: 100,
          lastSeenMs: 1000,
        ),
      );
      expect(n.savedManualPalette, _x);

      final (done, result) = save(n, const []);
      await pumpEventQueue();
      expect(paletteOps(ble).single['seq'], 777);

      // The wisp emptied its palette, but this status is not from this save.
      notify(
        ble,
        _status(
          manualId: '',
          manualSeq: 5,
          statusLastSeenMs: 900,
          lastSeenMs: 9000,
        ),
      );
      await pumpEventQueue();
      expect(done(), isFalse);

      notify(ble, _status(manualId: '', manualSeq: 777));
      expect(await result, isNull);
      expect(n.savedManualPalette, isEmpty);
      expect(n.manualPaletteWriteFailed, isFalse);
    },
  );

  test('id half: the echo confirms only with the palette sent', () async {
    WispNotifier.debugSeqStart = () => 31;
    final ble = _CountingBle();
    final n = await open(ble, _status(manualId: _id(_x), prefix: _id(_x)));

    final (done, result) = save(n, _y);
    await pumpEventQueue();
    notify(ble, _status(manualId: _id(_x), manualSeq: 31));
    await pumpEventQueue();
    expect(done(), isFalse);

    notify(ble, _status(manualId: _id(_y), manualSeq: 31));
    expect(await result, isNull);
    expect(n.savedManualPalette, _y);
  });

  test('no pre-read: an empty save reads no status before it sends', () async {
    WispNotifier.debugSeqStart = () => 12;
    final ble = _CountingBle();
    final n = await open(ble, _status(manualId: _id(_x), prefix: _id(_x)));
    final readsBefore = ble.statusReads;

    final (_, result) = save(n, const []);
    await pumpEventQueue();
    expect(paletteOps(ble), hasLength(1));
    expect(ble.statusReads, readsBefore);

    notify(ble, _status(manualId: '', manualSeq: 12));
    expect(await result, isNull);
  });

  test(
    'source switch: a save echoed after the wisp left Manual confirms',
    () async {
      WispNotifier.debugSeqStart = () => 4000;
      final ble = _CountingBle();
      final n = await open(ble, _status(manualId: _id(_x), prefix: _id(_x)));

      final (_, result) = save(n, _y);
      await pumpEventQueue();
      notify(ble, _status(source: 'osc', manualId: _id(_y), manualSeq: 4000));
      expect(await result, isNull);
      expect(n.savedManualPalette, _y);
      expect(n.manualPaletteWriteFailed, isFalse);
    },
  );

  test('resend reuses seq: a resent save carries the same seq and its echo '
      'confirms', () async {
    WispNotifier.debugSeqStart = () => 600;
    final ble = _CountingBle();
    final n = await open(ble, _status(manualId: _id(_x), prefix: _id(_x)));

    final (_, result) = save(n, _y);
    await pumpEventQueue();
    expect(paletteOps(ble), hasLength(1));
    // No echo: the op was lost, so the save sends it again.
    while (paletteOps(ble).length < 2) {
      await Future<void>.delayed(const Duration(milliseconds: 100));
    }
    final ops = paletteOps(ble);
    expect(ops[1]['seq'], ops[0]['seq']);

    notify(ble, _status(manualId: _id(_y), manualSeq: ops[1]['seq'] as int));
    expect(await result, isNull);
    expect(n.savedManualPalette, _y);
  }, timeout: const Timeout(Duration(seconds: 30)));

  test('reboot: a status holding the palette sent but no echo does not '
      'confirm', () async {
    WispNotifier.debugSeqStart = () => 90;
    final ble = _CountingBle();
    final n = await open(ble, _status(manualId: _id(_x), prefix: _id(_x)));

    final (done, result) = save(n, _y);
    await pumpEventQueue();
    // The wisp stored the palette, then rebooted before its status went out.
    notify(
      ble,
      _status(manualId: _id(_y), prefix: WispRepository.paletteIdHash(_y)),
    );
    await pumpEventQueue();
    expect(done(), isFalse);

    notify(ble, _status(manualId: _id(_y), manualSeq: 90));
    expect(await result, isNull);
  });

  test('other phone: another save\'s echo does not confirm this one', () async {
    WispNotifier.debugSeqStart = () => 222;
    final ble = _CountingBle();
    final n = await open(ble, _status(manualId: _id(_x), prefix: _id(_x)));

    final (done, result) = save(n, _y);
    await pumpEventQueue();
    notify(ble, _status(manualId: _id(_x), manualSeq: 9999));
    await pumpEventQueue();
    expect(done(), isFalse);

    notify(ble, _status(manualId: _id(_y), manualSeq: 222));
    expect(await result, isNull);
  });

  test(
    'skip last heard: a save never reuses the seq the wisp echoes now',
    () async {
      WispNotifier.debugSeqStart = () => 4321;
      final ble = _CountingBle();
      final n = await open(
        ble,
        _status(manualId: _id(_x), prefix: _id(_x), manualSeq: 4321),
      );

      final (_, result) = save(n, _y);
      await pumpEventQueue();
      final seq = paletteOps(ble).single['seq'] as int;
      expect(seq, isNot(4321));
      expect(seq, inInclusiveRange(1, 65535));

      notify(ble, _status(manualId: _id(_y), manualSeq: seq));
      expect(await result, isNull);
    },
  );

  test('seqs stay in 1..65535 and step past 0', () async {
    WispNotifier.debugSeqStart = () => 65534;
    final ble = _CountingBle();
    final n = await open(ble, _status(manualId: _id(_x), prefix: _id(_x)));

    var palette = _y;
    for (final expected in [65534, 65535, 1]) {
      final (_, result) = save(n, palette);
      await pumpEventQueue();
      expect(paletteOps(ble).last['seq'], expected);
      notify(ble, _status(manualId: _id(palette), manualSeq: expected));
      expect(await result, isNull);
      palette = palette == _y ? _x : _y;
    }
  });

  test('old wisp op: a save to a wisp that never stated its palette sends no '
      'seq', () async {
    final ble = _CountingBle();
    final n = await open(
      ble,
      '{"wispMac":"$_mac","source":"manual",'
      '"paletteIdPrefix":"${WispRepository.paletteIdHash(_x)}"}',
    );

    final (_, result) = save(n, _y);
    await pumpEventQueue();
    final writes = [
      for (final w in ble.writesTo(_lampId, BleUuids.wispOp))
        if (utf8.decode(w).contains('setManualPalette')) utf8.decode(w),
    ];
    expect(writes.single, jsonEncode(WispRepository.buildManualPaletteOp(_y)));

    notify(
      ble,
      '{"wispMac":"$_mac","source":"manual",'
      '"paletteIdPrefix":"${WispRepository.paletteIdHash(_y)}"}',
    );
    expect(await result, isNull);
  });

  group('the op and the status field', () {
    test('10 colours with W on each and the widest seq fit whole', () {
      final palette = List<LampColor>.filled(
        10,
        const LampColor(r: 255, g: 255, b: 255, w: 255),
      );
      final op = WispRepository.buildManualPaletteOp(palette, seq: 65535);
      expect(utf8.encode(jsonEncode(op)).length, 244);
      expect(
        (op['colors'] as List).every((c) => (c as List).length == 4),
        isTrue,
      );
      expect(
        WispRepository.sentManualId(palette, seq: 65535),
        WispRepository.statusManualId(palette),
      );
    });

    test('the expected id is the colours the op carries, after any strip', () {
      final palette = List<LampColor>.filled(
        25,
        const LampColor(r: 255, g: 255, b: 255, w: 255),
      );
      final op = WispRepository.buildManualPaletteOp(palette, seq: 65535);
      expect(
        utf8.encode(jsonEncode(op)).length,
        lessThanOrEqualTo(WispRepository.kWispOpPayloadCap),
      );
      final sent = [
        for (final c in op['colors'] as List)
          LampColor(
            r: (c as List)[0] as int,
            g: c[1] as int,
            b: c[2] as int,
            w: c.length > 3 ? c[3] as int : 0,
          ),
      ];
      expect(sent.last.w, 0, reason: 'the seq costs the last colour its W');
      expect(
        WispRepository.sentManualId(palette, seq: 65535),
        WispRepository.statusManualId(sent),
      );
      expect(
        WispRepository.sentManualId(palette, seq: 65535),
        isNot(WispRepository.statusManualId(palette)),
      );
    });

    test('manualSeq parses, is 0 when absent, and survives a copy', () {
      final s = WispStatus.fromBytes(
        _b('{"wispMac":"$_mac","source":"osc","manualSeq":4321}'),
      );
      expect(s.manualSeq, 4321);
      expect(s.copyWith(brightness: 30).manualSeq, 4321);
      expect(WispStatus.fromBytes(_b('{"wispMac":"$_mac"}')).manualSeq, 0);
    });
  });
}
