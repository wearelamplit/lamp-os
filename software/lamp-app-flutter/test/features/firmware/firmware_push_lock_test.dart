import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:go_router/go_router.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/ble_scanner.dart';
import 'package:lamp_app/core/ble/reaching_lamp_gate.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/core/routing/router.dart';
import 'package:lamp_app/core/routing/routes.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/application/control_state.dart';
import 'package:lamp_app/features/control/application/dev_mode.dart';
import 'package:lamp_app/features/control/presentation/widgets/brightness_card.dart';
import 'package:lamp_app/features/control/presentation/widgets/color_blocks_bar.dart';
import 'package:lamp_app/features/control/presentation/widgets/color_stops_sheet.dart';
import 'package:lamp_app/features/firmware/application/cached_firmware_notifier.dart';
import 'package:lamp_app/features/firmware/application/firmware_notifier.dart';
import 'package:lamp_app/features/firmware/data/cached_firmware.dart';
import 'package:lamp_app/features/firmware/domain/firmware_state.dart';
import 'package:lamp_app/features/firmware/domain/lsig_footer.dart';
import 'package:lamp_app/features/firmware/presentation/firmware_push_lock.dart';
import 'package:lamp_app/features/firmware/presentation/firmware_push_outcome.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/lamp_shell/presentation/expression_editor_screen.dart';
import 'package:lamp_app/features/lamp_shell/presentation/info_screen.dart';
import 'package:lamp_app/features/lamp_shell/presentation/lamp_shell.dart';
import 'package:lamp_app/features/nearby/application/nearby_lamps_notifier.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';

const _lampId = 'lamp-x';
const _otherId = 'lamp-y';
const _banner = 'Teaching Jacko new tricks';
const _settling = 'Giving Jacko a moment to settle';

/// The real notifier with its push replaced by a test-driven state, since a
/// real push needs a release-signed image.
class _DrivenFirmware extends FirmwareNotifier {
  @override
  FirmwareState build(String deviceId) => const FirmwareIdle();

  void emit(FirmwareState s) => state = s;
}

class _EmptyCache extends CachedFirmwareNotifier {
  @override
  Future<Map<String, CachedFirmware>> build() async => const {};

  @override
  Future<void> syncForInventory(List<InventoryLamp> inventory) async {}
}

const _lost = FirmwareFailed(
  reason: 'lost the lamp',
  failure: FirmwareFailure.linkLost,
);

final _footer = LsigFooter(
  channel: 'standard-beta',
  version: 0x010301,
  signedRegionLen: 0,
  signature: Uint8List(64),
);

GoRouter _router(String initial) => GoRouter(
  initialLocation: initial,
  routes: [
    GoRoute(
      path: AppRoutes.myLamps,
      builder: (_, _) => const Scaffold(body: Text('My Lamps')),
    ),
    GoRoute(
      path: '/lamp/:id/control',
      builder: (_, s) => LampShell(lampId: s.pathParameters['id']!),
    ),
    GoRoute(
      path: '/outcome/:id',
      builder: (_, s) => FirmwarePushOutcome(
        lampId: s.pathParameters['id']!,
        child: const Scaffold(body: Text('bare')),
      ),
    ),
    GoRoute(
      path: '/lamp/:id/expressions/:type/:target',
      builder: (_, s) => ExpressionEditorScreen(
        lampId: s.pathParameters['id']!,
        typeKey: s.pathParameters['type']!,
        targetKey: int.parse(s.pathParameters['target']!),
      ),
    ),
  ],
);

class _Rig {
  _Rig(this.container, this.ble);
  final ProviderContainer container;
  final InMemoryBleClient ble;

  _DrivenFirmware firmware(String id) =>
      container.read(firmwareNotifierProvider(id).notifier) as _DrivenFirmware;

  int get brightness =>
      container.read(controlNotifierProvider(_lampId)).value!.lamp.brightness;
}

Future<_Rig> _pump(
  WidgetTester tester, {
  String location = '/lamp/$_lampId/control',
  String baseColorsJson = '["#300783FF"]',
  bool devMode = false,
}) async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(
    ble,
    deviceId: _lampId,
    name: 'jacko',
    brightness: 50,
    baseColorsJson: baseColorsJson,
  );
  final router = _router(location);
  final c = ProviderContainer(
    overrides: [
      bleClientProvider.overrideWithValue(ble),
      bleScannerProvider.overrideWithValue(FakeBleScanner()),
      appRouterProvider.overrideWithValue(router),
      cachedFirmwareNotifierProvider.overrideWith(_EmptyCache.new),
      firmwareNotifierProvider.overrideWith2((_) => _DrivenFirmware()),
      devModeOnProvider.overrideWithValue(devMode),
    ],
  );
  addTearDown(c.dispose);
  await c.read(inventoryNotifierProvider.future);
  await c
      .read(inventoryNotifierProvider.notifier)
      .add(
        const InventoryLamp(
          id: _lampId,
          name: 'jacko',
          controlPassword: 'secret',
        ),
      );
  // Hold the other lamp's notifier alive so its state survives.
  c.listen(firmwareNotifierProvider(_otherId), (_, _) {});
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp.router(
        routerConfig: router,
        builder: (context, child) =>
            ReachingLampGate(child: child ?? const SizedBox.shrink()),
      ),
    ),
  );
  for (var i = 0; i < 60; i++) {
    await tester.pump(const Duration(milliseconds: 16));
    if (find.byType(BrightnessCard).evaluate().isNotEmpty ||
        find.byType(ExpressionEditorScreen).evaluate().isNotEmpty &&
            c.read(controlNotifierProvider(_lampId)).value?.connected == true) {
      break;
    }
  }
  return _Rig(c, ble);
}

Future<void> _settle(WidgetTester tester) async {
  for (var i = 0; i < 10; i++) {
    await tester.pump(const Duration(milliseconds: 50));
  }
}

/// Taps the far right of the brightness slider, which sets it to 100.
Future<void> _tapBrightnessMax(WidgetTester tester) async {
  final slider = find.descendant(
    of: find.byType(BrightnessCard),
    matching: find.byType(Slider),
  );
  await tester.tapAt(
    tester.getTopRight(slider) + Offset(-4, tester.getSize(slider).height / 2),
  );
  await _settle(tester);
}

/// CHAR_STATE_NOTIFY from a lamp that reports OTA quiet mode.
void _lampQuiet(_Rig rig, bool quiet) => rig.ble.simulateNotify(
  _lampId,
  BleUuids.controlService,
  BleUuids.stateNotify,
  Uint8List.fromList(
    utf8.encode('{"previewActive":false,"nearbyRev":0,"quiet":$quiet}'),
  ),
);

/// A push the lamp goes quiet for, failing while it is still quiet.
Future<void> _failWhileQuiet(WidgetTester tester, _Rig rig) async {
  rig
      .firmware(_lampId)
      .emit(FirmwareStreaming(footer: _footer, chunksSent: 3, totalChunks: 10));
  _lampQuiet(rig, true);
  await _settle(tester);
  rig.firmware(_lampId).emit(_lost);
  await _settle(tester);
}

double _opacityAbove(WidgetTester tester, Finder f) {
  var o = 1.0;
  for (final w in tester.widgetList<Opacity>(
    find.ancestor(of: f, matching: find.byType(Opacity)),
  )) {
    o *= w.opacity;
  }
  return o;
}

void main() {
  testWidgets('a push to this lamp freezes the Colors tab under the banner, '
      'and success thaws it', (tester) async {
    final rig = await _pump(tester);
    expect(find.textContaining(_banner), findsNothing);

    rig.firmware(_lampId).emit(FirmwareOfferSent(footer: _footer));
    await _settle(tester);
    expect(find.textContaining(_banner), findsOneWidget);
    expect(
      _opacityAbove(tester, find.byType(BrightnessCard)),
      lessThan(1),
      reason: 'frozen controls must look disabled',
    );
    await _tapBrightnessMax(tester);
    expect(rig.brightness, 50);
    expect(rig.ble.writesTo(_lampId, BleUuids.brightness), isEmpty);

    rig
        .firmware(_lampId)
        .emit(
          FirmwareStreaming(footer: _footer, chunksSent: 3, totalChunks: 10),
        );
    await _settle(tester);
    expect(find.textContaining(_banner), findsOneWidget);

    rig.firmware(_lampId).emit(FirmwareSucceeded(footer: _footer));
    await _settle(tester);
    expect(find.textContaining(_banner), findsNothing);
    expect(_opacityAbove(tester, find.byType(BrightnessCard)), 1);
    await _tapBrightnessMax(tester);
    expect(rig.brightness, 100);
  });

  testWidgets('a failed push thaws editing', (tester) async {
    final rig = await _pump(tester);
    rig
        .firmware(_lampId)
        .emit(
          FirmwareStreaming(footer: _footer, chunksSent: 3, totalChunks: 10),
        );
    await _settle(tester);
    await _tapBrightnessMax(tester);
    expect(rig.brightness, 50);

    rig.firmware(_lampId).emit(_lost);
    await _settle(tester);
    expect(find.textContaining(_banner), findsNothing);
    await _tapBrightnessMax(tester);
    expect(rig.brightness, 100);
  });

  testWidgets('after a failed push the banner and freeze hold until the lamp '
      'says it listens', (tester) async {
    final rig = await _pump(tester);
    await _failWhileQuiet(tester, rig);
    expect(find.textContaining(_settling), findsOneWidget);
    expect(_opacityAbove(tester, find.byType(BrightnessCard)), lessThan(1));
    await _tapBrightnessMax(tester);
    expect(rig.brightness, 50);

    _lampQuiet(rig, false);
    await _settle(tester);
    expect(find.textContaining(_settling), findsNothing);
    await _tapBrightnessMax(tester);
    expect(rig.brightness, 100);
  });

  testWidgets('a lamp that never says it listens thaws at the bound', (
    tester,
  ) async {
    final rig = await _pump(tester);
    await _failWhileQuiet(tester, rig);
    await tester.pump(
      ControlNotifier.quietAfterPushBound - const Duration(seconds: 1),
    );
    expect(find.textContaining(_settling), findsOneWidget);
    await tester.pump(const Duration(seconds: 2));
    await _settle(tester);
    expect(find.textContaining(_settling), findsNothing);
    await _tapBrightnessMax(tester);
    expect(rig.brightness, 100);
  });

  testWidgets('a second failed push restarts the bound', (tester) async {
    final rig = await _pump(tester);
    await _failWhileQuiet(tester, rig);
    await tester.pump(const Duration(seconds: 50));
    await _failWhileQuiet(tester, rig);
    await tester.pump(
      ControlNotifier.quietAfterPushBound - const Duration(seconds: 49),
    );
    expect(find.textContaining(_settling), findsOneWidget);
    await tester.pump(const Duration(seconds: 50));
    await _settle(tester);
    expect(find.textContaining(_settling), findsNothing);
  });

  testWidgets('the bound firing after the notifier is gone is harmless', (
    tester,
  ) async {
    final rig = await _pump(tester);
    await _failWhileQuiet(tester, rig);
    await tester.pumpWidget(const SizedBox.shrink());
    rig.container.dispose();
    await tester.pump(ControlNotifier.quietAfterPushBound);
    expect(tester.takeException(), isNull);
  });

  testWidgets('a rebuild during the hold keeps the bound', (tester) async {
    final rig = await _pump(tester);
    await _failWhileQuiet(tester, rig);
    rig.container.invalidate(controlNotifierProvider(_lampId));
    await _settle(tester);
    expect(find.textContaining(_settling), findsOneWidget);
    await tester.pump(ControlNotifier.quietAfterPushBound);
    await _settle(tester);
    expect(find.textContaining(_settling), findsNothing);
  });

  testWidgets("another lamp's push changes nothing here", (tester) async {
    final rig = await _pump(tester);
    rig
        .firmware(_otherId)
        .emit(
          FirmwareStreaming(footer: _footer, chunksSent: 3, totalChunks: 10),
        );
    await _settle(tester);
    expect(find.textContaining('new tricks'), findsNothing);
    expect(_opacityAbove(tester, find.byType(BrightnessCard)), 1);
    await _tapBrightnessMax(tester);
    expect(rig.brightness, 100);
  });

  testWidgets('the Info tab stays live during a push', (tester) async {
    final rig = await _pump(tester);
    rig.firmware(_lampId).emit(FirmwareOfferSent(footer: _footer));
    await _settle(tester);
    await tester.tap(find.text('Info'));
    await _settle(tester);
    expect(find.byType(InfoScreen), findsOneWidget);
    expect(find.textContaining(_banner), findsOneWidget);
    expect(_opacityAbove(tester, find.byType(InfoScreen)), 1);
    expect(
      find.ancestor(
        of: find.byType(InfoScreen),
        matching: find.byType(FirmwarePushLock),
      ),
      findsNothing,
    );
  });

  testWidgets('a dialog open when the push starts freezes with its draft, '
      'and saves once the push ends', (tester) async {
    final rig = await _pump(tester);
    await tester.tap(find.text('Config'));
    await _settle(tester);
    await tester.tap(find.text('Name'));
    await _settle(tester);
    await tester.enterText(find.byType(TextField), 'Lily');
    await tester.pump();
    bool typing() => tester
        .state<EditableTextState>(find.byType(EditableText))
        .widget
        .focusNode
        .hasFocus;
    expect(typing(), isTrue);

    rig.firmware(_lampId).emit(FirmwareOfferSent(footer: _footer));
    await _settle(tester);
    expect(_opacityAbove(tester, find.byType(AlertDialog)), lessThan(1));
    expect(
      typing(),
      isFalse,
      reason: 'the keyboard must not reach a frozen field',
    );
    await tester.tap(find.text('Save'));
    await _settle(tester);
    expect(
      find.byType(AlertDialog),
      findsOneWidget,
      reason: 'frozen dialog stays open',
    );
    expect(
      rig.container.read(controlNotifierProvider(_lampId)).value!.lamp.name,
      'jacko',
    );

    rig.firmware(_lampId).emit(_lost);
    await _settle(tester);
    expect(find.text('Lily'), findsOneWidget, reason: 'draft survives');
    await tester.tap(find.text('Save'));
    await _settle(tester);
    expect(
      rig.container
          .read(controlNotifierProvider(_lampId))
          .value!
          .lamp
          .name
          .toLowerCase(),
      'lily',
    );
  });

  testWidgets('a tap outside a frozen dialog still dismisses it', (
    tester,
  ) async {
    final rig = await _pump(tester);
    await tester.tap(find.text('Config'));
    await _settle(tester);
    await tester.tap(find.text('Name'));
    await _settle(tester);
    rig.firmware(_lampId).emit(FirmwareOfferSent(footer: _footer));
    await _settle(tester);
    await tester.tapAt(const Offset(10, 300));
    await _settle(tester);
    expect(find.byType(AlertDialog), findsNothing);
  });

  testWidgets("the expression editor's Test action is disabled during a push", (
    tester,
  ) async {
    final rig = await _pump(
      tester,
      location: '/lamp/$_lampId/expressions/breathing/0',
    );
    IconButton test() => tester.widget<IconButton>(
      find.widgetWithIcon(IconButton, Icons.play_arrow_rounded),
    );
    expect(test().onPressed, isNotNull);
    rig.firmware(_lampId).emit(FirmwareOfferSent(footer: _footer));
    await _settle(tester);
    expect(test().onPressed, isNull);
    rig.firmware(_lampId).emit(FirmwareSucceeded(footer: _footer));
    await _settle(tester);
    expect(test().onPressed, isNotNull);
  });

  testWidgets('frozen controls refuse screen-reader actions', (tester) async {
    final semantics = tester.ensureSemantics();
    final rig = await _pump(tester);
    bool canIncrease() =>
        find.semantics.byAction(SemanticsAction.increase).evaluate().isNotEmpty;
    expect(canIncrease(), isTrue);
    rig.firmware(_lampId).emit(FirmwareOfferSent(footer: _footer));
    await _settle(tester);
    expect(canIncrease(), isFalse);
    rig.firmware(_lampId).emit(_lost);
    await _settle(tester);
    expect(canIncrease(), isTrue);
    semantics.dispose();
  });

  testWidgets('the banner takes the status-bar inset from the screen below', (
    tester,
  ) async {
    tester.view.padding = const FakeViewPadding(top: 48);
    addTearDown(tester.view.resetPadding);
    final rig = await _pump(tester);
    final before = tester.getSize(find.byType(AppBar)).height;
    rig.firmware(_lampId).emit(FirmwareOfferSent(footer: _footer));
    await _settle(tester);
    final inset = 48 / tester.view.devicePixelRatio;
    expect(tester.getSize(find.byType(AppBar)).height, before - inset);
    final strip = find
        .ancestor(
          of: find.textContaining(_banner),
          matching: find.byType(Material),
        )
        .first;
    expect(
      tester.getTopLeft(find.byType(AppBar)).dy,
      tester.getBottomLeft(strip).dy,
    );
  });

  testWidgets('a dirty colour sheet frozen mid-push can still be discarded', (
    tester,
  ) async {
    final rig = await _pump(
      tester,
      baseColorsJson: '["#300783FF","#FF0000FF"]',
    );
    await tester.tap(find.text('Base'));
    await _settle(tester);
    await tester.tap(find.byIcon(Icons.close).first);
    await _settle(tester);
    rig.firmware(_lampId).emit(FirmwareOfferSent(footer: _footer));
    await _settle(tester);
    final sheetTop = tester.getTopLeft(find.byType(BottomSheet)).dy;
    await tester.tapAt(Offset(400, sheetTop - 8));
    await _settle(tester);
    expect(find.text('Discard changes?'), findsOneWidget);
    expect(_opacityAbove(tester, find.text('Discard changes?')), 1);
    await tester.tap(find.text('Discard'));
    await _settle(tester);
    expect(find.byType(BottomSheet), findsNothing);
  });

  testWidgets("the expression editor's colours sheet freezes with the screen", (
    tester,
  ) async {
    // Target 1 (shade) so the sheet's edits stream to the lamp live.
    final rig = await _pump(
      tester,
      location: '/lamp/$_lampId/expressions/breathing/1',
    );
    await tester.tap(find.byType(ColorBlocksBar));
    await _settle(tester);
    expect(find.byType(ColorStopsSheet), findsOneWidget);
    rig.firmware(_lampId).emit(FirmwareOfferSent(footer: _footer));
    await _settle(tester);
    expect(_opacityAbove(tester, find.byType(ColorStopsSheet)), lessThan(1));
    final writes =
        rig.ble.writesTo(_lampId, BleUuids.shadeColors).length +
        rig.ble.writesTo(_lampId, BleUuids.baseColors).length;
    await tester.tap(find.text('Add Color'));
    await _settle(tester);
    expect(
      rig.ble.writesTo(_lampId, BleUuids.shadeColors).length +
          rig.ble.writesTo(_lampId, BleUuids.baseColors).length,
      writes,
    );
    expect(find.byType(BottomSheet), findsOneWidget);
  });

  testWidgets(
    'a lock with no layout change still drops screen-reader actions',
    (tester) async {
      final semantics = tester.ensureSemantics();
      final c = ProviderContainer(
        overrides: [
          firmwareNotifierProvider.overrideWith2((_) => _DrivenFirmware()),
        ],
      );
      addTearDown(c.dispose);
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: c,
          child: MaterialApp(
            home: Scaffold(
              body: FirmwarePushLock(
                lampId: _lampId,
                child: Slider(value: 0.5, onChanged: (_) {}),
              ),
            ),
          ),
        ),
      );
      bool canIncrease() => find.semantics
          .byAction(SemanticsAction.increase)
          .evaluate()
          .isNotEmpty;
      expect(canIncrease(), isTrue);
      (c.read(firmwareNotifierProvider(_lampId).notifier) as _DrivenFirmware)
          .emit(FirmwareOfferSent(footer: _footer));
      await tester.pump();
      expect(canIncrease(), isFalse);
      semantics.dispose();
    },
  );

  for (final path in [
    AppRoutes.setup(_lampId),
    AppRoutes.homeMode(_lampId),
    AppRoutes.homeWifi(_lampId),
    AppRoutes.advancedLeds(_lampId),
    AppRoutes.knockout(_lampId),
    AppRoutes.addExpression(_lampId),
    AppRoutes.expressionEditor(_lampId, 'breathing', 0),
    AppRoutes.wispConfig(_lampId),
  ]) {
    testWidgets('$path freezes its body and keeps its app bar live', (
      tester,
    ) async {
      SharedPreferences.setMockInitialValues({});
      final ble = InMemoryBleClient();
      await seedControlBle(ble, deviceId: _lampId, name: 'jacko');
      final c = ProviderContainer(
        overrides: [
          bleClientProvider.overrideWithValue(ble),
          bleScannerProvider.overrideWithValue(FakeBleScanner()),
          cachedFirmwareNotifierProvider.overrideWith(_EmptyCache.new),
          firmwareNotifierProvider.overrideWith2((_) => _DrivenFirmware()),
        ],
      );
      addTearDown(c.dispose);
      await c.read(inventoryNotifierProvider.future);
      await c
          .read(inventoryNotifierProvider.notifier)
          .add(
            const InventoryLamp(
              id: _lampId,
              name: 'jacko',
              controlPassword: 'secret',
            ),
          );
      final router = c.read(appRouterProvider);
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: c,
          child: MaterialApp.router(routerConfig: router),
        ),
      );
      router.go(path);
      await _settle(tester);
      (c.read(firmwareNotifierProvider(_lampId).notifier) as _DrivenFirmware)
          .emit(FirmwareOfferSent(footer: _footer));
      await _settle(tester);

      final locks = find.byWidgetPredicate(
        (w) => w is FirmwarePushLock && w.lampId == _lampId,
      );
      expect(locks, findsWidgets);
      final lockOpacity = tester.widget<Opacity>(
        find.descendant(of: locks.first, matching: find.byType(Opacity)).first,
      );
      expect(lockOpacity.opacity, lessThan(1));
      expect(_opacityAbove(tester, find.byType(AppBar)), 1);
    });
  }

  testWidgets('the strip says the lamp is settling once the push ends and '
      'the hold runs', (tester) async {
    final rig = await _pump(tester);
    rig
        .firmware(_lampId)
        .emit(FirmwareStreaming(footer: _footer, chunksSent: 3, totalChunks: 10));
    _lampQuiet(rig, true);
    await _settle(tester);
    expect(find.textContaining(_banner), findsOneWidget);
    expect(find.textContaining(_settling), findsNothing);

    rig.firmware(_lampId).emit(_lost);
    await _settle(tester);
    expect(find.textContaining(_settling), findsOneWidget);
    expect(find.textContaining(_banner), findsNothing);
    _lampQuiet(rig, false);
    await _settle(tester);
  });

  testWidgets("the expression editor's Test action stays disabled through "
      'the hold after a push', (tester) async {
    final rig = await _pump(
      tester,
      location: '/lamp/$_lampId/expressions/breathing/0',
    );
    IconButton test() => tester.widget<IconButton>(
      find.widgetWithIcon(IconButton, Icons.play_arrow_rounded),
    );
    await _failWhileQuiet(tester, rig);
    expect(test().onPressed, isNull);
    _lampQuiet(rig, false);
    await _settle(tester);
    expect(test().onPressed, isNotNull);
  });

  group('how a push ended', () {
    const missed =
        "Jacko didn't catch the new tricks this time. "
        'Trying again next time you connect.';
    const learned = 'Jacko learned some new tricks ✨';

    /// Lets a shown snackbar time out.
    Future<void> outlast(WidgetTester tester) async {
      await tester.pump(const Duration(seconds: 5));
      await _settle(tester);
    }

    /// Drops the link and lets the ladder bring the lamp back on [fwVersion].
    Future<void> rebootInto(WidgetTester tester, _Rig rig, int fwVersion) async {
      await seedControlBle(
        rig.ble,
        deviceId: _lampId,
        name: 'jacko',
        brightness: 50,
        baseColorsJson: '["#300783FF"]',
        fwVersion: fwVersion,
        fwChannel: 'standard-beta',
      );
      ControlState? lamp() =>
          rig.container.read(controlNotifierProvider(_lampId)).value;
      await rig.ble.disconnect(_lampId);
      await tester.pump();
      expect(lamp()?.connected, isFalse);
      for (var i = 0; i < 120 && lamp()?.connected != true; i++) {
        await tester.pump(const Duration(milliseconds: 16));
      }
      expect(lamp()?.lamp.fwVersion, fwVersion);
      await _settle(tester);
    }

    testWidgets('a failed push says so once, when the hold ends', (
      tester,
    ) async {
      final rig = await _pump(tester);
      await _failWhileQuiet(tester, rig);
      expect(find.text(missed), findsNothing);

      _lampQuiet(rig, false);
      await _settle(tester);
      expect(find.text(missed), findsOneWidget);

      await outlast(tester);
      _lampQuiet(rig, true);
      await _settle(tester);
      _lampQuiet(rig, false);
      await _settle(tester);
      expect(find.text(missed), findsNothing);
    });

    testWidgets('waits for the hold wherever it is mounted', (tester) async {
      // Mounted bare, it subscribes to the push before the lamp's notifier
      // does, so it hears the push end before the hold starts.
      final rig = await _pump(tester, location: '/outcome/$_lampId');
      await _failWhileQuiet(tester, rig);
      expect(find.text(missed), findsNothing);
      _lampQuiet(rig, false);
      await _settle(tester);
      expect(find.text(missed), findsOneWidget);
    });

    testWidgets('a failed push with no hold says so at once', (tester) async {
      final rig = await _pump(tester);
      rig
          .firmware(_lampId)
          .emit(
            FirmwareStreaming(footer: _footer, chunksSent: 3, totalChunks: 10),
          );
      await _settle(tester);
      rig.firmware(_lampId).emit(_lost);
      await _settle(tester);
      expect(find.text(missed), findsOneWidget);
    });

    for (final declined in [
      FirmwareFailure.busy,
      FirmwareFailure.untrustedAtOffer,
    ]) {
      testWidgets('an offer the lamp declines (${declined.name}) says nothing', (
        tester,
      ) async {
        final rig = await _pump(tester);
        rig.firmware(_lampId).emit(FirmwareOfferSent(footer: _footer));
        _lampQuiet(rig, true);
        await _settle(tester);
        rig
            .firmware(_lampId)
            .emit(FirmwareFailed(reason: 'declined', failure: declined));
        await _settle(tester);
        _lampQuiet(rig, false);
        await _settle(tester);
        expect(find.text(missed), findsNothing);
      });
    }

    testWidgets('a download that fails its checks before the offer says '
        'nothing', (tester) async {
      final rig = await _pump(tester);
      rig.firmware(_lampId).emit(const FirmwareVerifying());
      await _settle(tester);
      rig
          .firmware(_lampId)
          .emit(
            const FirmwareFailed(
              reason: 'The cached firmware failed signature verification.',
              failure: FirmwareFailure.cacheUnverified,
            ),
          );
      await _settle(tester);
      expect(find.text(missed), findsNothing);
    });

    testWidgets('the next push replaces a line still waiting', (tester) async {
      final rig = await _pump(tester);
      await _failWhileQuiet(tester, rig);
      rig.firmware(_lampId).emit(FirmwareOfferSent(footer: _footer));
      await _settle(tester);
      rig
          .firmware(_lampId)
          .emit(
            const FirmwareFailed(
              reason: 'Another update is already in progress on this lamp.',
              failure: FirmwareFailure.busy,
            ),
          );
      await _settle(tester);
      _lampQuiet(rig, false);
      await _settle(tester);
      expect(find.text(missed), findsNothing);
    });

    testWidgets('a cancelled push says nothing', (tester) async {
      final rig = await _pump(tester);
      rig
          .firmware(_lampId)
          .emit(
            FirmwareStreaming(footer: _footer, chunksSent: 3, totalChunks: 10),
          );
      await _settle(tester);
      rig
          .firmware(_lampId)
          .emit(
            const FirmwareFailed(
              reason: 'Update cancelled.',
              failure: FirmwareFailure.cancelled,
            ),
          );
      await _settle(tester);
      expect(find.text(missed), findsNothing);
    });

    testWidgets('a good push says the lamp learned, once it is back on the '
        'new version', (tester) async {
      final rig = await _pump(tester);
      rig
          .firmware(_lampId)
          .emit(
            FirmwareStreaming(footer: _footer, chunksSent: 9, totalChunks: 10),
          );
      await _settle(tester);
      rig.firmware(_lampId).emit(FirmwareSucceeded(footer: _footer));
      await _settle(tester);
      expect(find.text(learned), findsNothing);

      await rebootInto(tester, rig, _footer.version);
      expect(find.text(learned), findsOneWidget);
      await outlast(tester);
      await rebootInto(tester, rig, _footer.version);
      expect(find.text(learned), findsNothing);
      // A fresh connect, as from "Try again", reads the version anew.
      rig.container.invalidate(controlNotifierProvider(_lampId));
      for (var i = 0; i < 60; i++) {
        await tester.pump(const Duration(milliseconds: 16));
      }
      expect(find.text(learned), findsNothing);
    });

    testWidgets('a push the phone lost after the lamp took it says learned, '
        'not missed', (tester) async {
      final rig = await _pump(tester);
      await _failWhileQuiet(tester, rig);
      await rebootInto(tester, rig, _footer.version);
      expect(find.text(learned), findsOneWidget);
      await outlast(tester);
      _lampQuiet(rig, false);
      await _settle(tester);
      expect(find.text(missed), findsNothing);
    });

    testWidgets('a push that ends after the lamp is back on its version says '
        'learned, not missed', (tester) async {
      final rig = await _pump(tester);
      rig.firmware(_lampId).emit(FirmwareFinalizing(footer: _footer));
      await _settle(tester);
      await rebootInto(tester, rig, _footer.version);
      expect(find.text(learned), findsOneWidget);
      await outlast(tester);
      rig
          .firmware(_lampId)
          .emit(
            const FirmwareFailed(
              reason: 'Lamp did not report an OTA result within the budget.',
              failure: FirmwareFailure.noResult,
            ),
          );
      await _settle(tester);
      expect(find.text(missed), findsNothing);
    });

    testWidgets('a good push says nothing while the lamp comes back on its '
        'old version', (tester) async {
      final rig = await _pump(tester);
      rig.firmware(_lampId).emit(FirmwareOfferSent(footer: _footer));
      await _settle(tester);
      rig.firmware(_lampId).emit(FirmwareSucceeded(footer: _footer));
      await _settle(tester);
      await rebootInto(tester, rig, _footer.version - 1);
      expect(find.text(learned), findsNothing);
    });

    testWidgets('an offer the lamp already knows says nothing', (tester) async {
      final rig = await _pump(tester);
      rig.firmware(_lampId).emit(FirmwareOfferSent(footer: _footer));
      await _settle(tester);
      rig
          .firmware(_lampId)
          .emit(FirmwareSucceeded(footer: _footer, alreadyCurrent: true));
      await _settle(tester);
      await rebootInto(tester, rig, _footer.version);
      expect(find.text(learned), findsNothing);
    });
  });

  group('the dev-mode Firmware panel', () {
    Future<_Rig> onInfo(WidgetTester tester) async {
      final rig = await _pump(tester, devMode: true);
      await tester.tap(find.text('Info'));
      await _settle(tester);
      return rig;
    }

    Finder raw(String text) => find.byWidgetPredicate(
      (w) => w is SelectableText && w.data == text,
    );

    testWidgets('a failure leads with a friendly line, the reason beneath', (
      tester,
    ) async {
      final rig = await onInfo(tester);
      const reason = 'Chunk write failed at idx=312: device is disconnected';
      rig
          .firmware(_lampId)
          .emit(
            const FirmwareFailed(
              reason: reason,
              failure: FirmwareFailure.linkLost,
            ),
          );
      await _settle(tester);
      final friendly = find.text(
        'Lost Jacko partway through. Bring your phone closer and try again.',
      );
      expect(friendly, findsOneWidget);
      expect(raw(reason), findsOneWidget);
      expect(
        tester.getTopLeft(raw(reason)).dy,
        greaterThan(tester.getTopLeft(friendly).dy),
      );
      expect(
        tester.widget<SelectableText>(raw(reason)).style?.fontFamily,
        'monospace',
      );
    });

    testWidgets('a lamp already on the version is not said to reboot', (
      tester,
    ) async {
      final rig = await onInfo(tester);
      rig
          .firmware(_lampId)
          .emit(FirmwareSucceeded(footer: _footer, alreadyCurrent: true));
      await _settle(tester);
      expect(find.text('Jacko already knows these tricks.'), findsOneWidget);
      expect(find.textContaining('rebooting'), findsNothing);

      rig.firmware(_lampId).emit(FirmwareSucceeded(footer: _footer));
      await _settle(tester);
      expect(find.textContaining('rebooting'), findsOneWidget);
    });
  });
}
