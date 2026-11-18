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
import '../../_support/wisp_palette_ble.dart';

const _lampId = 'palette-cap-lamp';
const _mac = 'AA:BB:CC:DD:EE:FF';

final _twenty = [
  for (var i = 0; i < 20; i++)
    LampColor(r: i + 1, g: 40 + i, b: 200 - i, w: i.isEven ? 0 : i),
];

/// A full status from a wisp that stores [p] and states all of it.
String _status(List<LampColor> p, {String source = 'manual'}) => jsonEncode({
  'wispMac': _mac,
  'source': source,
  'manualId': WispRepository.statusManualId(p),
  if (p.isNotEmpty) 'paletteIdPrefix': WispRepository.manualPaletteId(p),
});

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));
Uint8List _bytes(List<LampColor> p) => Uint8List.fromList([
  for (final c in p) ...[c.r, c.g, c.b, c.w],
]);

/// A wisp from before the 16-colour cap can hold a longer Manual palette.
/// The editor holds its first 16, says so, and offers Save.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUp(() => SharedPreferences.setMockInitialValues({}));

  ProviderSubscription<Object?>? visit;

  Future<WispNotifier> open(
    InMemoryBleClient ble,
    ProviderContainer c,
    List<LampColor> held, {
    String source = 'manual',
  }) async {
    ble.seedSection(_lampId, 'wispstatus', _b(_status(held, source: source)));
    ble.seedSection(_lampId, 'wisppalette', _bytes(held));
    visit = c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    return c.read(wispNotifierProvider(_lampId).notifier);
  }

  ProviderContainer container(InMemoryBleClient ble) {
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    return c;
  }

  test('a palette past 16 colours seeds the editor with its first 16',
      () async {
    final ble = InMemoryBleClient();
    await ble.connect(_lampId);
    final n = await open(ble, container(ble), _twenty);

    expect(n.paletteLoading, isFalse);
    expect(n.savedManualPalette, _twenty);
    expect(n.draftManualPalette, _twenty.take(16));
    expect(n.manualPaletteTrimmed, isTrue);
    expect(n.manualPaletteDirty, isTrue);
  });

  test('a palette of 16 colours is not trimmed', () async {
    final ble = InMemoryBleClient();
    await ble.connect(_lampId);
    final sixteen = _twenty.take(16).toList();
    final n = await open(ble, container(ble), sixteen);

    expect(n.draftManualPalette, sixteen);
    expect(n.manualPaletteTrimmed, isFalse);
    expect(n.manualPaletteDirty, isFalse);
  });

  // The trimmed draft is the editor's own seed, so a later visit treats it
  // as untouched and seeds from what the wisp holds then.
  test('a later visit reseeds an untouched trimmed draft', () async {
    final ble = InMemoryBleClient();
    await ble.connect(_lampId);
    final c = container(ble);
    await open(ble, c, _twenty);
    visit!.close();
    await pumpEventQueue();
    expect(c.exists(wispNotifierProvider(_lampId)), isFalse);

    final n = await open(ble, c, const [], source: 'off');

    expect(n.paletteLoading, isFalse);
    expect(n.draftManualPalette, isEmpty);
    expect(n.manualPaletteTrimmed, isFalse);
  });

  test('a palette changed elsewhere replaces an untouched trimmed draft',
      () async {
    final ble = InMemoryBleClient();
    await ble.connect(_lampId);
    final n = await open(ble, container(ble), _twenty);
    final three = _twenty.skip(5).take(3).toList();

    ble.seedSection(_lampId, 'wisppalette', _bytes(three));
    ble.simulateNotify(_lampId, BleUuids.controlService, BleUuids.wispStatus,
        _b(_status(three)));
    await pumpEventQueue();

    expect(n.savedManualPalette, three);
    expect(n.draftManualPalette, three);
    expect(n.manualPaletteDirty, isFalse);
  });

  test('a save sends the first 16 and confirms on the wisp storing them',
      () async {
    final ble = InMemoryBleClient();
    await ble.connect(_lampId);
    final n = await open(ble, container(ble), _twenty);

    final save = n.setManualPalette();
    await pumpEventQueue();
    final sent = [
      for (final w in ble.writesTo(_lampId, BleUuids.wispOp))
        if (utf8.decode(w).contains('setManualPalette'))
          jsonDecode(utf8.decode(w)) as Map<String, dynamic>,
    ].single;
    expect((sent['colors'] as List).length, 16);

    ble.simulateNotify(_lampId, BleUuids.controlService, BleUuids.wispStatus,
        _b(echoOfLastPaletteSave(ble, _lampId, mac: _mac)));
    await save;

    expect(n.savedManualPalette, _twenty.take(16));
    expect(n.manualPaletteTrimmed, isFalse);
    expect(n.manualPaletteDirty, isFalse);
  });
}
