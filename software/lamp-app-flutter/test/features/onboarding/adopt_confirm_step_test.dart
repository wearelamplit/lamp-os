import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/features/nearby/application/nearby_lamps_notifier.dart';
import 'package:lamp_app/features/nearby/domain/nearby_lamp.dart';
import 'package:lamp_app/features/onboarding/application/add_lamp_notifier.dart';
import 'package:lamp_app/features/onboarding/domain/add_lamp_state.dart';
import 'package:lamp_app/features/onboarding/presentation/widgets/adopt_confirm_step.dart';

void main() {
  const deviceId = 'lamp-adopt-01';

  final seededLamp = NearbyLamp(
    id: deviceId,
    name: 'Test Lamp',
    rssi: -60,
    serviceUuids: const [],
    baseRgb: 0xFF8000,
    shadeRgb: 0x000000,
    lastSeenEpochMs: DateTime.now().millisecondsSinceEpoch,
    isMesh: true,
    configured: false,
  );

  ProviderContainer makeContainer(InMemoryBleClient ble) => ProviderContainer(
        overrides: [
          bleClientProvider.overrideWithValue(ble),
          nearbyLampsNotifierProvider.overrideWithValue([seededLamp]),
        ],
      );

  Widget wrap(ProviderContainer c) => UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          theme: appTheme,
          home: const Scaffold(body: AdoptConfirmStep()),
        ),
      );

  testWidgets('renders title, body, and Adopt/Cancel buttons', (tester) async {
    final ble = InMemoryBleClient();
    final c = makeContainer(ble);
    addTearDown(c.dispose);
    c.read(addLampNotifierProvider.notifier).select(deviceId);

    await tester.pumpWidget(wrap(c));
    await tester.pump();

    expect(find.text('Found your stray?'), findsOneWidget);
    expect(
      find.text(
        'The one blinking at you is the stray you tapped. Take it in?',
      ),
      findsOneWidget,
    );
    expect(find.text('Adopt'), findsOneWidget);
    expect(find.text('Cancel'), findsOneWidget);
  });

  testWidgets('expressionTest write fires on first frame — pulse started',
      (tester) async {
    final ble = InMemoryBleClient();
    final c = makeContainer(ble);
    addTearDown(c.dispose);
    c.read(addLampNotifierProvider.notifier).select(deviceId);

    await tester.pumpWidget(wrap(c));
    await tester.pump();

    final writes = ble.writesTo(deviceId, BleUuids.expressionTest);
    expect(writes, isNotEmpty);
    final payload =
        jsonDecode(utf8.decode(writes.first)) as Map<String, dynamic>;
    expect(payload['a'], 'test_expression');
    expect(payload['type'], 'pulse');
  });

  testWidgets(
    'Cancel sends complete-write, disconnects, returns to scan',
    (tester) async {
      final ble = InMemoryBleClient();
      final c = makeContainer(ble);
      addTearDown(c.dispose);
      c.read(addLampNotifierProvider.notifier).select(deviceId);

      await tester.pumpWidget(wrap(c));
      await tester.pump();

      await tester.tap(find.text('Cancel'));
      await tester.pump();

      final writes = ble.writesTo(deviceId, BleUuids.expressionTest);
      final last =
          jsonDecode(utf8.decode(writes.last)) as Map<String, dynamic>;
      expect(last['a'], 'test_expression_complete');
      expect(ble.isConnected(deviceId), isFalse);
      expect(c.read(addLampNotifierProvider).step, AddLampStep.scan);
    },
  );

  testWidgets(
    'Adopt sends complete-write, disconnects, advances to name',
    (tester) async {
      final ble = InMemoryBleClient();
      final c = makeContainer(ble);
      addTearDown(c.dispose);
      c.read(addLampNotifierProvider.notifier).select(deviceId);

      await tester.pumpWidget(wrap(c));
      await tester.pump();

      await tester.tap(find.text('Adopt'));
      await tester.pump();

      final writes = ble.writesTo(deviceId, BleUuids.expressionTest);
      final last =
          jsonDecode(utf8.decode(writes.last)) as Map<String, dynamic>;
      expect(last['a'], 'test_expression_complete');
      expect(ble.isConnected(deviceId), isFalse);
      expect(c.read(addLampNotifierProvider).step, AddLampStep.name);
    },
  );
}
