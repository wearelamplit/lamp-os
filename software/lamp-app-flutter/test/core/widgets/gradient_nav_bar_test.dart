import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/widgets/gradient_nav_bar.dart';

void main() {
  testWidgets('renders a badge on the destination that has one', (
    tester,
  ) async {
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          bottomNavigationBar: GradientNavBar(
            selectedIndex: 0,
            onDestinationSelected: (_) {},
            destinations: const [
              (icon: Icons.palette_outlined, label: 'Colors', badge: null),
              (
                icon: Icons.info_outline,
                label: 'Info',
                badge: Text('✨', key: Key('sparkle')),
              ),
            ],
          ),
        ),
      ),
    );
    expect(find.byKey(const Key('sparkle')), findsOneWidget);
  });
}
