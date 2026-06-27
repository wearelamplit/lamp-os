import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/widgets/form_section.dart';

void main() {
  testWidgets('FormSection renders header, both rows, and one divider', (tester) async {
    await tester.pumpWidget(
      MaterialApp(
        theme: appTheme,
        home: const Scaffold(
          body: FormSection(
            title: 'LEDs',
            children: [Text('a'), Text('b')],
          ),
        ),
      ),
    );

    expect(find.text('LEDS'), findsOneWidget);
    expect(find.text('a'), findsOneWidget);
    expect(find.text('b'), findsOneWidget);
    expect(find.byType(Divider), findsOneWidget);
  });
}
