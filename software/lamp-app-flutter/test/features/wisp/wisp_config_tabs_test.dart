import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/social/application/lamp_nearby_peers_notifier.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/presentation/wisp_config_screen.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';

class _FakePeers extends LampNearbyPeersNotifier {
  @override
  Future<List<LampNearbyPeer>> build(String lampId) async => const [];
}

const _devId = 'lamp-tab-test';

Future<ProviderContainer> _makeContainer({
  String wispJson = '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual"}',
}) async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble, deviceId: _devId);
  await ble.connect(_devId);
  await ble.write(
    _devId,
    BleUuids.controlService,
    BleUuids.wispStatus,
    Uint8List.fromList(utf8.encode(wispJson)),
  );
  final c = ProviderContainer(
    overrides: [
      bleClientProvider.overrideWithValue(ble),
      lampNearbyPeersNotifierProvider(_devId)
          .overrideWith(() => _FakePeers()),
    ],
  );
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'testlamp',
        controlPassword: 'secret',
      ));
  return c;
}

Widget _wrap(ProviderContainer c) => UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(body: WispConfigScreen(lampId: _devId)),
      ),
    );

Future<void> _pumpScreen(WidgetTester tester, ProviderContainer c) async {
  await tester.runAsync(() async {
    await c.read(wispNotifierProvider(_devId).future);
  });
  await tester.pumpWidget(_wrap(c));
  // Pump until the tab bar appears, up to 20 frames.
  for (var i = 0; i < 20; i++) {
    await tester.pump(const Duration(milliseconds: 16));
    if (find.text('Palette source').evaluate().isNotEmpty) return;
  }
}

void main() {
  group('WispConfigScreen tabs', () {
    testWidgets('renders Palette source, Settings, and Lamps tabs in order',
        (tester) async {
      final c = await _makeContainer();
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);

      expect(find.text('Palette source'), findsOneWidget);
      expect(find.text('Settings'), findsOneWidget);
      expect(find.text('Lamps'), findsOneWidget);

      // Verify order: Palette source comes before Settings, Settings before Lamps.
      final sourcesPos = tester.getCenter(find.text('Palette source')).dx;
      final settingsPos = tester.getCenter(find.text('Settings')).dx;
      final lampsPos = tester.getCenter(find.text('Lamps')).dx;
      expect(sourcesPos, lessThan(settingsPos));
      expect(settingsPos, lessThan(lampsPos));
    });

    testWidgets('default tab is Palette source — source picker visible on open',
        (tester) async {
      final c = await _makeContainer();
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);

      // SourcePicker pills visible without any tab tap.
      expect(find.text('Manual'), findsOneWidget);
      expect(find.text('Off'), findsOneWidget);
      expect(find.text('Aurora'), findsOneWidget);
    });

    testWidgets('Palette source tab shows per-mode widget (manual palette editor)',
        (tester) async {
      final c = await _makeContainer();
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);

      // Still on Palette source tab (default); manual source is active in seed JSON.
      expect(find.text('MANUAL PALETTE'), findsOneWidget);
    });

    testWidgets('Settings tab shows name field, password field, and drift',
        (tester) async {
      final c = await _makeContainer();
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);

      await tester.tap(find.text('Settings'));
      await tester.pumpAndSettle();

      // Name row (opens the shared rename dialog on tap)
      expect(find.byKey(const Key('wisp-name-row')), findsOneWidget);
      // Password row (SettingsRow from WispPasswordField)
      expect(find.text('No password'), findsOneWidget);
      // DriftControls renders a 'Color drift' section header. The persistent
      // space-brightness footer shortens the tab, so scroll it into view.
      await tester.ensureVisible(find.text('NETWORK'));
      await tester.pumpAndSettle();
      await tester.ensureVisible(find.text('COLOR DRIFT'));
      await tester.pumpAndSettle();
      expect(find.text('COLOR DRIFT'), findsOneWidget);
    });

    testWidgets('Lamps tab shows PaintedLampsList', (tester) async {
      final c = await _makeContainer();
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);

      await tester.tap(find.text('Lamps'));
      await tester.pumpAndSettle();

      // PaintedLampsList renders the empty-state message when no lamps claimed.
      expect(find.textContaining('No lamps claimed'), findsOneWidget);
    });

    testWidgets('source picker is NOT on Settings tab', (tester) async {
      final c = await _makeContainer();
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);

      await tester.tap(find.text('Settings'));
      await tester.pumpAndSettle();

      // Source picker pills should not appear on the Settings tab.
      expect(find.text('Manual'), findsNothing);
      expect(find.text('Off'), findsNothing);
      expect(find.text('Aurora'), findsNothing);
    });

    testWidgets('DriftControls does NOT appear on Palette source or Lamps tabs',
        (tester) async {
      final c = await _makeContainer();
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);

      // On the Palette source tab by default — no drift.
      expect(find.text('COLOR DRIFT'), findsNothing);

      await tester.tap(find.text('Lamps'));
      await tester.pumpAndSettle();

      // On the Lamps tab — still no drift.
      expect(find.text('COLOR DRIFT'), findsNothing);
    });

    testWidgets('Settings tab scrolls to reveal Network section and drift',
        (tester) async {
      final c = await _makeContainer();
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);

      await tester.tap(find.text('Settings'));
      await tester.pumpAndSettle();

      // The Settings tab is taller now; use ensureVisible to scroll each
      // section into view rather than a fixed-pixel drag.
      await tester.ensureVisible(find.text('NETWORK'));
      await tester.pumpAndSettle();
      expect(find.text('NETWORK'), findsOneWidget);

      await tester.ensureVisible(find.text('COLOR DRIFT'));
      await tester.pumpAndSettle();
      expect(find.text('COLOR DRIFT'), findsOneWidget);

      // Scroll back up to verify the name row is still in the tree.
      await tester.drag(find.byType(ListView).last, const Offset(0, 800));
      await tester.pumpAndSettle();

      expect(find.byKey(const Key('wisp-name-row')), findsOneWidget);
      expect(find.text('No password'), findsOneWidget);
    });
  });

  group('WispPasswordField validation', () {
    testWidgets(
        'when hasPassword=true and current-password field is blank, Clear shows a validation message',
        (tester) async {
      final c = await _makeContainer(
        wispJson:
            '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual","hasPassword":true}',
      );
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);

      await tester.tap(find.text('Settings'));
      await tester.pumpAndSettle();

      await tester.ensureVisible(find.text('Password set'));
      await tester.pumpAndSettle();

      expect(find.text('Password set'), findsOneWidget);
      await tester.tap(find.text('Password set'));
      await tester.pumpAndSettle();

      // Dialog is open; leave the current-password field blank.
      expect(find.byType(AlertDialog), findsOneWidget);

      // Tap Clear with blank current password.
      await tester.tap(find.text('Clear'));
      await tester.pumpAndSettle();

      // Validation message should appear; dialog stays open.
      expect(find.text('Enter the current password'), findsOneWidget);
      expect(find.byType(AlertDialog), findsOneWidget);
    });

    testWidgets(
        'when hasPassword=true and current-password field is blank, Save shows a validation message',
        (tester) async {
      final c = await _makeContainer(
        wispJson:
            '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual","hasPassword":true}',
      );
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);

      await tester.tap(find.text('Settings'));
      await tester.pumpAndSettle();

      // Scroll to reveal the password row.
      await tester.ensureVisible(find.text('Password set'));
      await tester.pumpAndSettle();

      // Tap the password row to open the dialog.
      expect(find.text('Password set'), findsOneWidget);
      await tester.tap(find.text('Password set'));
      await tester.pumpAndSettle();

      // Dialog is open; fill in a new password but leave current blank.
      expect(find.byType(AlertDialog), findsOneWidget);
      await tester.enterText(find.widgetWithText(TextField, 'New password'), 'newpass');
      await tester.pumpAndSettle();

      // Tap Save with blank current password.
      await tester.tap(find.text('Save'));
      await tester.pumpAndSettle();

      // Validation message should appear instead of dismissing the dialog.
      expect(find.text('Enter the current password'), findsOneWidget);
      // Dialog is still open.
      expect(find.byType(AlertDialog), findsOneWidget);
    });
  });
}
