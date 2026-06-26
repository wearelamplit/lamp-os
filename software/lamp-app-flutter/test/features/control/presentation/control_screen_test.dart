import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:go_router/go_router.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/ble_scanner.dart';
import 'package:lamp_app/features/control/presentation/control_screen.dart';
import 'package:lamp_app/features/control/presentation/widgets/base_card.dart';
import 'package:lamp_app/features/control/presentation/widgets/brightness_card.dart';
import 'package:lamp_app/features/control/presentation/widgets/connecting_view.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/lamp_shell/presentation/bt_only_lamp_screen.dart';
import 'package:lamp_app/features/nearby/application/nearby_lamps_notifier.dart';
import 'package:lamp_app/features/nearby/domain/nearby_lamp.dart';

import '../../../_support/seed.dart';

const _devId = 'lamp-x';

Future<ProviderContainer> _withLamp() async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble, deviceId: _devId, name: 'jacko');
  final c = ProviderContainer(
    overrides: [
      bleClientProvider.overrideWithValue(ble),
      bleScannerProvider.overrideWithValue(FakeBleScanner()),
    ],
  );
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId, name: 'jacko', controlPassword: 'secret'));
  return c;
}

/// Inventory + BLE state where the firmware's auth gate would reject our
/// stored credential: lampSection reads back empty bytes. controlNotifier's
/// canary trips and the provider goes to AsyncError(LampAuthRequiredException).
Future<(ProviderContainer, InMemoryBleClient)> _withAuthGatedLamp() async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble, deviceId: _devId, name: 'jacko');
  // Wipe the lamp section so the canary read returns empty bytes.
  ble.seedSection(_devId, 'lamp', Uint8List(0));
  final c = ProviderContainer(
    overrides: [
      bleClientProvider.overrideWithValue(ble),
      bleScannerProvider.overrideWithValue(FakeBleScanner()),
    ],
  );
  await c.read(inventoryNotifierProvider.future);
  // Empty password mirrors the "stored credential no longer satisfies the
  // gate" scenario this flow is built for.
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId, name: 'jacko', controlPassword: ''));
  return (c, ble);
}

Future<void> _pumpUntil(WidgetTester tester, Finder finder,
    {int frames = 60}) async {
  for (var i = 0; i < frames; i++) {
    await tester.pump(const Duration(milliseconds: 16));
    if (finder.evaluate().isNotEmpty) return;
  }
}

Future<void> _pumpToData(WidgetTester tester) async {
  for (var i = 0; i < 30; i++) {
    await tester.pump(const Duration(milliseconds: 16));
    if (find.byType(BrightnessCard).evaluate().isNotEmpty) return;
  }
}

void main() {
  testWidgets('BrightnessCard sits below BaseCard', (tester) async {
    final c = await _withLamp();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(body: ControlScreen(lampId: _devId)),
      ),
    ));
    await _pumpToData(tester);
    await tester.dragUntilVisible(
      find.byType(BrightnessCard),
      find.byType(ListView),
      const Offset(0, -200),
    );
    final brightnessY = tester.getTopLeft(find.byType(BrightnessCard)).dy;
    final baseY = tester.getTopLeft(find.byType(BaseCard)).dy;
    expect(brightnessY, greaterThan(baseY),
        reason: 'Brightness must be visually below the base card');
  });

  // ---------------------------------------------------------------------------
  // Defense-in-depth redirect: a stale `isMesh: false` adv in the cache must
  // NOT yank a user out of ControlScreen when the BLE connection actually
  // succeeded. The cached adv is older than the connection-success signal
  // we just got, so the connection wins. (Previously the redirect fired
  // unconditionally on `isMesh: false`, trapping users on BtOnlyLampScreen
  // for a lamp that was clearly working.)
  // ---------------------------------------------------------------------------
  testWidgets(
      'ControlScreen stays put when isMesh false but connection succeeded',
      (tester) async {
    final c = await _withLamp();
    addTearDown(c.dispose);
    c.read(nearbyLampsNotifierProvider.notifier).state = [
      const NearbyLamp(
        id: _devId,
        name: 'jacko',
        rssi: -50,
        serviceUuids: [],
        baseRgb: 0,
        shadeRgb: 0,
        lastSeenEpochMs: 0,
        isMesh: false,
      ),
    ];

    // Use a real GoRouter with both routes so we can detect whether the
    // defense-in-depth redirect fires. If it does, the location flips to
    // the BT-only route; if it stays put, we render the control surface.
    // The Scaffold wrapper supplies the Material ancestor InkWell-based
    // cards inside ControlScreen rely on.
    final router = GoRouter(
      initialLocation: '/lamp/$_devId/control',
      routes: [
        GoRoute(
          path: '/lamp/:id/control',
          builder: (_, state) => Scaffold(
            body: ControlScreen(lampId: state.pathParameters['id']!),
          ),
        ),
        GoRoute(
          path: '/lamp/:id/bt-only',
          builder: (_, state) =>
              BtOnlyLampScreen(lampId: state.pathParameters['id']!),
        ),
      ],
    );
    addTearDown(router.dispose);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: MaterialApp.router(routerConfig: router),
    ));
    await _pumpToData(tester);

    expect(find.byType(BrightnessCard), findsOneWidget,
        reason: 'control surface mounted — connection succeeded');
    expect(find.byType(BtOnlyLampScreen), findsNothing,
        reason: 'no redirect to BT-only despite stale isMesh: false adv');
    expect(router.routerDelegate.currentConfiguration.uri.toString(),
        '/lamp/$_devId/control');
  });

  // ---------------------------------------------------------------------------
  // Connect-time password prompt: when the stored credential no longer
  // satisfies the firmware's auth gate, ControlScreen surfaces a dialog over
  // ConnectingView instead of dying with a generic FormatException.
  // ---------------------------------------------------------------------------

  testWidgets(
      'shows password dialog over ConnectingView when auth is required',
      (tester) async {
    final (c, _) = await _withAuthGatedLamp();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(body: ControlScreen(lampId: _devId)),
      ),
    ));
    await _pumpUntil(tester, find.text('Enter password'));
    expect(find.text('Enter password'), findsOneWidget);
    expect(find.byType(ConnectingView), findsOneWidget,
        reason: 'ConnectingView stays behind the dialog');
  });

  testWidgets(
      'Connect with correct password closes dialog and renders control surface',
      (tester) async {
    final (c, ble) = await _withAuthGatedLamp();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(body: ControlScreen(lampId: _devId)),
      ),
    ));
    await _pumpUntil(tester, find.text('Enter password'));

    // Simulate firmware "unlock": now reads of the lamp section return
    // real bytes.
    ble.seedSection(
      _devId,
      'lamp',
      Uint8List.fromList(utf8.encode(
        '{"name":"jacko","brightness":50,"advancedEnabled":false}',
      )),
    );

    await tester.enterText(find.byType(TextField), 'alpha');
    await tester.tap(find.widgetWithText(FilledButton, 'Connect'));
    await _pumpUntil(tester, find.byType(BrightnessCard));

    expect(find.text('Enter password'), findsNothing);
    expect(find.byType(BrightnessCard), findsOneWidget);
  });

  testWidgets(
      'Connect with wrong password shows inline error and keeps dialog open',
      (tester) async {
    final (c, _) = await _withAuthGatedLamp();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(body: ControlScreen(lampId: _devId)),
      ),
    ));
    await _pumpUntil(tester, find.text('Enter password'));

    // Do NOT unlock: lampSection stays empty, so the canary in
    // submitConnectPassword fails again.
    await tester.enterText(find.byType(TextField), 'nope');
    await tester.tap(find.widgetWithText(FilledButton, 'Connect'));
    await _pumpUntil(tester, find.textContaining('Wrong password'));

    expect(find.text('Enter password'), findsOneWidget,
        reason: 'Dialog stays open so the user can retry');
    expect(find.textContaining('Wrong password'), findsOneWidget);
  });
}
