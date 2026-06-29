import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/widgets/confirm_discard.dart';

void main() {
  testWidgets('confirmDiscard returns true on Discard, false on Keep editing',
      (tester) async {
    late BuildContext ctx;
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Builder(builder: (c) { ctx = c; return const SizedBox(); }),
    ));

    final discardFuture = confirmDiscard(ctx);
    await tester.pumpAndSettle();
    expect(find.text('Discard changes?'), findsOneWidget);
    await tester.tap(find.text('Discard'));
    await tester.pumpAndSettle();
    expect(await discardFuture, isTrue);

    final keepFuture = confirmDiscard(ctx);
    await tester.pumpAndSettle();
    await tester.tap(find.text('Keep editing'));
    await tester.pumpAndSettle();
    expect(await keepFuture, isFalse);
  });
}
