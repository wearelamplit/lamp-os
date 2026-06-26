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

  group('WispNotifier currentPalette from read', () {
    String paletteJson(List<List<int>> rgb, {String mac = 'AA:BB:CC:DD:EE:FF',
        String prefix = 'abc12345'}) {
      final bytes = [for (final c in rgb) ...c];
      final b64 = base64Encode(bytes);
      return '{"wispMac":"$mac","paletteIdPrefix":"$prefix","palette":"$b64"}';
    }

    test('read seeds the editor from the wisp palette (source of truth)',
        () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, paletteJson([[255, 0, 0], [0, 128, 64]]));
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      expect(n.savedManualPalette.length, 2);
      expect(n.savedManualPalette[0].r, 255);
      expect(n.savedManualPalette[1].g, 128);
      expect(n.draftManualPalette.length, 2);
      expect(n.manualPaletteDirty, isFalse);
      expect(n.paletteLoading, isFalse);
    });

    test('present wisp with no palette yet → paletteLoading, no swatches',
        () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      expect(n.paletteLoading, isTrue);
      expect(n.savedManualPalette, isEmpty);
      expect(n.draftManualPalette, isEmpty);
    });

    test('no SharedPreferences key is written for the palette', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, paletteJson([[1, 2, 3]]));
      final c = makeContainer(ble: ble);
      await c.read(wispNotifierProvider(lampId).future);
      for (var i = 0; i < 10; i++) {
        await Future<void>.delayed(Duration.zero);
      }
      final prefs = await SharedPreferences.getInstance();
      expect(prefs.getString('wisp.manualPalette.v1.$lampId'), isNull);
    });
  });
}
