import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/widgets/lamp_icon.dart';

void main() {
  testWidgets('renders with the requested size', (tester) async {
    await tester.pumpWidget(const MaterialApp(
      home: Scaffold(
        body: LampIcon(
          shade: Color(0xFFEFA3C8),
          base: Color(0xFF446C9C),
          size: 48,
        ),
      ),
    ));
    final box = tester.getSize(find.byType(LampIcon));
    expect(box, const Size(48, 48));
  });

  testWidgets('defaults to slate grey when no colors given', (tester) async {
    await tester.pumpWidget(const MaterialApp(
      home: Scaffold(body: LampIcon(size: 24)),
    ));
    expect(find.byType(LampIcon), findsOneWidget);
  });
}
