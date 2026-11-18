import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/ble_scanner.dart';
import 'package:lamp_app/features/control/application/dev_mode.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/lamp_shell/presentation/info_screen.dart';
import 'package:lamp_app/features/nearby/application/nearby_lamps_notifier.dart';

import '../../../_support/in_memory_ble_client.dart';
import '../../../_support/seed.dart';

const _lampId = 'lamp-x';

Future<void> _pumpInfo(WidgetTester tester, {String appCommit = ''}) async {
  PackageInfo.setMockInitialValues(
    appName: 'lamp_app',
    packageName: 'ca.lamplit.lamp_app',
    version: '7.8.9',
    buildNumber: '4242',
    buildSignature: '',
  );
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble, deviceId: _lampId, name: 'jacko', brightness: 50);
  final c = ProviderContainer(
    overrides: [
      bleClientProvider.overrideWithValue(ble),
      bleScannerProvider.overrideWithValue(FakeBleScanner()),
      devModeOnProvider.overrideWithValue(false),
      appCommitProvider.overrideWithValue(appCommit),
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
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(body: InfoScreen(lampId: _lampId)),
      ),
    ),
  );
  for (var i = 0; i < 30; i++) {
    await tester.pump(const Duration(milliseconds: 50));
  }
}

void main() {
  testWidgets('the Info tab shows the app version and build number', (
    tester,
  ) async {
    await _pumpInfo(tester);
    expect(find.text('App 7.8.9+4242'), findsOneWidget);
  });

  testWidgets('the Info tab names the commit when the build carries one', (
    tester,
  ) async {
    await _pumpInfo(tester, appCommit: 'abc1234-dirty');
    expect(find.text('App 7.8.9+4242 (abc1234-dirty)'), findsOneWidget);
  });
}
