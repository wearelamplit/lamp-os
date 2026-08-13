import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/widgets/lamp_card.dart';

void main() {
  testWidgets('LampCard renders child on carbon surface', (t) async {
    await t.pumpWidget(MaterialApp(
      theme: appTheme,
      home: const Scaffold(body: LampCard(child: Text('hi'))),
    ));
    expect(find.text('hi'), findsOneWidget);
    expect(find.byType(Card), findsOneWidget);
  });
}
