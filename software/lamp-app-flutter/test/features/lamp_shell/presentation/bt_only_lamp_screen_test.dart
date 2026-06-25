import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:go_router/go_router.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/ble_scanner.dart';
import 'package:lamp_app/features/control/presentation/control_screen.dart';
import 'package:lamp_app/features/control/presentation/widgets/brightness_card.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/lamp_shell/presentation/bt_only_lamp_screen.dart';
import 'package:lamp_app/features/nearby/application/nearby_lamps_notifier.dart';
import 'package:lamp_app/features/nearby/domain/nearby_lamp.dart';

import '../../../_support/seed.dart';

const _devId = 'lamp-x';

NearbyLamp _adv({required bool isMesh}) => NearbyLamp(
      id: _devId,
      name: 'jacko',
      rssi: -50,
      serviceUuids: const [],
      baseRgb: 0,
      shadeRgb: 0,
      lastSeenEpochMs: 0,
      isMesh: isMesh,
    );

Future<ProviderContainer> _container() async {
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

Future<void> _pumpUntil(WidgetTester tester, Finder finder,
    {int frames = 60}) async {
  for (var i = 0; i < frames; i++) {
    await tester.pump(const Duration(milliseconds: 16));
    if (finder.evaluate().isNotEmpty) return;
  }
}

void main() {
  // ---------------------------------------------------------------------------
  // If the user ends up on BtOnlyLampScreen because of a stale `isMesh: false`
  // adv, and the next adv arrives reporting `isMesh: true`, the app must
  // auto-route them to ControlScreen — they shouldn't have to back out and
  // tap the lamp again.
  // ---------------------------------------------------------------------------
  testWidgets(
      'BtOnlyLampScreen auto-routes to control when isMesh flips to true',
      (tester) async {
    final c = await _container();
    addTearDown(c.dispose);

    c.read(nearbyLampsNotifierProvider.notifier).state = [
      _adv(isMesh: false),
    ];

    final router = GoRouter(
      initialLocation: '/lamp/$_devId/bt-only',
      routes: [
        GoRoute(
          path: '/lamp/:id/bt-only',
          builder: (_, state) =>
              BtOnlyLampScreen(lampId: state.pathParameters['id']!),
        ),
        GoRoute(
          path: '/lamp/:id/control',
          // Scaffold supplies the Material ancestor ControlScreen's cards
          // expect — without it the InkWell inside ShadeCard asserts.
          builder: (_, state) => Scaffold(
            body: ControlScreen(lampId: state.pathParameters['id']!),
          ),
        ),
      ],
    );
    addTearDown(router.dispose);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: MaterialApp.router(routerConfig: router),
    ));
    await _pumpUntil(tester, find.byType(BtOnlyLampScreen));
    expect(find.byType(BtOnlyLampScreen), findsOneWidget);
    expect(router.routerDelegate.currentConfiguration.uri.toString(),
        '/lamp/$_devId/bt-only');

    // Fresh adv arrives — lamp is mesh-capable after all.
    c.read(nearbyLampsNotifierProvider.notifier).state = [
      _adv(isMesh: true),
    ];

    // Wait for the post-frame pushReplacement to fire and ControlScreen to
    // reach its data state (BrightnessCard mounts when the section reads
    // resolve through the seeded BLE).
    await _pumpUntil(tester, find.byType(BrightnessCard));

    expect(find.byType(BrightnessCard), findsOneWidget,
        reason: 'auto-routed to ControlScreen on mesh flip');
    // The router's current configuration is the authoritative signal that
    // pushReplacement landed. (The outgoing page may briefly stay in the
    // widget tree during GoRouter's page transition animation, so we
    // don't assert on BtOnlyLampScreen absence here.)
    expect(router.routerDelegate.currentConfiguration.uri.toString(),
        '/lamp/$_devId/control');
  });
}
