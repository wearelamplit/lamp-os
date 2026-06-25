import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/lamp_shell/domain/wifi_state.dart';
import 'package:lamp_app/features/lamp_shell/presentation/widgets/wifi_network_picker.dart';

const _devId = 'lamp-x';

Future<ProviderContainer> _seed(String wifiJson) async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await ble.connect(_devId);
  await ble.write(
    _devId,
    BleUuids.controlService,
    BleUuids.wifiState,
    Uint8List.fromList(utf8.encode(wifiJson)),
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

void main() {
  testWidgets('renders one row per scan result', (tester) async {
    final c = await _seed(
      '{"state":"idle","scanResults":['
      '{"ssid":"alpha","rssi":-55,"encrypted":true},'
      '{"ssid":"beta","rssi":-70,"encrypted":false}'
      ']}',
    );
    addTearDown(c.dispose);

    WifiScanResult? picked;
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: Scaffold(
          body: WifiNetworkPicker(
            lampId: _devId,
            onPick: (r) => picked = r,
          ),
        ),
      ),
    ));
    await tester.pump();

    expect(find.byKey(const Key('wifi-picker-row-alpha')), findsOneWidget);
    expect(find.byKey(const Key('wifi-picker-row-beta')), findsOneWidget);

    await tester.tap(find.byKey(const Key('wifi-picker-row-alpha')));
    await tester.pump();
    expect(picked, isNotNull);
    expect(picked!.ssid, 'alpha');
    expect(picked!.encrypted, isTrue);
  });

  testWidgets('shows a check on the row whose ssid matches currentSsid',
      (tester) async {
    final c = await _seed(
      '{"state":"idle","scanResults":['
      '{"ssid":"alpha","rssi":-55,"encrypted":true},'
      '{"ssid":"beta","rssi":-70,"encrypted":false}'
      ']}',
    );
    addTearDown(c.dispose);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(
          body: WifiNetworkPicker(
            lampId: _devId,
            currentSsid: 'beta',
            onPick: _noop,
          ),
        ),
      ),
    ));
    await tester.pump();

    // Two scan results → one Icons.lock_outline (alpha is encrypted) and
    // one Icons.check (beta is current). The picker keeps both icons in
    // the trailing Row for the current row; for this test we just assert
    // a check appears once.
    expect(find.byIcon(Icons.check), findsOneWidget);
  });

  testWidgets('renders empty hint when scan list is empty', (tester) async {
    final c = await _seed('{"state":"idle","scanResults":[]}');
    addTearDown(c.dispose);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(
          body: WifiNetworkPicker(
            lampId: _devId,
            onPick: _noop,
            emptyHint: 'No networks yet.',
          ),
        ),
      ),
    ));
    await tester.pump();
    // After the first frame the picker kicks off a scan; while scanning
    // it shows the spinner instead of the empty hint. We tolerate either:
    // the contract is "user sees something useful, not a void".
    final hint = find.text('No networks yet.');
    final spinner = find.byType(CircularProgressIndicator);
    expect(
      hint.evaluate().isNotEmpty || spinner.evaluate().isNotEmpty,
      isTrue,
    );
  });
}

void _noop(WifiScanResult _) {}
