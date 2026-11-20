import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/features/control/application/lamp_busy_exception.dart';
import 'package:lamp_app/features/control/presentation/lamp_error_page.dart';

Widget _page(Object error) => ProviderScope(
  child: MaterialApp(
    home: Scaffold(
      body: LampErrorPage(lampId: 'lamp-x', error: error),
    ),
  ),
);

void main() {
  testWidgets('a busy lamp reads as low on memory, with a retry', (
    tester,
  ) async {
    await tester.pumpWidget(_page(const LampBusyException()));
    expect(find.text('Your lamp is low on memory.'), findsOneWidget);
    expect(find.text("Couldn't reach your lamp."), findsNothing);
    expect(find.text('Try again'), findsOneWidget);
  });

  testWidgets('any other failure reads as out of reach, with a retry', (
    tester,
  ) async {
    await tester.pumpWidget(_page(const BleNotConnected('lamp-x')));
    expect(find.text("Couldn't reach your lamp."), findsOneWidget);
    expect(find.text('Your lamp is low on memory.'), findsNothing);
    expect(find.text('Try again'), findsOneWidget);
  });
}
