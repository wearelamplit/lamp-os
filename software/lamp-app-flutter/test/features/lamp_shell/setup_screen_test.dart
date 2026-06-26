import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/control/application/advanced_session.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/lamp_shell/presentation/setup_screen.dart';

import '../../_support/seed.dart';

const _devId = 'lamp-x';

Future<void> _seed(
  InMemoryBleClient ble, {
  String homeSsid = '',
  bool advancedEnabled = false,
}) =>
    seedControlBle(
      ble,
      deviceId: _devId,
      homeSsid: homeSsid,
      advancedEnabled: advancedEnabled,
    );

Future<ProviderContainer> _makeContainer({
  String homeSsid = '',
  bool advancedEnabled = false,
}) async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await _seed(ble, homeSsid: homeSsid, advancedEnabled: advancedEnabled);
  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'jacko',
        controlPassword: 'secret',
      ));
  return c;
}

/// Pump frames until the row list has rendered (its lampname subtitle is
/// the most stable visible anchor). ConnectingView runs an infinite
/// animation so pumpAndSettle would never converge.
Future<void> _pumpToData(WidgetTester tester) async {
  for (var i = 0; i < 30; i++) {
    await tester.pump(const Duration(milliseconds: 16));
    if (find.text('jacko').evaluate().isNotEmpty) return;
  }
}

Widget _wrap(ProviderContainer c) => UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        // GoRouter's context.push needs a Router; we don't need one for these
        // row-rendering tests since taps fire onTap callbacks not navigation.
        // The dialog path uses Navigator, which a plain MaterialApp provides.
        home: const Scaffold(body: SetupScreen(lampId: _devId)),
      ),
    );

void main() {
  testWidgets('renders Name row with current lamp name as subtitle',
      (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    expect(find.text('Name'), findsOneWidget);
    expect(find.text('jacko'), findsOneWidget);
  });

  testWidgets('Home Mode row shows "Off" when SSID empty', (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);
    expect(find.text('Home Mode'), findsOneWidget);
    expect(find.text('Off'), findsOneWidget);
  });

  testWidgets('Home Mode row subtitle reflects SSID + brightness once set',
      (tester) async {
    final c = await _makeContainer(homeSsid: 'home');
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);
    // The merged row's subtitle now reads e.g. "home · 60%".
    expect(find.textContaining('home'), findsWidgets);
    expect(find.textContaining('%'), findsOneWidget);
  });

  testWidgets('Advanced LED row hidden by default', (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);
    expect(find.text('Advanced LED setup'), findsNothing);
  });

  testWidgets('Advanced LED row appears when session advanced is enabled',
      (tester) async {
    // Advanced gating moved from persisted firmware advancedEnabled to a
    // session-only Riverpod flag so the unlock doesn't survive a
    // disconnect/reconnect. Flip the session flag directly instead of
    // seeding the firmware bit.
    final c = await _makeContainer();
    c.read(advancedSessionProvider(_devId).notifier).enable();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);
    expect(find.text('Advanced LED setup'), findsOneWidget);
  });

  testWidgets('Knockout row no longer on Setup (moved into LED setup)',
      (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    expect(find.text('Per-pixel knockout'), findsNothing);
  });

  testWidgets('tapping Name row opens a rename dialog that updates the notifier',
      (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    await tester.tap(find.text('Name'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 300));

    await tester.enterText(find.byType(TextField).first, 'foyer');
    await tester.tap(find.text('Save'));
    await tester.pump();

    expect(
      c.read(controlNotifierProvider(_devId)).value!.lamp.name,
      'foyer',
    );
  });

  testWidgets(
      'toggling Home Mode switch off flips enabled but preserves the SSID',
      (tester) async {
    final c = await _makeContainer(homeSsid: 'home');
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    // With homeSsid: 'home', HomeSection.fromJson defaults enabled=true
    // (legacy lamps without the `enabled` field migrate to "ssid present
    // => home mode on"). The switch should reflect that.
    expect(
      c.read(controlNotifierProvider(_devId)).value!.home.enabled,
      isTrue,
    );

    await tester.tap(find.byType(Switch).first);
    await tester.pump();

    final home = c.read(controlNotifierProvider(_devId)).value!.home;
    expect(home.enabled, isFalse);
    // Soft toggle preserves credentials — Forget Network lives inside the
    // Home Mode pane.
    expect(home.ssid, 'home');
  });
}
