import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/control/presentation/widgets/lamp_color_swatch.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/social/application/lamp_nearby_peers_notifier.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';
import 'package:lamp_app/features/wisp/domain/palette_gradient.dart';
import 'package:lamp_app/features/wisp/domain/tuple_sampler.dart';
import 'package:lamp_app/features/wisp/domain/wisp_config.dart';
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';
import 'package:lamp_app/features/wisp/domain/wisp_status.dart';
import 'package:lamp_app/features/wisp/presentation/palette_gradient_bar.dart';
import 'package:lamp_app/features/wisp/presentation/widgets/wisp_painted_lamps.dart';
import 'package:lamp_app/features/wisp/presentation/wisp_config_screen.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';

class _FakePeers extends LampNearbyPeersNotifier {
  @override
  Future<List<LampNearbyPeer>> build(String lampId) async => const [];
}

const _devId = 'lamp-palette-style';
const _mac = 'AA:BB:CC:DD:EE:FF';
const _red = LampColor(r: 255, g: 0, b: 0, w: 0);
const _blue = LampColor(r: 0, g: 0, b: 255, w: 0);
const _claimed = '10:20:30:40:50:60';

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

String _status({String source = 'manual', int seen = 100}) => jsonEncode({
  'wispMac': _mac,
  'configSplit': true,
  'source': source,
  'manualId': WispRepository.statusManualId(const [_red, _blue]),
  'configLastSeenMs': seen,
});

/// A `wispConfig` with [swatches] as given; null leaves the key out, as a
/// wisp that predates it sends.
String _config({bool? swatches}) => jsonEncode({
  'char': 'wispConfig',
  'ledType': 'GRB',
  'px': 30,
  'brightness': 100,
  'driftIntervalMs': 120000,
  'driftFadePct': 50,
  'shuffleSeed': 0,
  'opSeq': 0,
  'ip': '',
  'swatches': ?swatches,
});

List<bool> _swatchOps(InMemoryBleClient ble) => [
  for (final w in ble.writesTo(_devId, BleUuids.wispOp))
    if (jsonDecode(utf8.decode(w)) case {
      'op': 'setSwatches',
      'swatches': final bool v,
    })
      v,
];

void _notify(InMemoryBleClient ble, String json) => ble.simulateNotify(
  _devId,
  BleUuids.controlService,
  BleUuids.wispStatus,
  _b(json),
);

Future<void> _wait(Duration d) => Future<void>.delayed(d);

/// Holds `wispconfig` reads while [holdConfig] is set.
class _HoldConfigBle extends InMemoryBleClient {
  Completer<void>? holdConfig;

  @override
  Future<Uint8List> readSection(String deviceId, String name) async {
    if (name == 'wispconfig') await holdConfig?.future;
    return super.readSection(deviceId, name);
  }
}

/// Fails every `setSource` and `setOscRole` write, the first held open while
/// [holdSource] is set.
class _FailSourceBle extends InMemoryBleClient {
  Completer<void>? holdSource;

  @override
  Future<void> write(
    String d,
    String s,
    String c,
    Uint8List v, {
    bool withoutResponse = false,
    bool allowLongWrite = false,
  }) async {
    final op = utf8.decode(v, allowMalformed: true);
    if (op.contains('setSource') || op.contains('setOscRole')) {
      final hold = holdSource;
      holdSource = null;
      await hold?.future;
      throw Exception('source write failed');
    }
    return super.write(
      d,
      s,
      c,
      v,
      withoutResponse: withoutResponse,
      allowLongWrite: allowLongWrite,
    );
  }
}

void main() {
  group('setPaletteStyle', () {
    final defaultWindow = WispNotifier.debugConfigConfirmWindow;
    final defaultReadBack = WispNotifier.debugConfigReadBackDelay;
    setUp(() {
      SharedPreferences.setMockInitialValues({});
      WispNotifier.debugConfigConfirmWindow = const Duration(milliseconds: 300);
      WispNotifier.debugConfigReadBackDelay = const Duration(milliseconds: 40);
    });
    tearDown(() {
      WispNotifier.debugConfigConfirmWindow = defaultWindow;
      WispNotifier.debugConfigReadBackDelay = defaultReadBack;
    });

    Future<(InMemoryBleClient, ProviderContainer, WispNotifier)> open({
      InMemoryBleClient? client,
    }) async {
      final ble = client ?? InMemoryBleClient();
      await ble.connect(_devId);
      ble.seedSection(_devId, 'wispstatus', _b(_status()));
      ble.seedSection(_devId, 'wispconfig', _b(_config(swatches: false)));
      final c = ProviderContainer(
        overrides: [bleClientProvider.overrideWithValue(ble)],
      );
      addTearDown(c.dispose);
      c.listen(wispNotifierProvider(_devId), (_, _) {});
      await c.read(wispNotifierProvider(_devId).future);
      await pumpEventQueue();
      return (ble, c, c.read(wispNotifierProvider(_devId).notifier));
    }

    PaletteStyle? shown(ProviderContainer c) =>
        c.read(wispNotifierProvider(_devId)).value!.paletteStyle;

    test('shows at once and confirms on the wisp config', () async {
      final (ble, c, n) = await open();
      expect(shown(c), PaletteStyle.gradient);

      final done = n.setPaletteStyle(PaletteStyle.swatches);
      await pumpEventQueue();
      expect(shown(c), PaletteStyle.swatches);
      expect(_swatchOps(ble), [true]);

      ble.seedSection(_devId, 'wispconfig', _b(_config(swatches: true)));
      _notify(ble, _status(seen: 200));
      await done;

      expect(shown(c), PaletteStyle.swatches);
      expect(_swatchOps(ble), [true], reason: 'confirmed, no resend');
    });

    test(
      'a config read from before the wisp applied it holds the switch',
      () async {
        final (ble, c, n) = await open();

        final done = n.setPaletteStyle(PaletteStyle.swatches);
        await pumpEventQueue();
        _notify(ble, _status(seen: 150));
        await pumpEventQueue();
        expect(
          shown(c),
          PaletteStyle.swatches,
          reason: 'stale config held off',
        );

        ble.seedSection(_devId, 'wispconfig', _b(_config(swatches: true)));
        _notify(ble, _status(seen: 200));
        await done;
        expect(shown(c), PaletteStyle.swatches);

        // Once confirmed, the wisp's config rules again.
        ble.seedSection(_devId, 'wispconfig', _b(_config(swatches: false)));
        _notify(ble, _status(seen: 300));
        await pumpEventQueue();
        expect(shown(c), PaletteStyle.gradient);
      },
    );

    test('rolls back and throws when the wisp never confirms', () async {
      final (ble, c, n) = await open();

      Object? error;
      await n
          .setPaletteStyle(PaletteStyle.swatches)
          .catchError((Object e) => error = e);

      expect(error, isNotNull);
      expect(shown(c), PaletteStyle.gradient);
      expect(_swatchOps(ble), [
        true,
        true,
        true,
        true,
      ], reason: 'resent each attempt');
    });

    test('a newer pick stops the older one resending', () async {
      final (ble, c, n) = await open();

      final first = n.setPaletteStyle(PaletteStyle.swatches);
      await pumpEventQueue();
      final second = n.setPaletteStyle(PaletteStyle.gradient);
      await Future.wait([first, second]);
      await _wait(WispNotifier.debugConfigConfirmWindow);

      expect(shown(c), PaletteStyle.gradient);
      expect(_swatchOps(ble).where((v) => v), hasLength(1));
    });

    // Swatches may still land at the wisp after the pick back to Gradient, so
    // a read of the config the lamp held before that pick proves nothing.
    test('a pick back is not confirmed by a config cached before it', () async {
      final (ble, c, n) = await open();

      final first = n.setPaletteStyle(PaletteStyle.swatches);
      await pumpEventQueue();
      final second = n.setPaletteStyle(PaletteStyle.gradient);
      await Future.wait([first, second]);

      expect(_swatchOps(ble).where((v) => !v), hasLength(4),
          reason: 'Gradient resent for the whole budget');
      expect(shown(c), PaletteStyle.gradient);
    });

    // Another setting's write moves the config stamp while Swatches may
    // still land.
    test('a pick back is not confirmed by another setting changing', () async {
      final (ble, c, n) = await open();

      final first = n.setPaletteStyle(PaletteStyle.swatches);
      await pumpEventQueue();
      final second = n.setPaletteStyle(PaletteStyle.gradient);
      await pumpEventQueue();
      ble.seedSection(
        _devId,
        'wispconfig',
        _b(_config(swatches: false).replaceFirst('"brightness":100',
            '"brightness":40')),
      );
      _notify(ble, _status(seen: 400));
      await Future.wait([first, second]);

      expect(c.read(wispNotifierProvider(_devId)).value!.brightness, 40,
          reason: 'the other change was read');
      expect(_swatchOps(ble).where((v) => !v), hasLength(4));
      expect(shown(c), PaletteStyle.gradient);
    });

    test('a failed role change keeps a pick still confirming', () async {
      final ble = _FailSourceBle();
      final (_, c, n) = await open(client: ble);

      final hold = ble.holdSource = Completer<void>();
      Object? roleError;
      final role = n
          .setOscRole(OscRole.fill)
          .catchError((Object e) => roleError = e);
      await pumpEventQueue();
      final style = n
          .setPaletteStyle(PaletteStyle.swatches)
          .catchError((Object _) {});
      await pumpEventQueue();
      hold.complete();
      await role;

      expect(roleError, isNotNull);
      final now = c.read(wispNotifierProvider(_devId)).value!;
      expect(now.oscRole, OscRole.primary);
      expect(now.paletteStyle, PaletteStyle.swatches);
      await style;
    });

    // The lamp already holds a Swatches config from before this pick that the
    // app has not read; an earlier pick may still land over it.
    test('a pick is not confirmed by a config cached before it', () async {
      final ble = _HoldConfigBle();
      final (_, c, n) = await open(client: ble);

      final hold = ble.holdConfig = Completer<void>();
      ble.seedSection(_devId, 'wispconfig', _b(_config(swatches: true)));
      _notify(ble, _status(seen: 200));
      await pumpEventQueue();
      expect(shown(c), PaletteStyle.gradient, reason: 'its read is held');

      final done = n.setPaletteStyle(PaletteStyle.swatches);
      await pumpEventQueue();
      hold.complete();
      await done;

      expect(_swatchOps(ble), [true, true, true, true]);
      expect(shown(c), PaletteStyle.swatches);
    });

    String otherWisp(int seen) => jsonEncode({
          'wispMac': '11:22:33:44:55:66',
          'configSplit': true,
          'source': 'manual',
          'configLastSeenMs': seen,
        });

    test('a status from another wisp carries no pending pick', () async {
      final (ble, c, n) = await open();

      final done = n
          .setPaletteStyle(PaletteStyle.swatches)
          .catchError((Object _) {});
      await pumpEventQueue();
      // A wisp that predates swatches.
      ble.seedSection(_devId, 'wispconfig', _b(_config()));
      _notify(ble, otherWisp(500));
      await pumpEventQueue();

      final now = c.read(wispNotifierProvider(_devId)).value!;
      expect(now.wispMac, '11:22:33:44:55:66');
      expect(now.paletteStyle, isNull);
      await done;
    });

    test("another wisp's config never confirms a pick", () async {
      final (ble, _, n) = await open();

      Object? error;
      final done = n
          .setPaletteStyle(PaletteStyle.swatches)
          .catchError((Object e) => error = e);
      await pumpEventQueue();
      ble.seedSection(_devId, 'wispconfig', _b(_config(swatches: true)));
      _notify(ble, otherWisp(500));
      await done;

      expect(error, isNotNull);
      expect(_swatchOps(ble), [true], reason: 'no resend to the other wisp');
    });

    test('a failed source change keeps a pick still confirming', () async {
      final ble = _FailSourceBle();
      final (_, c, n) = await open(client: ble);

      final hold = ble.holdSource = Completer<void>();
      Object? sourceError;
      final source = n
          .setSource(WispSourceMode.osc)
          .catchError((Object e) => sourceError = e);
      await pumpEventQueue();
      final style = n
          .setPaletteStyle(PaletteStyle.swatches)
          .catchError((Object _) {});
      await pumpEventQueue();
      hold.complete();
      await source;

      expect(sourceError, isNotNull);
      final now = c.read(wispNotifierProvider(_devId)).value!;
      expect(now.source, WispSourceMode.manual);
      expect(now.paletteStyle, PaletteStyle.swatches);
      await style;
    });
  });

  group('palette style on screen', () {
    Future<(InMemoryBleClient, ProviderContainer)> container({
      required String status,
      required String config,
    }) async {
      SharedPreferences.setMockInitialValues({});
      final ble = InMemoryBleClient();
      await seedControlBle(ble, deviceId: _devId);
      await ble.connect(_devId);
      ble.seedSection(_devId, 'wispstatus', _b(status));
      ble.seedSection(_devId, 'wispconfig', _b(config));
      ble.seedSection(
        _devId,
        'wisppalette',
        Uint8List.fromList([255, 0, 0, 0, 0, 0, 255, 0]),
      );
      await ble.write(
        _devId,
        BleUuids.controlService,
        BleUuids.wispClaims,
        Uint8List.fromList([1, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60]),
      );
      final c = ProviderContainer(
        overrides: [
          bleClientProvider.overrideWithValue(ble),
          lampNearbyPeersNotifierProvider(
            _devId,
          ).overrideWith(() => _FakePeers()),
        ],
      );
      await c.read(inventoryNotifierProvider.future);
      await c
          .read(inventoryNotifierProvider.notifier)
          .add(const InventoryLamp(id: _devId, name: 'testlamp'));
      return (ble, c);
    }

    Future<void> pump(
      WidgetTester tester,
      ProviderContainer c,
      Widget w,
    ) async {
      tester.view.physicalSize = const Size(800, 2400);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      await tester.runAsync(() async {
        await c.read(wispNotifierProvider(_devId).future);
      });
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: c,
          child: MaterialApp(home: Scaffold(body: w)),
        ),
      );
      for (var i = 0; i < 20; i++) {
        await tester.pump(const Duration(milliseconds: 16));
      }
    }

    const screen = WispConfigScreen(lampId: _devId);

    for (final source in ['manual', 'osc']) {
      testWidgets('the switch shows under $source when the config has it', (
        tester,
      ) async {
        final (_, c) = await container(
          status: _status(source: source),
          config: _config(swatches: true),
        );
        addTearDown(c.dispose);
        await pump(tester, c, screen);

        expect(find.byKey(const Key('wisp-palette-style')), findsOneWidget);
        expect(find.text('Gradient'), findsOneWidget);
        expect(find.text('Swatches'), findsOneWidget);
      });
    }

    testWidgets('no switch for a wisp whose config lacks swatches', (
      tester,
    ) async {
      final (_, c) = await container(status: _status(), config: _config());
      addTearDown(c.dispose);
      await pump(tester, c, screen);

      expect(c.read(wispNotifierProvider(_devId)).value!.configKnown, isTrue);
      expect(find.byKey(const Key('wisp-palette-style')), findsNothing);
    });

    testWidgets('no switch in Off', (tester) async {
      final (_, c) = await container(
        status: _status(source: 'off'),
        config: _config(swatches: false),
      );
      addTearDown(c.dispose);
      await pump(tester, c, screen);

      expect(find.byKey(const Key('wisp-palette-style')), findsNothing);
    });

    testWidgets(
      'tapping Swatches sends setSwatches, and says so when it fails',
      (tester) async {
        final (ble, c) = await container(
          status: _status(),
          config: _config(swatches: false),
        );
        addTearDown(c.dispose);
        await pump(tester, c, screen);

        await tester.tap(find.text('Swatches'));
        await tester.pump();
        await tester.runAsync(() => pumpEventQueue());
        expect(_swatchOps(ble), [true]);
        // Run out the unconfirmed resends and the rollback's snackbar.
        final failed = find.text("Couldn't reach the wisp. Try again.");
        for (var i = 0; i < 60 && failed.evaluate().isEmpty; i++) {
          await tester.pump(const Duration(seconds: 1));
          await tester.runAsync(() => pumpEventQueue());
        }
        expect(failed, findsOneWidget);
        expect(_swatchOps(ble), [true, true, true, true]);
        await tester.pump(const Duration(seconds: 10));
      },
    );

    testWidgets('the preview predicts swatch colours when the config says so', (
      tester,
    ) async {
      final (_, c) = await container(
        status: _status(),
        config: _config(swatches: true),
      );
      addTearDown(c.dispose);
      await pump(tester, c, const PaintedLampsList(lampId: _devId));

      final shown = [
        for (final w in tester.widgetList<LampColorSwatch>(
          find.byType(LampColorSwatch),
        ))
          w.color,
      ];
      final want = predictTuple(
        mac: parseMacFromBleId(_claimed)!,
        palette: const [_red, _blue],
        style: PaletteStyle.swatches,
      )!;
      expect(shown, [want.base, want.shade]);
      expect(shown.toSet(), {_red, _blue});
      // The fixture tells the styles apart: gradient gives a blend here.
      final gradient = predictTuple(
        mac: parseMacFromBleId(_claimed)!,
        palette: const [_red, _blue],
      )!;
      expect([gradient.base, gradient.shade], isNot(shown));
    });

    testWidgets('the palette bar draws blocks in swatches', (tester) async {
      List<Color> ramp(PaletteStyle style) {
        final paint = tester.widget<CustomPaint>(
          find.descendant(
            of: find.byType(PaletteGradientBar),
            matching: find.byType(CustomPaint),
          ),
        );
        return (paint.painter as dynamic).ramp as List<Color>;
      }

      Future<void> bar(PaletteStyle style) => tester.pumpWidget(
        MaterialApp(
          home: PaletteGradientBar(
            sourceMode: WispSourceMode.manual,
            manualPalette: const [_red, _blue],
            offColor: _red,
            style: style,
          ),
        ),
      );

      await bar(PaletteStyle.swatches);
      final blocks = ramp(PaletteStyle.swatches);
      final stops = [
        foldRgbwWarmBias(255, 0, 0, 0),
        foldRgbwWarmBias(0, 0, 255, 0),
      ];
      expect(blocks.toSet(), stops.toSet());
      expect(blocks, renderPaletteBlocks(stops, blocks.length));

      await bar(PaletteStyle.gradient);
      expect(ramp(PaletteStyle.gradient).toSet().length, greaterThan(2));
    });
  });

  group('palette style in the model', () {
    test('wispConfig swatches parses, null when absent', () {
      expect(
        WispConfig.fromBytes(_b(_config(swatches: true)))!.paletteStyle,
        PaletteStyle.swatches,
      );
      expect(
        WispConfig.fromBytes(_b(_config(swatches: false)))!.paletteStyle,
        PaletteStyle.gradient,
      );
      expect(WispConfig.fromBytes(_b(_config()))!.paletteStyle, isNull);
    });

    test('a status merges it, compares on it, and copyWith keeps it', () {
      const base = WispStatus(wispMac: _mac, configSplit: true);
      final sw = base.withConfig(
        WispConfig.fromBytes(_b(_config(swatches: true)))!,
      );
      final gr = base.withConfig(
        WispConfig.fromBytes(_b(_config(swatches: false)))!,
      );
      expect(sw.paletteStyle, PaletteStyle.swatches);
      expect(sw == gr, isFalse);
      expect(sw.copyWith(brightness: 10).paletteStyle, PaletteStyle.swatches);
      expect(
        sw.withConfig(WispConfig.fromBytes(_b(_config()))!).paletteStyle,
        isNull,
      );
    });

    test('a held config keeps it through a brightness drag', () {
      final config = WispConfig.fromBytes(_b(_config(swatches: true)))!;
      expect(
        config.copyWith(brightness: 10).paletteStyle,
        PaletteStyle.swatches,
      );
    });

    test('blocks: each pixel one stop in order, as the wisp ring', () {
      const stops = [
        Color(0xFFFF0000),
        Color(0xFF00FF00),
        Color(0xFF0000FF),
        Color(0xFF090909),
      ];
      final out = renderPaletteBlocks(stops, 30);
      final perStop = [0, 0, 0, 0];
      for (var i = 0; i < 30; i++) {
        final k = (i * 4) ~/ 30;
        expect(out[i], stops[k]);
        perStop[k]++;
      }
      expect(perStop, [8, 7, 8, 7]);
      expect(renderPaletteBlocks(const [], 5), List.filled(5, kWarmWhite));
    });
  });
}
