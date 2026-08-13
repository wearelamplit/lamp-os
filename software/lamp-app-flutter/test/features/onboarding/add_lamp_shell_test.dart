import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/ble_scanner.dart';
import 'package:lamp_app/features/nearby/application/nearby_lamps_notifier.dart';
import 'package:lamp_app/features/onboarding/presentation/add_lamp_shell.dart';

void main() {
  testWidgets('AddLampShell renders Scan step initially', (tester) async {
    final ble = InMemoryBleClient();
    final scanner = FakeBleScanner();
    await tester.pumpWidget(
      ProviderScope(
        overrides: [
          bleClientProvider.overrideWithValue(ble),
          bleScannerProvider.overrideWithValue(scanner),
        ],
        child: const MaterialApp(home: AddLampShell()),
      ),
    );
    await tester.pump();
    expect(find.text('Searching for a stray lamp…'), findsOneWidget);
  });
}
