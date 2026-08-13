import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:lamp_app/core/widgets/password_prompt_dialog.dart';

/// Mounts an Open button that calls [showPasswordPromptDialog] and
/// captures the result on resolution. The test then asserts on the
/// captured value via [resultCompleter].
Widget _harness({
  required String title,
  required void Function(String? value) onResult,
}) {
  return MaterialApp(
    home: Builder(builder: (ctx) {
      return TextButton(
        onPressed: () async {
          final v = await showPasswordPromptDialog(ctx, title: title);
          onResult(v);
        },
        child: const Text('open'),
      );
    }),
  );
}

void main() {
  testWidgets('returns the entered password on Save', (tester) async {
    String? captured;
    bool resolved = false;
    await tester.pumpWidget(_harness(
      title: 'Password for homenet',
      onResult: (v) {
        captured = v;
        resolved = true;
      },
    ));
    await tester.tap(find.text('open'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 16));

    expect(find.text('Password for homenet'), findsOneWidget);
    await tester.enterText(
        find.byKey(const Key('password-prompt-field')), 'hunter2');
    await tester.tap(find.byKey(const Key('password-prompt-confirm')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 16));

    expect(resolved, isTrue);
    expect(captured, 'hunter2');
  });

  testWidgets('returns null on Cancel', (tester) async {
    String? captured = '__sentinel__';
    bool resolved = false;
    await tester.pumpWidget(_harness(
      title: 'WiFi password',
      onResult: (v) {
        captured = v;
        resolved = true;
      },
    ));
    await tester.tap(find.text('open'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 16));

    await tester.enterText(
        find.byKey(const Key('password-prompt-field')), 'something');
    await tester.tap(find.byKey(const Key('password-prompt-cancel')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 16));

    expect(resolved, isTrue);
    expect(captured, isNull);
  });

  testWidgets('empty password keeps the dialog open', (tester) async {
    bool resolved = false;
    await tester.pumpWidget(_harness(
      title: 'WiFi password',
      onResult: (_) => resolved = true,
    ));
    await tester.tap(find.text('open'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 16));

    // Tap confirm with empty field — should be a no-op.
    await tester.tap(find.byKey(const Key('password-prompt-confirm')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 16));

    // Dialog still on screen, future not resolved.
    expect(find.byKey(const Key('password-prompt-field')), findsOneWidget);
    expect(resolved, isFalse);
  });

  testWidgets('onSubmit: shows error inline on failure, pops on success',
      (tester) async {
    String? captured;
    int callCount = 0;

    await tester.pumpWidget(MaterialApp(
      home: Builder(builder: (ctx) {
        return TextButton(
          onPressed: () async {
            final v = await showPasswordPromptDialog(
              ctx,
              title: 'Auth',
              confirmLabel: 'Connect',
              onSubmit: (pw) async {
                callCount++;
                if (pw == 'wrong') return ('Bad password', null);
                return null; // success
              },
            );
            captured = v;
          },
          child: const Text('open'),
        );
      }),
    ));

    await tester.tap(find.text('open'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 16));

    // First attempt: wrong password → error shown, dialog stays open.
    await tester.enterText(
        find.byKey(const Key('password-prompt-field')), 'wrong');
    await tester.tap(find.byKey(const Key('password-prompt-confirm')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 16));

    expect(find.text('Bad password'), findsOneWidget);
    expect(find.byKey(const Key('password-prompt-field')), findsOneWidget);
    expect(captured, isNull); // not resolved yet

    // Second attempt: correct password → dialog closes.
    await tester.enterText(
        find.byKey(const Key('password-prompt-field')), 'correct');
    await tester.tap(find.byKey(const Key('password-prompt-confirm')));
    await tester.pumpAndSettle();

    expect(find.byKey(const Key('password-prompt-field')), findsNothing);
    expect(captured, 'correct');
    expect(callCount, 2);
  });
}
