import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/features/onboarding/application/add_lamp_notifier.dart';
import 'package:lamp_app/features/onboarding/presentation/widgets/add_lamp_password_step.dart';

void main() {
  ProviderContainer makeContainer() => ProviderContainer(
        overrides: [bleClientProvider.overrideWithValue(InMemoryBleClient())],
      );

  Widget wrap(ProviderContainer c) => UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          theme: appTheme,
          home: const Scaffold(body: AddLampPasswordStep()),
        ),
      );

  testWidgets('submit button label interpolates name as Take {name} home',
      (tester) async {
    final c = makeContainer();
    addTearDown(c.dispose);

    final notifier = c.read(addLampNotifierProvider.notifier);
    notifier.select('test-lamp');
    notifier.next(); // adoptConfirm → name
    notifier.setName('Sparky');
    notifier.next(); // name → password

    await tester.pumpWidget(wrap(c));
    await tester.pump();

    expect(find.text('Take Sparky home'), findsOneWidget);
    expect(find.text('Welcome them home'), findsNothing);
  });

  testWidgets('hides password fields and shows Settling in… during verifying',
      (tester) async {
    final c = makeContainer();
    addTearDown(c.dispose);

    final notifier = c.read(addLampNotifierProvider.notifier);
    notifier.select('test-lamp');
    notifier.next(); // adoptConfirm → name
    notifier.next(); // name → password
    notifier.next(); // password → verifying

    await tester.pumpWidget(wrap(c));
    await tester.pump();

    expect(find.byType(TextField), findsNothing);
    expect(find.text('Settling in…'), findsOneWidget);
  });
}
