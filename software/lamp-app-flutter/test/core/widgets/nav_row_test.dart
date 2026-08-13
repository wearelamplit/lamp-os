import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/widgets/nav_row.dart';

void main() {
  testWidgets('renders title, subtitle, chevron, and fires onTap', (tester) async {
    var tapped = false;
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Scaffold(
        body: NavRow(
          icon: Icons.wifi,
          title: 'WiFi',
          subtitle: 'Off',
          onTap: () => tapped = true,
        ),
      ),
    ));
    expect(find.text('WiFi'), findsOneWidget);
    expect(find.text('Off'), findsOneWidget);
    expect(find.byIcon(Icons.chevron_right), findsOneWidget);
    await tester.tap(find.byType(ListTile));
    await tester.pump();
    expect(tapped, isTrue);
  });
}
