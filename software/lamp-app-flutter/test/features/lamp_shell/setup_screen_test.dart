import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/control/application/advanced_session.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/lamp_shell/presentation/setup_screen.dart';

import 'package:lamp_app/core/theme/app_theme.dart';

import '../../_support/seed.dart';

const _devId = 'lamp-x';

Future<ProviderContainer> _makeContainer({
  String homeSsid = '',
  bool advancedEnabled = false,
  bool? hasPassword,
  int basePx = 35,
  int shadePx = 38,
}) async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(
    ble,
    deviceId: _devId,
    homeSsid: homeSsid,
    advancedEnabled: advancedEnabled,
    hasPassword: hasPassword,
    basePx: basePx,
    shadePx: shadePx,
  );
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
        theme: appTheme,
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

  testWidgets('Advanced-gated row hidden by default', (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);
    // LED setup + Setup hotspot are always on; rows gated on advanced
    // (e.g. Factory reset) stay hidden by default.
    expect(find.text('Setup hotspot'), findsOneWidget);
    expect(find.text('Factory reset'), findsNothing);
  });

  testWidgets('Advanced-gated row appears when session advanced is enabled',
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
    await tester.scrollUntilVisible(find.text('Factory reset'), 50);
    expect(find.text('Factory reset'), findsOneWidget);
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

  // --- Password status ---

  testWidgets('Password row shows "Protected" when hasPassword is true',
      (tester) async {
    final c = await _makeContainer(hasPassword: true);
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);
    expect(find.text('Protected'), findsOneWidget);
  });

  testWidgets('Password row shows "Open · no password" when hasPassword is false',
      (tester) async {
    final c = await _makeContainer(hasPassword: false);
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);
    expect(find.text('Open · no password'), findsOneWidget);
  });

  testWidgets('Password row shows no status when hasPassword is null',
      (tester) async {
    final c = await _makeContainer(); // hasPassword defaults to null
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);
    expect(find.text('Protected'), findsNothing);
    expect(find.text('Open · no password'), findsNothing);
  });

  // --- De-jargon ---

  testWidgets('shows "Setup hotspot", not "Boot-time setup AP"', (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);
    expect(find.text('Setup hotspot'), findsOneWidget);
    expect(find.textContaining('Boot-time setup AP'), findsNothing);
  });

  testWidgets('LED subtitle uses px counts, no byteOrder / GRB jargon',
      (tester) async {
    final c = await _makeContainer(basePx: 40, shadePx: 60);
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);
    expect(find.text('Base 40 · Shade 60 LEDs'), findsOneWidget);
    expect(find.textContaining('GRB'), findsNothing);
  });

  // --- Home Mode drill chevron ---

  testWidgets('Home Mode row shows both Switch and chevron_right', (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    final homeModeRow = find.ancestor(
      of: find.text('Home Mode'),
      matching: find.byType(Material),
    ).first;

    expect(find.descendant(of: homeModeRow, matching: find.byType(Switch)),
        findsOneWidget);
    expect(
      find.descendant(
        of: homeModeRow,
        matching: find.byIcon(Icons.chevron_right),
      ),
      findsOneWidget,
    );
  });

  // --- Grouping order ---

  testWidgets('"Setup hotspot" sits under CONNECTIVITY and above LEDS',
      (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    final connectivityDy =
        tester.getTopLeft(find.text('CONNECTIVITY')).dy;
    final hotspotDy = tester.getTopLeft(find.text('Setup hotspot')).dy;
    final ledsDy = tester.getTopLeft(find.text('LEDS')).dy;

    expect(hotspotDy, greaterThan(connectivityDy));
    expect(hotspotDy, lessThan(ledsDy));
  });
}
