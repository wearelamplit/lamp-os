import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/widgets/section_header.dart';

void main() {
  testWidgets('renders label uppercased', (tester) async {
    await tester.pumpWidget(
      MaterialApp(
        theme: appTheme,
        home: const Scaffold(body: SectionHeader('Personality')),
      ),
    );
    expect(find.text('PERSONALITY'), findsOneWidget);
  });
}
