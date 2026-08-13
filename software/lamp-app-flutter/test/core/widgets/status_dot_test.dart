import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/widgets/status_dot.dart';

void main() {
  testWidgets('renders for each StatusKind', (tester) async {
    for (final kind in StatusKind.values) {
      await tester.pumpWidget(MaterialApp(
        home: Scaffold(body: StatusDot(kind: kind)),
      ));
      expect(find.byType(StatusDot), findsOneWidget);
      await tester.pump(const Duration(milliseconds: 250));
    }
  });
}
