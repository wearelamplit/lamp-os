import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/application/manual_palette_draft.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';

const _lampId = 'reopen-lamp';
const _mac = 'AA:BB:CC:DD:EE:FF';
const _first = [
  LampColor(r: 200, g: 10, b: 30, w: 0),
  LampColor(r: 5, g: 90, b: 160, w: 0),
  LampColor(r: 40, g: 40, b: 0, w: 70),
];

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

/// The manual-palette draft outlives the notifier: leaving a lamp disposes
/// the notifier and opening it again builds a fresh one over the same draft.
/// What the editor shows, and whether Save is lit, after the wisp's palette
/// changed elsewhere in between.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUp(() => SharedPreferences.setMockInitialValues({}));

  late InMemoryBleClient ble;
  late ProviderContainer c;

  // The lamp's view of the wisp: its status (prefix of [palette], none when
  // empty) and its `wisppalette` section.
  Future<void> wispHolds(List<LampColor> palette) async {
    final prefix = palette.isEmpty
        ? ''
        : ',"paletteIdPrefix":"${WispRepository.paletteIdHash(palette)}"';
    await ble.write(_lampId, BleUuids.controlService, BleUuids.wispStatus,
        _b('{"wispMac":"$_mac","source":"manual"$prefix}'));
    ble.seedSection(_lampId, 'wisppalette',
        Uint8List.fromList([for (final x in palette) ...[x.r, x.g, x.b, x.w]]));
  }

  Future<ProviderSubscription<Object?>> openLamp() async {
    final sub = c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    await pumpEventQueue();
    return sub;
  }

  Future<void> leaveLamp(ProviderSubscription<Object?> sub) async {
    sub.close();
    await pumpEventQueue();
    expect(c.exists(wispNotifierProvider(_lampId)), isFalse,
        reason: 'leaving the lamp disposes its notifier');
  }

  WispNotifier notifier() => c.read(wispNotifierProvider(_lampId).notifier);
  List<LampColor> draft() => c.read(manualPaletteDraftProvider(_lampId)).colors;

  setUp(() async {
    ble = InMemoryBleClient();
    await ble.connect(_lampId);
    await wispHolds(_first);
    c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
  });

  test('a palette changed elsewhere replaces an untouched draft', () async {
    const moved = [
      LampColor(r: 1, g: 250, b: 2, w: 0),
      LampColor(r: 3, g: 4, b: 240, w: 9),
    ];
    await leaveLamp(await openLamp());
    expect(draft(), _first);

    await wispHolds(moved);
    await openLamp();

    expect(draft(), moved);
    expect(notifier().manualPaletteDirty, isFalse);
  });

  test('a palette emptied elsewhere does not light Save', () async {
    await leaveLamp(await openLamp());

    await wispHolds(const []);
    await openLamp();

    expect(notifier().manualPaletteDirty, isFalse,
        reason: 'Save would write the old palette back over the clear');
  });

  test('an unsaved edit survives leaving the lamp', () async {
    const edit = [
      LampColor(r: 200, g: 10, b: 30, w: 0),
      LampColor(r: 0, g: 0, b: 0, w: 255),
    ];
    final sub = await openLamp();
    notifier().setManualPaletteDraft(edit);
    await leaveLamp(sub);

    await openLamp();

    expect(draft(), edit);
    expect(notifier().manualPaletteDirty, isTrue);
  });
}
