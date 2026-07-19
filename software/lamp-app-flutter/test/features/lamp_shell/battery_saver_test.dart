import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/lamp_crypto.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/lamp_shell/presentation/advanced_leds_screen.dart';

const _devId = 'lamp-x';

Future<ProviderContainer> _makeContainer(InMemoryBleClient ble) async {
  SharedPreferences.setMockInitialValues({});
  // No control password: settings_blob writes land plaintext (magic + JSON)
  // so the test can decode the recorded write and assert its contents.
  await seedControlBle(
    ble,
    deviceId: _devId,
    brightnessCeiling: 170,
    drawIdleMa: 120,
    drawFullMa: 2400,
  );
  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'jacko',
      ));
  return c;
}

Future<void> _pumpToData(WidgetTester tester) async {
  for (var i = 0; i < 30; i++) {
    await tester.pump(const Duration(milliseconds: 16));
    if (find.text('SHADE STRIP').evaluate().isNotEmpty) return;
  }
}

Widget _wrap(ProviderContainer c) => UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(home: AdvancedLedsScreen(lampId: _devId)),
    );

String _estimateText(WidgetTester tester) =>
    tester.widget<Text>(find.textContaining('20 Ah battery')).data!;

void main() {
  testWidgets('Battery Saver estimate renders and updates on Bright',
      (tester) async {
    final ble = InMemoryBleClient();
    final c = await _makeContainer(ble);
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    // Default Standard (170) shows a non-empty estimate caption.
    expect(find.textContaining('20 Ah battery'), findsOneWidget);
    final standardEstimate = _estimateText(tester);

    await tester.tap(find.text('Bright'));
    for (var i = 0; i < 10; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }

    // Caption re-interpolates to the brighter ceiling (shorter runtime).
    final brightEstimate = _estimateText(tester);
    expect(brightEstimate, isNot(standardEstimate));

    // The notifier committed a plaintext settings_blob write to the lamp.
    final writes = ble.writesTo(_devId, BleUuids.settingsBlob);
    expect(writes, isNotEmpty);
    final write = writes.last;
    expect(write[0], LampCrypto.magicPlaintext);
    final json =
        jsonDecode(utf8.decode(write.sublist(1))) as Map<String, dynamic>;
    expect((json['lamp'] as Map<String, dynamic>)['brightnessCeiling'], 230);
  });

  group('batteryEstimateLabel', () {
    test('interpolates sensibly for a known triple', () {
      // 200 + (1400-200)*170/255 = 1000 mA. 20000/1000 = 20.0 h.
      final label = batteryEstimateLabel(200, 1400, 170);
      expect(label, contains('20 Ah battery'));
      expect(label, isNotEmpty);
    });

    test('higher ceiling yields a shorter runtime', () {
      double hours(String label) =>
          double.parse(RegExp(r'~([\d.]+) h').firstMatch(label)!.group(1)!);
      final dim = batteryEstimateLabel(200, 1400, 120);
      final bright = batteryEstimateLabel(200, 1400, 230);
      expect(hours(bright), lessThan(hours(dim)));
    });

    test('returns empty when full draw does not exceed idle', () {
      expect(batteryEstimateLabel(1400, 1400, 170), '');
      expect(batteryEstimateLabel(1400, 200, 170), '');
    });
  });
}
