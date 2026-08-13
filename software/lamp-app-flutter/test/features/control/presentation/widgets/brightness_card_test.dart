import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/widgets/lamp_card.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/presentation/widgets/brightness_card.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../../_support/seed.dart';

const _devId = 'aa:bb:cc:dd:ee:ff';

Future<ProviderContainer> _buildContainer({int brightness = 42}) async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble, deviceId: _devId, brightness: brightness);
  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'jacko',
        controlPassword: 'secret',
      ));
  await c.read(controlNotifierProvider(_devId).future);
  return c;
}

Widget _wrap(ProviderContainer c) => UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        theme: appTheme,
        home: const Scaffold(
          body: BrightnessCard(lampId: _devId),
        ),
      ),
    );

void main() {
  testWidgets('renders inside LampCard with flanking brightness icons',
      (tester) async {
    final c = await _buildContainer(brightness: 42);
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await tester.pumpAndSettle();

    expect(find.byType(LampCard), findsOneWidget);
    expect(find.byIcon(Icons.brightness_low), findsOneWidget);
    expect(find.byIcon(Icons.brightness_high), findsOneWidget);
    expect(find.byType(Slider), findsOneWidget);
  });

  testWidgets('drag commits the released brightness value on release',
      (tester) async {
    final c = await _buildContainer(brightness: 50);
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await tester.pumpAndSettle();

    // Drag far right — clamps deterministically to max value 100.
    await tester.drag(find.byType(Slider), const Offset(10000, 0));
    // Pump past the 500ms commit debounce.
    await tester.pump(const Duration(milliseconds: 600));

    // State reflects the exact released value.
    final brightness =
        c.read(controlNotifierProvider(_devId)).value!.lamp.brightness;
    expect(brightness, equals(100));

    // CHAR_COMMIT was written exactly once — proves scheduleBrightnessCommit
    // fired on release. A broken onChangeEnd path would leave this empty.
    final ble = c.read(bleClientProvider) as InMemoryBleClient;
    final commits = ble.writesTo(_devId, BleUuids.commit);
    expect(commits, hasLength(1));
    expect(commits.first, equals(Uint8List.fromList([0x01])));
  });
}
