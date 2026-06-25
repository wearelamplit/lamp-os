import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Helpers + setup for the notifier-level palette tests.
///
/// The notifier reads CHAR_WISP_STATUS on build and subscribes to its
/// notify stream. The fake BLE client (InMemoryBleClient) needs a value
/// seeded for the initial read or the build path returns [WispStatus.empty]
/// via the catch-all (which is also fine for these tests — we don't care
/// about the status field, only the palette draft helpers).
///
/// The notifier also mirrors the saved palette to SharedPreferences (so
/// the editor can re-hydrate after a hot/cold restart). Tests use the
/// flutter_test `setMockInitialValues` shim so prefs operations resolve
/// against an in-memory map instead of throwing MissingPluginException.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  const lampId = 'test-lamp';

  setUp(() => SharedPreferences.setMockInitialValues({}));

  ProviderContainer makeContainer({InMemoryBleClient? ble}) {
    final client = ble ?? InMemoryBleClient();
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(client)],
    );
    addTearDown(c.dispose);
    return c;
  }

  Future<void> primeStatus(
    InMemoryBleClient ble,
    String json,
  ) async {
    await ble.connect(lampId);
    // The notifier subscribes then reads; we have to write the value so
    // the subsequent read returns it.
    await ble.write(
      lampId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      Uint8List.fromList(utf8.encode(json)),
    );
  }

  group('WispNotifier manual palette draft', () {
    test('starts empty + clean (no dirty save button)', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      expect(n.draftManualPalette, isEmpty);
      expect(n.savedManualPalette, isEmpty);
      expect(n.manualPaletteDirty, isFalse);
    });

    test('appending a color marks the draft dirty', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      n.appendManualPaletteColor(
        const LampColor(r: 255, g: 0, b: 0, w: 0),
      );
      expect(n.draftManualPalette.length, 1);
      expect(n.manualPaletteDirty, isTrue);
    });

    test('caps the draft at 10 colors', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      for (var i = 0; i < 12; i++) {
        n.appendManualPaletteColor(
          LampColor(r: i, g: i, b: i, w: 0),
        );
      }
      expect(n.draftManualPalette.length, 10);
    });

    test('setManualPalette clears the dirty flag on success', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      n.appendManualPaletteColor(
        const LampColor(r: 1, g: 2, b: 3, w: 0),
      );
      n.appendManualPaletteColor(
        const LampColor(r: 4, g: 5, b: 6, w: 0),
      );
      expect(n.manualPaletteDirty, isTrue);

      await n.setManualPalette();
      expect(n.manualPaletteDirty, isFalse);
      expect(n.savedManualPalette.length, 2);
    });

    test('setManualPalette writes a wispOp with RGB triples', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      n.appendManualPaletteColor(
        const LampColor(r: 255, g: 128, b: 64, w: 0),
      );
      await n.setManualPalette();

      // Last value written to wispOp should be the setManualPalette
      // JSON envelope.
      final written = await ble.read(
        lampId,
        BleUuids.controlService,
        BleUuids.wispOp,
      );
      final decoded =
          jsonDecode(utf8.decode(written)) as Map<String, dynamic>;
      expect(decoded['char'], 'wispOp');
      expect(decoded['op'], 'setManualPalette');
      expect(decoded['colors'], [
        [255, 128, 64]
      ]);
    });

    test('reorder moves a color', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      n.appendManualPaletteColor(
        const LampColor(r: 1, g: 0, b: 0, w: 0),
      );
      n.appendManualPaletteColor(
        const LampColor(r: 2, g: 0, b: 0, w: 0),
      );
      n.appendManualPaletteColor(
        const LampColor(r: 3, g: 0, b: 0, w: 0),
      );

      n.reorderManualPaletteColor(0, 2);
      // After moving index 0 to the back, the order becomes [2, 3, 1].
      // (`newIndex` is the post-removal target index — standard
      // ReorderableListView semantics.)
      expect(n.draftManualPalette.map((c) => c.r).toList(),
          [2, 3, 1]);
    });

    test('remove drops a swatch', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      n.appendManualPaletteColor(
        const LampColor(r: 1, g: 0, b: 0, w: 0),
      );
      n.appendManualPaletteColor(
        const LampColor(r: 2, g: 0, b: 0, w: 0),
      );
      n.removeManualPaletteColor(0);
      expect(n.draftManualPalette.length, 1);
      expect(n.draftManualPalette.first.r, 2);
    });
  });

  group('WispNotifier setSource', () {
    test('writes a setSource wispOp with the wire-format mode string',
        () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      await n.setSource(WispSourceMode.manual);
      final written = await ble.read(
        lampId,
        BleUuids.controlService,
        BleUuids.wispOp,
      );
      final decoded =
          jsonDecode(utf8.decode(written)) as Map<String, dynamic>;
      expect(decoded['char'], 'wispOp');
      expect(decoded['op'], 'setSource');
      expect(decoded['mode'], 'manual');
    });

    test('optimistically updates state.source so the UI reflects the tap',
        () async {
      final ble = InMemoryBleClient();
      await primeStatus(
          ble, '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"aurora"}');
      final c = makeContainer(ble: ble);

      final initial =
          await c.read(wispNotifierProvider(lampId).future);
      expect(initial.source, WispSourceMode.aurora);

      final n = c.read(wispNotifierProvider(lampId).notifier);
      await n.setSource(WispSourceMode.off);

      // The optimistic update lives on the AsyncData state.
      final after = c.read(wispNotifierProvider(lampId)).value!;
      expect(after.source, WispSourceMode.off);
    });
  });

  // ── Saved palette persistence across notifier rebuilds ───────────────
  // The wisp does not echo the saved palette back in wispStatus (payload
  // budget — only the 8-char ID prefix fits). Before this change, the
  // notifier-instance `_savedManualPalette` was the only copy, so a tab
  // switch (which auto-dispose-tears the notifier down) wiped it. The
  // fix mirrors the saved palette to SharedPreferences keyed by lampId
  // and hydrates from disk on every build.
  group('WispNotifier saved palette persistence', () {
    test('setManualPalette writes the committed colors to SharedPreferences',
        () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      n.appendManualPaletteColor(
        const LampColor(r: 255, g: 0, b: 0, w: 0),
      );
      n.appendManualPaletteColor(
        const LampColor(r: 0, g: 128, b: 64, w: 0),
      );
      await n.setManualPalette();

      // Drain pending microtasks so the fire-and-forget prefs write
      // settles before we read it back.
      // SharedPreferences.getInstance + getString chain through multiple
      // microtasks; pump the event queue until they settle.
      for (var i = 0; i < 10; i++) {
        await Future<void>.delayed(Duration.zero);
      }

      final prefs = await SharedPreferences.getInstance();
      final raw = prefs.getString('wisp.manualPalette.v1.$lampId');
      expect(raw, isNotNull);
      final decoded = jsonDecode(raw!) as List;
      expect(decoded, ['#FF000000', '#00804000']);
    });

    test('hydrates the saved palette from SharedPreferences on build',
        () async {
      // Pre-seed prefs as if a previous app session had committed a
      // palette. A fresh notifier should pick it up so the editor opens
      // populated rather than empty.
      SharedPreferences.setMockInitialValues({
        'wisp.manualPalette.v1.$lampId': jsonEncode([
          '#11223300',
          '#44556600',
        ]),
      });

      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);

      // Hold a listener so the autoDispose provider doesn't tear down
      // between our read() calls — without this the second .notifier
      // read could trigger a fresh build, racing the hydrate.
      final sub = c.listen(wispNotifierProvider(lampId), (_, _) {});
      addTearDown(sub.close);

      await c.read(wispNotifierProvider(lampId).future);
      // Let the fire-and-forget hydrate complete.
      // SharedPreferences.getInstance + getString chain through multiple
      // microtasks; pump the event queue until they settle.
      for (var i = 0; i < 10; i++) {
        await Future<void>.delayed(Duration.zero);
      }

      final n = c.read(wispNotifierProvider(lampId).notifier);
      expect(n.savedManualPalette.length, 2);
      expect(n.savedManualPalette[0].r, 0x11);
      expect(n.savedManualPalette[0].g, 0x22);
      expect(n.savedManualPalette[0].b, 0x33);
      expect(n.savedManualPalette[1].r, 0x44);
      expect(n.savedManualPalette[1].g, 0x55);
      expect(n.savedManualPalette[1].b, 0x66);
      // Draft is auto-seeded from the hydrated saved palette so the
      // editor opens populated and the save button stays clean.
      expect(n.draftManualPalette.length, 2);
      expect(n.manualPaletteDirty, isFalse);
    });

    test(
        'ignores a corrupt prefs payload — no throw, saved stays empty',
        () async {
      SharedPreferences.setMockInitialValues({
        'wisp.manualPalette.v1.$lampId': 'not-json',
      });

      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      // SharedPreferences.getInstance + getString chain through multiple
      // microtasks; pump the event queue until they settle.
      for (var i = 0; i < 10; i++) {
        await Future<void>.delayed(Duration.zero);
      }

      final n = c.read(wispNotifierProvider(lampId).notifier);
      expect(n.savedManualPalette, isEmpty);
    });

    test('per-lamp keying — palettes for different lamps don\'t collide',
        () async {
      // One lamp's saved palette must not leak into another's editor.
      SharedPreferences.setMockInitialValues({
        'wisp.manualPalette.v1.lamp-a': jsonEncode(['#AABBCC00']),
        'wisp.manualPalette.v1.lamp-b': jsonEncode(['#DDEEFF00']),
      });

      final ble = InMemoryBleClient();
      await ble.connect('lamp-a');
      await ble.connect('lamp-b');
      await ble.write(
        'lamp-a',
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List.fromList(utf8.encode('{}')),
      );
      await ble.write(
        'lamp-b',
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List.fromList(utf8.encode('{}')),
      );
      final c = makeContainer(ble: ble);
      // Hold listeners on both so neither autoDisposes between reads.
      final subA = c.listen(wispNotifierProvider('lamp-a'), (_, _) {});
      addTearDown(subA.close);
      final subB = c.listen(wispNotifierProvider('lamp-b'), (_, _) {});
      addTearDown(subB.close);

      await c.read(wispNotifierProvider('lamp-a').future);
      await c.read(wispNotifierProvider('lamp-b').future);
      // SharedPreferences.getInstance + getString chain through multiple
      // microtasks; pump the event queue until they settle.
      for (var i = 0; i < 10; i++) {
        await Future<void>.delayed(Duration.zero);
      }

      final a = c.read(wispNotifierProvider('lamp-a').notifier);
      final b = c.read(wispNotifierProvider('lamp-b').notifier);
      expect(a.savedManualPalette.single.r, 0xAA);
      expect(b.savedManualPalette.single.r, 0xDD);
    });
  });
}
