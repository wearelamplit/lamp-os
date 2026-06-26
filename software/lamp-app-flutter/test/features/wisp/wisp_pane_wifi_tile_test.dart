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
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/presentation/wisp_config_screen.dart';

import '../../_support/seed.dart';

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

/// Pump until the WiFi config row has rendered. Mirrors the previous
/// form-test's polling pattern — pumpAndSettle would spin forever on
/// the wisp pane's 1Hz stale-tick timer.
Future<void> _pumpToWifiRow(WidgetTester tester) async {
  // wispNotifier needs an `await watchConnected.firstWhere(true)` to
  // resolve, then the initial readStatus + first state emission, before
  // the aurora-branch wifi row even has a chance to render. 200 frames
  // (~3.2s) is the conservative ceiling for the InMemoryBleClient's
  // microtask scheduling under flutter_test.
  for (var i = 0; i < 200; i++) {
    await tester.pump(const Duration(milliseconds: 16));
    if (find.byKey(const Key('wifi-config-row')).evaluate().isNotEmpty) {
      return;
    }
  }
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
    await tester.pumpWidget(_wrap(c));
    await _pumpToWifiRow(tester);

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
    await tester.pumpWidget(_wrap(c));
    await _pumpToWifiRow(tester);

    expect(find.byKey(const Key('wifi-config-row')), findsOneWidget);
    expect(find.textContaining('Connected'), findsWidgets);
  });

  testWidgets('tapping the WiFi row opens the network picker sheet',
      (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToWifiRow(tester);

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
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
          id: _devId,
          name: 'jacko',
          controlPassword: 'secret',
        ));

    await tester.pumpWidget(_wrap(c));
    await _pumpToWifiRow(tester);

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
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
          id: _devId,
          name: 'jacko',
          controlPassword: 'secret',
        ));

    await tester.pumpWidget(_wrap(c));
    await _pumpToWifiRow(tester);

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

    // wispOp characteristic was never written, so InMemoryBleClient
    // throws BleNotFound on read. That's our "no setWifi shipped"
    // signal — anything else would mean the cancel path silently sent
    // credentials.
    await expectLater(
      () => ble.read(_devId, BleUuids.controlService, BleUuids.wispOp),
      throwsA(isA<BleNotFound>()),
    );
  });

  // Negative tests: the WiFi config row is now Aurora-only. Confirm it's
  // absent under both Off and Manual modes — flipping back to the old
  // "always-rendered" behaviour would silently break this guarantee.
  //
  // The wispNotifier's build() awaits real BLE futures that don't drain
  // under FakeAsync (which testWidgets installs by default), so the
  // existing pump-and-poll pattern at the top of this file races. Use
  // `tester.runAsync` to escape FakeAsync long enough to resolve the
  // notifier's future — then pump the widget tree once, by which point
  // the state is AsyncData and the pane builds synchronously.
  for (final mode in <String>['off', 'manual']) {
    testWidgets('WiFi row is NOT rendered when source=$mode', (tester) async {
      final c = await _makeContainer(
        wispStatusJson:
            '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"$mode"}',
      );
      addTearDown(c.dispose);
      await tester.runAsync(() async {
        await c.read(wispNotifierProvider(_devId).future);
      });

      await tester.pumpWidget(_wrap(c));
      for (var i = 0; i < 4; i++) {
        await tester.pump(const Duration(milliseconds: 16));
      }

      expect(
        find.byKey(const Key('wifi-config-row')),
        findsNothing,
        reason: 'WiFi row must be hidden when wisp.source=$mode '
            '(only Aurora mode needs internet)',
      );
    });
  }
}
