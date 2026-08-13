import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/widgets/lamp_chip.dart';
import 'package:lamp_app/core/widgets/status_dot.dart';

void main() {
  testWidgets('renders name and fires onTap', (tester) async {
    var taps = 0;
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: LampChip(
          name: 'jacko',
          status: StatusKind.mesh,
          onTap: () => taps++,
        ),
      ),
    ));
    expect(find.text('jacko'), findsOneWidget);
    expect(find.byType(StatusDot), findsOneWidget);
    await tester.tap(find.byType(LampChip));
    expect(taps, 1);
  });
}
