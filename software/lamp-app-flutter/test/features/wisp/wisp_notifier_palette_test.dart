import 'dart:convert';
import 'dart:typed_data';

import 'package:fake_async/fake_async.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Tests for notifier-level palette state.
///
/// Seed CHAR_WISP_STATUS before building the container; the notifier
/// reads it on build. Tests use `setMockInitialValues` so prefs
/// operations resolve in-memory instead of throwing MissingPluginException.
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

  // Seed the `wisppalette` page section the notifier reads on build: raw
  // interleaved R,G,B,W, one 4-byte group per color.
  void seedPalette(InMemoryBleClient ble, List<LampColor> colors) {
    final bytes = <int>[
      for (final c in colors) ...[c.r, c.g, c.b, c.w],
    ];
    ble.seedSection(lampId, 'wisppalette', Uint8List.fromList(bytes));
  }

  // Push the status NOTIFY the palette confirm awaits: source stays Manual
  // and paletteIdPrefix carries the content hash of [committed], standing in
  // for the wisp having stored and re-broadcast the exact palette.
  void confirmPalette(InMemoryBleClient ble, List<LampColor> committed,
      {String mac = 'AA:BB:CC:DD:EE:FF'}) {
    final prefix = WispRepository.paletteIdHash(committed);
    ble.simulateNotify(
      lampId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      Uint8List.fromList(utf8.encode(
          '{"wispMac":"$mac","source":"manual","paletteIdPrefix":"$prefix"}')),
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

    test('setManualPalette clears the dirty flag once the wisp echoes it',
        () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble,
          '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual"}');
      final c = makeContainer(ble: ble);

      c.listen(wispNotifierProvider(lampId), (_, _) {});
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      const committed = [
        LampColor(r: 1, g: 2, b: 3, w: 0),
        LampColor(r: 4, g: 5, b: 6, w: 0),
      ];
      n.appendManualPaletteColor(committed[0]);
      n.appendManualPaletteColor(committed[1]);
      expect(n.manualPaletteDirty, isTrue);

      // The wisp stamps the palette content hash and re-broadcasts it on the
      // status NOTIFY; the confirm matches that prefix.
      final f = n.setManualPalette();
      await Future<void>.delayed(Duration.zero);
      confirmPalette(ble, committed);
      await f;
      expect(n.manualPaletteDirty, isFalse);
      expect(n.manualPaletteWriteFailed, isFalse);
      expect(n.savedManualPalette.length, 2);
    });

    test('setManualPalette writes a wispOp with RGB triples', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble,
          '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual"}');
      final c = makeContainer(ble: ble);

      c.listen(wispNotifierProvider(lampId), (_, _) {});
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      const committed = [LampColor(r: 255, g: 128, b: 64, w: 0)];
      n.appendManualPaletteColor(committed[0]);
      final f = n.setManualPalette();
      await Future<void>.delayed(Duration.zero);
      confirmPalette(ble, committed);
      await f;

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

    test('unconfirmed palette write resends then surfaces failure + stays dirty',
        () async {
      final ble = InMemoryBleClient();
      // No status NOTIFY ever carries the committed palette hash, so no
      // confirm arrives and every attempt times out.
      await primeStatus(ble,
          '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual"}');
      final c = makeContainer(ble: ble);
      c.listen(wispNotifierProvider(lampId), (_, _) {});
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      n.appendManualPaletteColor(const LampColor(r: 9, g: 9, b: 9, w: 0));

      fakeAsync((fa) {
        Object? err;
        n.setManualPalette().catchError((Object e) => err = e);
        fa.elapse(WispNotifier.writeConfirmBudget);
        fa.flushMicrotasks();
        expect(err, isNotNull, reason: 'unconfirmed write surfaces error');
        expect(err.toString(), contains('not confirmed'));
      });

      expect(n.manualPaletteWriteFailed, isTrue);
      expect(n.manualPaletteDirty, isTrue,
          reason: 'saved snapshot never advanced past the unconfirmed draft');
      expect(ble.writesTo(lampId, BleUuids.wispOp).length, greaterThan(1),
          reason: 'op resent, not fire-and-forget once');
    });

    // A wisp holding [saved] in Manual, its status stamped with that palette's
    // prefix, and a notifier whose draft has been emptied.
    Future<(InMemoryBleClient, WispNotifier)> emptiedDraft(
        List<LampColor> saved) async {
      final ble = InMemoryBleClient();
      seedPalette(ble, saved);
      final prefix = WispRepository.paletteIdHash(saved);
      await primeStatus(ble,
          '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual","paletteIdPrefix":"$prefix"}');
      final c = makeContainer(ble: ble);
      c.listen(wispNotifierProvider(lampId), (_, _) {});
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);
      expect(n.savedManualPalette, saved);
      for (var i = saved.length - 1; i >= 0; i--) {
        n.removeManualPaletteColor(i);
      }
      expect(n.manualPaletteDirty, isTrue);
      return (ble, n);
    }

    test('saving an emptied palette writes it and confirms on a prefix-less '
        'Manual status', () async {
      final (ble, n) =
          await emptiedDraft(const [LampColor(r: 7, g: 8, b: 9, w: 0)]);

      final f = n.setManualPalette();
      await Future<void>.delayed(Duration.zero);
      // The wisp clears its palette id on an empty set, so its status omits
      // paletteIdPrefix.
      ble.simulateNotify(
        lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List.fromList(utf8
            .encode('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual"}')),
      );
      await f;

      final writes = ble.writesTo(lampId, BleUuids.wispOp);
      expect(writes, isNotEmpty);
      final decoded =
          jsonDecode(utf8.decode(writes.last)) as Map<String, dynamic>;
      expect(decoded['op'], 'setManualPalette');
      expect(decoded['colors'], isEmpty);
      expect(n.savedManualPalette, isEmpty);
      expect(n.manualPaletteDirty, isFalse);
      expect(n.manualPaletteWriteFailed, isFalse);
    });

    test('a status still carrying the old prefix does not confirm an emptied '
        'palette', () async {
      const saved = [LampColor(r: 7, g: 8, b: 9, w: 0)];
      final (ble, n) = await emptiedDraft(saved);

      var done = false;
      final f = n.setManualPalette().whenComplete(() => done = true);
      await Future<void>.delayed(Duration.zero);
      confirmPalette(ble, saved);
      await pumpEventQueue();
      expect(done, isFalse, reason: 'stale prefix is not a confirm');
      expect(n.manualPaletteDirty, isTrue);

      ble.simulateNotify(
        lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List.fromList(utf8
            .encode('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual"}')),
      );
      await f;
      expect(n.manualPaletteDirty, isFalse);
    });

    test('an emptied palette the wisp never confirms resends then surfaces '
        'failure + stays dirty', () async {
      final (ble, n) =
          await emptiedDraft(const [LampColor(r: 7, g: 8, b: 9, w: 0)]);

      fakeAsync((fa) {
        Object? err;
        n.setManualPalette().catchError((Object e) => err = e);
        fa.elapse(WispNotifier.writeConfirmBudget);
        fa.flushMicrotasks();
        expect(err, isNotNull, reason: 'unconfirmed empty save surfaces error');
      });

      expect(n.manualPaletteWriteFailed, isTrue);
      expect(n.manualPaletteDirty, isTrue);
      expect(ble.writesTo(lampId, BleUuids.wispOp).length, greaterThan(1),
          reason: 'op resent, not fire-and-forget once');
    });

    const ours = 'AA:BB:CC:DD:EE:FF';
    const rival = '11:22:33:44:55:66';
    const saved = [
      LampColor(r: 7, g: 8, b: 9, w: 0),
      LampColor(r: 120, g: 0, b: 64, w: 30),
    ];
    String status(String mac, {int? stamp, String prefix = ''}) =>
        '{"wispMac":"$mac","source":"manual"'
        '${stamp == null ? '' : ',"lastSeenMs":$stamp'}'
        '${prefix.isEmpty ? '' : ',"paletteIdPrefix":"$prefix"'}}';
    void notify(InMemoryBleClient ble, String json) => ble.simulateNotify(
        lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List.fromList(utf8.encode(json)));

    // The app first reads [heard].first, then takes the rest as notifies,
    // while the lamp's palette section holds [saved]. The draft is then
    // emptied.
    Future<(InMemoryBleClient, ProviderContainer, WispNotifier)>
        emptiedAfterHearing(List<String> heard) async {
      final ble = InMemoryBleClient();
      seedPalette(ble, saved);
      await primeStatus(ble, heard.first);
      final c = makeContainer(ble: ble);
      c.listen(wispNotifierProvider(lampId), (_, _) {});
      await c.read(wispNotifierProvider(lampId).future);
      for (final json in heard.skip(1)) {
        notify(ble, json);
        await pumpEventQueue();
      }
      final n = c.read(wispNotifierProvider(lampId).notifier);
      expect(n.savedManualPalette, saved);
      n.setManualPaletteDraft(const []);
      return (ble, c, n);
    }

    final savedPrefix = WispRepository.paletteIdHash(saved);
    for (final (label, heard, stale, newer) in [
      (
        'a re-sent copy of the status from before the save',
        [status(ours, stamp: 73500)],
        73500,
        73501,
      ),
      (
        'a status stamped before the save, as after a wisp reboot',
        [status(ours, stamp: 73500)],
        2100,
        73501,
      ),
      (
        'a status without a stamp',
        [status(ours, stamp: 73500)],
        null,
        73501,
      ),
      (
        'a status older than the newest heard, after an older copy arrived '
            'last',
        [
          status(ours, stamp: 73500),
          status(ours, stamp: 90000, prefix: savedPrefix),
          status(ours, stamp: 73500),
        ],
        80000,
        90001,
      ),
      (
        'a re-sent copy after the lamp switched from another wisp',
        [status(rival, stamp: 900000), status(ours, stamp: 1000)],
        1000,
        1001,
      ),
      (
        'an older copy after the lamp followed another wisp in between',
        [
          status(ours, stamp: 80000),
          status(rival, stamp: 900000),
          status(ours, stamp: 1000),
        ],
        73500,
        80001,
      ),
      (
        'a status older than one heard before another wisp and an unstamped '
            'status',
        [
          status(ours, stamp: 80000),
          status(rival, stamp: 900000),
          status(ours),
        ],
        1000,
        80001,
      ),
    ]) {
      test('$label does not confirm an emptied palette', () async {
        final (ble, c, n) = await emptiedAfterHearing(heard);

        var done = false;
        final f = n.setManualPalette().whenComplete(() => done = true);
        await Future<void>.delayed(Duration.zero);
        notify(ble, status(ours, stamp: stale));
        await pumpEventQueue();
        expect(c.read(wispNotifierProvider(lampId)).value!.lastSeenMs, stale,
            reason: 'the notifier took the status');
        expect(done, isFalse);
        expect(n.savedManualPalette, saved);
        expect(n.manualPaletteDirty, isTrue);

        notify(ble, status(ours, stamp: newer));
        await f;
        expect(n.savedManualPalette, isEmpty);
      });
    }

    for (final (label, heard, newer) in [
      ('a prefix-less status stamped after the save',
          [status(ours, stamp: 73500)], 73501),
      ('a stamped status after an unstamped one from a new wisp',
          [status(rival, stamp: 900000), status(ours)], 1001),
    ]) {
      test('$label confirms an emptied palette', () async {
        final (ble, _, n) = await emptiedAfterHearing(heard);

        final f = n.setManualPalette();
        await Future<void>.delayed(Duration.zero);
        notify(ble, status(ours, stamp: newer));
        await f;

        expect(n.savedManualPalette, isEmpty);
        expect(n.manualPaletteDirty, isFalse);
        expect(n.manualPaletteWriteFailed, isFalse);
      });
    }

    test('after an emptied palette lands, the old palette coming back is '
        're-read', () async {
      const palette = [LampColor(r: 7, g: 8, b: 9, w: 0)];
      final (ble, n) = await emptiedDraft(palette);
      final f = n.setManualPalette();
      await Future<void>.delayed(Duration.zero);
      ble.simulateNotify(
        lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List.fromList(utf8
            .encode('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual"}')),
      );
      await f;
      expect(n.savedManualPalette, isEmpty);

      // Another client saves the same palette back onto the wisp.
      seedPalette(ble, palette);
      confirmPalette(ble, palette);
      await pumpEventQueue();
      expect(n.savedManualPalette, palette);
      expect(n.draftManualPalette, palette);
    });

    test('source leaving Manual mid-confirm surfaces a source-changed error',
        () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble,
          '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual"}');
      final c = makeContainer(ble: ble);
      c.listen(wispNotifierProvider(lampId), (_, _) {});
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      n.appendManualPaletteColor(const LampColor(r: 1, g: 2, b: 3, w: 0));
      Object? err;
      final f = n.setManualPalette().catchError((Object e) => err = e);
      await Future<void>.delayed(Duration.zero);
      // The wisp reports it left Manual before the palette prefix could
      // confirm; the write is moot, not a delivery failure.
      ble.simulateNotify(
        lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List.fromList(
            utf8.encode('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"off"}')),
      );
      await f;
      expect(err, isNotNull);
      expect(err.toString(), contains('source changed'));
    });

    test('editing then disposing writes nothing (local preview only)',
        () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      n.appendManualPaletteColor(const LampColor(r: 7, g: 7, b: 7, w: 0));
      final before = ble.writesTo(lampId, BleUuids.wispOp).length;
      c.invalidate(wispNotifierProvider(lampId));
      await Future<void>.delayed(Duration.zero);

      expect(ble.writesTo(lampId, BleUuids.wispOp).length, before,
          reason: 'edits never write; only an explicit Save reaches the wisp');
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

    test('re-emitting the same draft palette schedules no write', () async {
      final ble = InMemoryBleClient();
      seedPalette(ble, const [
        LampColor(r: 1, g: 2, b: 3, w: 0),
        LampColor(r: 4, g: 5, b: 6, w: 0),
      ]);
      await primeStatus(ble,
          '{"wispMac":"AA:BB:CC:DD:EE:FF","paletteIdPrefix":"seed0001"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);
      final current = n.draftManualPalette;
      final before = ble.writesTo(lampId, BleUuids.wispOp).length;

      fakeAsync((fa) {
        // The color-stops editor re-streams its current value on every
        // rebuild the notifier triggers; an idempotent re-emit like this
        // must not re-arm the debounced BLE write.
        n.setManualPaletteDraft(List.of(current));
        fa.elapse(const Duration(milliseconds: 300));
        fa.flushMicrotasks();
      });

      expect(ble.writesTo(lampId, BleUuids.wispOp).length, before);
    });

    test('a genuinely different draft palette still writes nothing (Save only)',
        () async {
      final ble = InMemoryBleClient();
      seedPalette(ble, const [LampColor(r: 1, g: 2, b: 3, w: 0)]);
      await primeStatus(ble,
          '{"wispMac":"AA:BB:CC:DD:EE:FF","paletteIdPrefix":"seed0002"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);
      final before = ble.writesTo(lampId, BleUuids.wispOp).length;

      fakeAsync((fa) {
        n.setManualPaletteDraft(const [
          LampColor(r: 1, g: 2, b: 3, w: 0),
          LampColor(r: 9, g: 9, b: 9, w: 0),
        ]);
        fa.elapse(const Duration(milliseconds: 300));
        fa.flushMicrotasks();
      });

      expect(ble.writesTo(lampId, BleUuids.wispOp).length, before,
          reason: 'draft edits are local preview; the wisp write waits for Save');
    });
  });

  group('WispNotifier setSource', () {
    // setSource confirms by watching for a wispStatus echo carrying the
    // requested mode; push one so the call resolves.
    void confirmSource(InMemoryBleClient ble, String mode) {
      ble.simulateNotify(
        lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List.fromList(utf8.encode(
            '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"$mode"}')),
      );
    }

    test('writes a setSource wispOp with the wire-format mode string',
        () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);

      c.listen(wispNotifierProvider(lampId), (_, _) {});
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      final f = n.setSource(WispSourceMode.manual);
      await Future<void>.delayed(Duration.zero);
      confirmSource(ble, 'manual');
      await f;
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
          ble, '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"osc"}');
      final c = makeContainer(ble: ble);

      c.listen(wispNotifierProvider(lampId), (_, _) {});
      final initial =
          await c.read(wispNotifierProvider(lampId).future);
      expect(initial.source, WispSourceMode.osc);

      final n = c.read(wispNotifierProvider(lampId).notifier);
      final f = n.setSource(WispSourceMode.off);
      // The optimistic update lands before any confirmation.
      expect(c.read(wispNotifierProvider(lampId)).value!.source,
          WispSourceMode.off);

      await Future<void>.delayed(Duration.zero);
      confirmSource(ble, 'off');
      await f;
      expect(c.read(wispNotifierProvider(lampId)).value!.source,
          WispSourceMode.off);
    });

    test('resends then surfaces failure when the wisp never confirms',
        () async {
      final ble = InMemoryBleClient();
      await primeStatus(
          ble, '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"osc"}');
      final c = makeContainer(ble: ble);
      c.listen(wispNotifierProvider(lampId), (_, _) {});
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      fakeAsync((fa) {
        Object? err;
        // No confirming echo; every attempt times out.
        n.setSource(WispSourceMode.off).catchError((Object e) => err = e);
        fa.elapse(WispNotifier.writeConfirmBudget);
        fa.flushMicrotasks();
        expect(err, isNotNull, reason: 'unconfirmed setSource surfaces error');
      });

      // Optimistic flip rolled back to ground truth; op resent, not once.
      expect(c.read(wispNotifierProvider(lampId)).value!.source,
          WispSourceMode.osc);
      expect(ble.writesTo(lampId, BleUuids.wispOp).length, greaterThan(1));
    });

    test('drops a stale source echo WHOLE, preserving the palette',
        () async {
      // The palette rides its own section read; the stale echo is dropped
      // whole by the source guard, so the seeded palette survives regardless.
      final ble = InMemoryBleClient();
      seedPalette(ble, const [LampColor(r: 10, g: 11, b: 12, w: 0)]);
      await primeStatus(
          ble,
          '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual",'
          '"paletteIdPrefix":"seed0001"}');
      final c = makeContainer(ble: ble);

      c.listen(wispNotifierProvider(lampId), (_, _) {});
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);
      expect(n.savedManualPalette.single.r, 10);

      final f = n.setSource(WispSourceMode.off);
      await Future<void>.delayed(Duration.zero);

      // Relay re-broadcasts the cached pre-change status still on manual.
      ble.simulateNotify(
        lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List.fromList(utf8.encode(
            '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual",'
            '"paletteIdPrefix":"seed0001"}')),
      );
      await Future<void>.delayed(Duration.zero);

      expect(c.read(wispNotifierProvider(lampId)).value!.source,
          WispSourceMode.off,
          reason: 'stale manual echo dropped, not adopted');
      expect(n.savedManualPalette.single.r, 10,
          reason: 'stale echo dropped whole, seeded palette preserved');

      // Confirm so the pending setSource resolves cleanly.
      ble.simulateNotify(
        lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List.fromList(utf8.encode(
            '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"off"}')),
      );
      await f;
    });

    test('releases the guard when the wisp confirms the requested mode',
        () async {
      final ble = InMemoryBleClient();
      await primeStatus(
          ble, '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual"}');
      final c = makeContainer(ble: ble);

      c.listen(wispNotifierProvider(lampId), (_, _) {});
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      final f = n.setSource(WispSourceMode.off);
      await Future<void>.delayed(Duration.zero);
      ble.simulateNotify(
        lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List.fromList(
            utf8.encode('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"off"}')),
      );
      await f;

      // After confirmation a genuine later manual switch flows through.
      ble.simulateNotify(
        lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List.fromList(
            utf8.encode('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual"}')),
      );
      await Future<void>.delayed(Duration.zero);

      expect(c.read(wispNotifierProvider(lampId)).value!.source,
          WispSourceMode.manual,
          reason: 'guard released on confirm, later status not suppressed');
    });
  });

  group('WispNotifier palette from section', () {
    test('section read seeds the editor from the wisp palette', () async {
      final ble = InMemoryBleClient();
      seedPalette(ble, const [
        LampColor(r: 255, g: 0, b: 0, w: 0),
        LampColor(r: 0, g: 128, b: 64, w: 0),
      ]);
      await primeStatus(ble,
          '{"wispMac":"AA:BB:CC:DD:EE:FF","paletteIdPrefix":"abc12345"}');
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

    test('preserves W when the section carries an RGBW palette', () async {
      final ble = InMemoryBleClient();
      seedPalette(ble, const [LampColor(r: 199, g: 0, b: 16, w: 80)]);
      await primeStatus(ble,
          '{"wispMac":"AA:BB:CC:DD:EE:FF","paletteIdPrefix":"abc12345"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      expect(n.savedManualPalette.single.w, 80);
    });

    test('present wisp, empty section → not loading, empty editor (no hang)',
        () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble: ble);

      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      // A completed read marks the palette known even when empty (legacy lamp
      // or empty palette), so the editor renders a populatable palette instead
      // of hanging on "reading from wisp" forever.
      expect(n.paletteLoading, isFalse);
      expect(n.savedManualPalette, isEmpty);
      expect(n.draftManualPalette, isEmpty);
    });

    test('no SharedPreferences key is written for the palette', () async {
      final ble = InMemoryBleClient();
      seedPalette(ble, const [LampColor(r: 1, g: 2, b: 3, w: 0)]);
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
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
