import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/features/nearby/application/nearby_lamps_notifier.dart';
import 'package:lamp_app/features/onboarding/application/add_lamp_notifier.dart';
import 'package:lamp_app/features/onboarding/presentation/widgets/add_lamp_done_step.dart';

void main() {
  ProviderContainer makeContainer() {
    SharedPreferences.setMockInitialValues({});
    return ProviderContainer(
      overrides: [
        bleClientProvider.overrideWithValue(InMemoryBleClient()),
        // Empty nearby list → isMesh = false → BT-only path (no mesh card).
        // The lamp isn't in range here; we're just checking copy strings.
        nearbyLampsNotifierProvider.overrideWithValue(const []),
      ],
    );
  }

  Widget wrap(ProviderContainer c) => UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          theme: appTheme,
          home: const Scaffold(body: AddLampDoneStep()),
        ),
      );

  /// Advance the notifier to the done step with the given name via public
  /// state-machine transitions — no BLE required.
  void advanceToDone(ProviderContainer c, String name) {
    final n = c.read(addLampNotifierProvider.notifier);
    n.select('test-id');
    n.next(); // adoptConfirm → name
    n.setName(name);
    n.next(); // name → password
    n.next(); // password → verifying
    n.next(); // verifying → done
  }

  testWidgets('section header reads Getting to know {name}', (tester) async {
    final c = makeContainer();
    addTearDown(c.dispose);
    advanceToDone(c, 'Sparky');

    await tester.pumpWidget(wrap(c));
    await tester.pump();

    expect(find.text('Getting to know Sparky'), findsOneWidget);
    expect(find.textContaining('First moves with'), findsNothing);
  });

  testWidgets('CTA reads Say hi to {name}', (tester) async {
    final c = makeContainer();
    addTearDown(c.dispose);
    advanceToDone(c, 'Sparky');

    await tester.pumpWidget(wrap(c));
    await tester.pump();

    expect(find.text('Say hi to Sparky'), findsOneWidget);
  });
}
