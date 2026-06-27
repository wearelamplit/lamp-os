import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/widgets/app_sheet.dart';

void main() {
  testWidgets('showAppSheet shows content and a BottomSheet', (tester) async {
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Builder(builder: (ctx) {
        return TextButton(
          onPressed: () => showAppSheet<void>(ctx,
              builder: (_) => const SizedBox(
                    height: 200,
                    child: Center(child: Text('sheet content')),
                  )),
          child: const Text('open'),
        );
      }),
    ));

    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    expect(find.text('sheet content'), findsOneWidget);
    expect(find.byType(BottomSheet), findsOneWidget);
  });

  testWidgets('showAppSheet swipe-down dismisses the sheet', (tester) async {
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Builder(builder: (ctx) {
        return TextButton(
          onPressed: () => showAppSheet<void>(ctx,
              builder: (_) => const SizedBox(
                    height: 200,
                    child: Center(child: Text('sheet content')),
                  )),
          child: const Text('open'),
        );
      }),
    ));

    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();
    expect(find.text('sheet content'), findsOneWidget);

    await tester.drag(find.byType(BottomSheet), const Offset(0, 300));
    await tester.pumpAndSettle();

    expect(find.text('sheet content'), findsNothing);
  });
}
