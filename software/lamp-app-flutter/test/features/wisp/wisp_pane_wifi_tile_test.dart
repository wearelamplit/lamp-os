import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client.dart';
import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/social/application/lamp_nearby_peers_notifier.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/presentation/wisp_config_screen.dart';

import '../../_support/seed.dart';

// Stub that returns an empty peer list without starting the 1Hz polling
// timer, so the FakeAsync zone in testWidgets stays timer-clean.
class _FakeLampNearbyPeers extends LampNearbyPeersNotifier {
  @override
  Future<List<LampNearbyPeer>> build(String lampId) async => const [];
}

const _devId = 'lamp-x';

Future<ProviderContainer> _makeContainer({
  String wispStatusJson = '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"aurora"}',
  String wifiStateJson =
      '{"state":"idle","scanResults":[{"ssid":"homenet","rssi":-55,"encrypted":true}]}',
}) async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble, deviceId: _devId);
  await ble.connect(_devId);
  await ble.write(
    _devId,
    BleUuids.controlService,
    BleUuids.wispStatus,
    Uint8List.fromList(utf8.encode(wispStatusJson)),
  );
  await ble.write(
    _devId,
    BleUuids.controlService,
    BleUuids.wifiState,
    Uint8List.fromList(utf8.encode(wifiStateJson)),
  );
  final c = ProviderContainer(
    overrides: [
      bleClientProvider.overrideWithValue(ble),
      lampNearbyPeersNotifierProvider(_devId)
          .overrideWith(() => _FakeLampNearbyPeers()),
    ],
  );
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'jacko',
        controlPassword: 'secret',
      ));
  return c;
}

/// Resolve the wispNotifier's build() future in the real-async zone before
/// building the widget tree. The notifier awaits real BLE futures that
/// never drain under FakeAsync; resolving via `c.read(...future)` inside
/// runAsync first lets it reach AsyncData, then pumpWidget builds from cache.
///
/// Lands on Palette source tab (default). Call [_tapSettingsTab] to reach
/// the Settings tab where wifi-config-row now lives.
Future<void> _pumpScreen(WidgetTester tester, ProviderContainer c) async {
  await tester.runAsync(() async {
    await c.read(wispNotifierProvider(_devId).future);
  });
  await tester.pumpWidget(_wrap(c));
  // Wait for the tab bar to render (Palette source is the default tab).
  for (var i = 0; i < 20; i++) {
    await tester.pump(const Duration(milliseconds: 16));
    if (find.text('Palette source').evaluate().isNotEmpty) break;
  }
}

Future<void> _tapSettingsTab(WidgetTester tester) async {
  await tester.tap(find.text('Settings'));
  await tester.pumpAndSettle();
}

Widget _wrap(ProviderContainer c) => UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(body: WispConfigScreen(lampId: _devId)),
      ),
    );

void main() {
  testWidgets(
      'renders the WiFi row with "Not connected" subtitle when wifiConnected=false',
      (tester) async {
    final c = await _makeContainer(
      wispStatusJson:
          '{"wispMac":"AA:BB:CC:DD:EE:FF","wifiConnected":false,"source":"aurora"}',
    );
    addTearDown(c.dispose);
    await _pumpScreen(tester, c);
    await _tapSettingsTab(tester);

    expect(find.byKey(const Key('wifi-config-row')), findsOneWidget);
    expect(find.text('WiFi'), findsOneWidget);
    expect(
      find.textContaining('Not connected'),
      findsOneWidget,
    );
  });

  testWidgets(
      'renders the WiFi row with "Connected" subtitle when wifiConnected=true',
      (tester) async {
    final c = await _makeContainer(
      wispStatusJson:
          '{"wispMac":"AA:BB:CC:DD:EE:FF","wifiConnected":true,"source":"aurora"}',
    );
    addTearDown(c.dispose);
    await _pumpScreen(tester, c);
    await _tapSettingsTab(tester);

    expect(find.byKey(const Key('wifi-config-row')), findsOneWidget);
    expect(find.textContaining('Connected'), findsWidgets);
  });

  testWidgets('tapping the WiFi row opens the network picker sheet',
      (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await _pumpScreen(tester, c);
    await _tapSettingsTab(tester);

    await tester.tap(find.byKey(const Key('wifi-config-row')));
    // Give the modal a couple of frames to slide in.
    for (var i = 0; i < 20; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }

    // Sheet header + at least one scanned-network row.
    expect(find.text('WiFi networks'), findsOneWidget);
    expect(
      find.byKey(const Key('wifi-picker-row-homenet')),
      findsOneWidget,
    );
  });

  testWidgets(
      'picking a network + entering a password ships a setWifi wispOp',
      (tester) async {
    final ble = InMemoryBleClient();
    await seedControlBle(ble, deviceId: _devId);
    await ble.connect(_devId);
    await ble.write(
      _devId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      Uint8List.fromList(utf8.encode('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"aurora"}')),
    );
    await ble.write(
      _devId,
      BleUuids.controlService,
      BleUuids.wifiState,
      Uint8List.fromList(utf8.encode(
          '{"state":"idle","scanResults":[{"ssid":"homenet","rssi":-55,"encrypted":true}]}')),
    );
    SharedPreferences.setMockInitialValues({});
    final c = ProviderContainer(
      overrides: [
        bleClientProvider.overrideWithValue(ble),
        lampNearbyPeersNotifierProvider(_devId)
            .overrideWith(() => _FakeLampNearbyPeers()),
      ],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
          id: _devId,
          name: 'jacko',
          controlPassword: 'secret',
        ));

    await _pumpScreen(tester, c);
    await _tapSettingsTab(tester);

    // Open the picker.
    await tester.tap(find.byKey(const Key('wifi-config-row')));
    for (var i = 0; i < 20; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }

    // Pick the network → password dialog should appear.
    await tester.tap(find.byKey(const Key('wifi-picker-row-homenet')));
    for (var i = 0; i < 20; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }

    expect(find.byKey(const Key('password-prompt-field')), findsOneWidget);
    await tester.enterText(
        find.byKey(const Key('password-prompt-field')), 'sup3rs3cret');
    await tester.tap(find.byKey(const Key('password-prompt-confirm')));
    // Pump the BLE write + the setState that flips _busy back.
    for (var i = 0; i < 20; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }

    final written = await ble.read(
      _devId,
      BleUuids.controlService,
      BleUuids.wispOp,
    );
    final decoded =
        jsonDecode(utf8.decode(written)) as Map<String, dynamic>;
    expect(decoded['char'], 'wispOp');
    expect(decoded['op'], 'setWifi');
    expect(decoded['ssid'], 'homenet');
    expect(decoded['pw'], 'sup3rs3cret');
  });

  testWidgets('cancelling the password dialog does NOT ship a wispOp',
      (tester) async {
    final ble = InMemoryBleClient();
    await seedControlBle(ble, deviceId: _devId);
    await ble.connect(_devId);
    await ble.write(
      _devId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      Uint8List.fromList(utf8.encode('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"aurora"}')),
    );
    await ble.write(
      _devId,
      BleUuids.controlService,
      BleUuids.wifiState,
      Uint8List.fromList(utf8.encode(
          '{"state":"idle","scanResults":[{"ssid":"homenet","rssi":-55,"encrypted":true}]}')),
    );
    SharedPreferences.setMockInitialValues({});
    final c = ProviderContainer(
      overrides: [
        bleClientProvider.overrideWithValue(ble),
        lampNearbyPeersNotifierProvider(_devId)
            .overrideWith(() => _FakeLampNearbyPeers()),
      ],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
          id: _devId,
          name: 'jacko',
          controlPassword: 'secret',
        ));

    await _pumpScreen(tester, c);
    await _tapSettingsTab(tester);

    await tester.tap(find.byKey(const Key('wifi-config-row')));
    for (var i = 0; i < 20; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }
    await tester.tap(find.byKey(const Key('wifi-picker-row-homenet')));
    for (var i = 0; i < 20; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }
    await tester.tap(find.byKey(const Key('password-prompt-cancel')));
    for (var i = 0; i < 20; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }

    // The screen's open-time pollStatus may have written to wispOp, so
    // assert on op names: a setWifi write here would mean the cancel path
    // silently sent credentials.
    final ops = ble
        .writesTo(_devId, BleUuids.wispOp)
        .map((w) => (jsonDecode(utf8.decode(w)) as Map<String, dynamic>)['op'])
        .toList();
    expect(ops, isNot(contains('setWifi')));
  });

  // WiFi is now on the Settings tab unconditionally; confirm it appears there
  // regardless of source mode.
  for (final mode in <String>['off', 'manual']) {
    testWidgets('WiFi row is on Settings tab when source=$mode', (tester) async {
      final c = await _makeContainer(
        wispStatusJson:
            '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"$mode"}',
      );
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);
      await _tapSettingsTab(tester);

      expect(
        find.byKey(const Key('wifi-config-row')),
        findsOneWidget,
        reason: 'WiFi row lives on Settings tab regardless of source mode',
      );
    });
  }
}
