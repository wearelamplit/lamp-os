import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart' as fbp;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/bluetooth_gate.dart';

Future<void> _pumpGate(
  WidgetTester tester,
  fbp.BluetoothAdapterState state,
) {
  return tester.pumpWidget(
    ProviderScope(
      overrides: [
        bluetoothAdapterStateProvider.overrideWith((ref) => Stream.value(state)),
      ],
      child: const MaterialApp(
        home: BluetoothGate(child: Text('lamp screen')),
      ),
    ),
  );
}

void main() {
  testWidgets('adapter on passes the child through', (tester) async {
    await _pumpGate(tester, fbp.BluetoothAdapterState.on);
    await tester.pump();
    expect(find.text('lamp screen'), findsOneWidget);
    expect(find.text('Bluetooth is off'), findsNothing);
  });

  testWidgets('adapter off raises the BT-off panel over the child',
      (tester) async {
    await _pumpGate(tester, fbp.BluetoothAdapterState.off);
    await tester.pump();
    expect(find.text('Bluetooth is off'), findsOneWidget);
    expect(find.text('lamp screen'), findsOneWidget);
  });

  testWidgets('adapter unauthorized raises the permission panel',
      (tester) async {
    await _pumpGate(tester, fbp.BluetoothAdapterState.unauthorized);
    await tester.pump();
    expect(find.text('Bluetooth permission needed'), findsOneWidget);
    expect(find.text('Open Settings'), findsOneWidget);
  });
}
