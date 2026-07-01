import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/features/control/presentation/widgets/recolored_critter.dart';
import 'package:lamp_app/features/inventory/application/active_lamp_notifier.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/onboarding/application/add_lamp_notifier.dart';
import 'package:lamp_app/features/onboarding/presentation/widgets/add_lamp_meet_step.dart';
import 'package:shared_preferences/shared_preferences.dart';

// State transitions (working → ready → done, wrongPassword, connectFailed) are
// covered in add_lamp_notifier_test. This asserts the pane renders the ready
// state: welcome copy, recolored critter, and an enabled Continue.
void main() {
  setUp(() {
    SharedPreferences.setMockInitialValues({});
    AddLampNotifier.verifyDelay = Duration.zero;
    AddLampNotifier.verifySkipDelay = Duration.zero;
    AddLampNotifier.reconnectSettleDelay = Duration.zero;
    AddLampNotifier.reconnectBackoff = Duration.zero;
  });
  tearDown(() {
    AddLampNotifier.verifyDelay = const Duration(seconds: 5);
    AddLampNotifier.verifySkipDelay = const Duration(seconds: 2);
    AddLampNotifier.reconnectSettleDelay = const Duration(milliseconds: 500);
    AddLampNotifier.reconnectBackoff = const Duration(milliseconds: 1500);
  });

  testWidgets('ready pane shows welcome copy, critter, and enabled Continue',
      (tester) async {
    final ble = InMemoryBleClient();
    ble.seedSection(
      'dev1',
      'lamp',
      Uint8List.fromList(utf8.encode('{"name":"jacko"}')),
    );
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(activeLampNotifierProvider.future);

    final n = c.read(addLampNotifierProvider.notifier);
    n.select('dev1', baseRgb: 0xFF8000, shadeRgb: 0x2244FF);
    n.setName('jacko');
    n.setPassword('secret');
    // Drive the claim + background reconnect under real async — verifyDone
    // awaits Timer-based delays/timeouts that never fire under the widget
    // tester's fake clock without a pump.
    await tester.runAsync(() async {
      await n.submit();
      await n.verifyDone;
    });

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          theme: appTheme,
          home: const Scaffold(body: AddLampMeetStep()),
        ),
      ),
    );
    await tester.pump();

    expect(find.text('Welcome home, jacko.'), findsOneWidget);
    expect(find.byType(RecoloredCritter), findsOneWidget);
    final continueBtn = find.widgetWithText(FilledButton, 'Continue');
    expect(continueBtn, findsOneWidget);
    expect(tester.widget<FilledButton>(continueBtn).onPressed, isNotNull);
  });
}
