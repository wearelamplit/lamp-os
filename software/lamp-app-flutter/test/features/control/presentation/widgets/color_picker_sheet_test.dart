import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/control/presentation/widgets/color_picker_sheet.dart';
import 'package:lamp_app/features/control/presentation/widgets/lamp_color_swatch.dart';

Widget _harness({required int bpp, required LampColor initial}) {
  return MaterialApp(
    home: Builder(
      builder: (ctx) => Scaffold(
        body: Center(
          child: TextButton(
            onPressed: () => showColorPickerSheet(ctx, initial: initial, bpp: bpp),
            child: const Text('open'),
          ),
        ),
      ),
    ),
  );
}

void main() {
  testWidgets('opens with 4 channel labels when bpp = 4', (tester) async {
    await tester.pumpWidget(MaterialApp(
      home: Builder(
        builder: (ctx) => Scaffold(
          body: Center(
            child: TextButton(
              onPressed: () => showColorPickerSheet(
                ctx,
                initial: const LampColor(r: 0x30, g: 0x07, b: 0x83, w: 0x80),
                bpp: 4,
              ),
              child: const Text('open'),
            ),
          ),
        ),
      ),
    ));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();
    expect(find.text('Red'), findsOneWidget);
    expect(find.text('Green'), findsOneWidget);
    expect(find.text('Blue'), findsOneWidget);
    expect(find.text('Warm White'), findsOneWidget);
  });

  testWidgets('hides Warm White slider when bpp = 3', (tester) async {
    await tester.pumpWidget(MaterialApp(
      home: Builder(
        builder: (ctx) => Scaffold(
          body: Center(
            child: TextButton(
              onPressed: () => showColorPickerSheet(
                ctx,
                initial: const LampColor(r: 0x30, g: 0x07, b: 0x83, w: 0xFF),
                bpp: 3,
              ),
              child: const Text('open'),
            ),
          ),
        ),
      ),
    ));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();
    expect(find.text('Red'), findsOneWidget);
    expect(find.text('Green'), findsOneWidget);
    expect(find.text('Blue'), findsOneWidget);
    expect(find.text('Warm White'), findsNothing);
  });

  testWidgets('onLive fires with bpp-3 W forced to 0', (tester) async {
    LampColor? lastLive;
    await tester.pumpWidget(MaterialApp(
      home: Builder(
        builder: (ctx) => Scaffold(
          body: Center(
            child: TextButton(
              onPressed: () => showColorPickerSheet(
                ctx,
                initial: const LampColor(r: 0, g: 0, b: 0, w: 0xFF),
                bpp: 3,
                onLive: (c) => lastLive = c,
              ),
              child: const Text('open'),
            ),
          ),
        ),
      ),
    ));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    // Drag the Red slider to fire a live update.
    final redSlider = find.byType(Slider).first;
    await tester.drag(redSlider, const Offset(200, 0));
    await tester.pumpAndSettle();

    expect(lastLive, isNotNull);
    expect(lastLive!.w, 0); // bpp=3 forces W to 0 even though initial was 0xFF.
    expect(lastLive!.r, greaterThan(0));
  });

  testWidgets('4bpp: ww-slider key present; preview swatch has correct W', (tester) async {
    const initial = LampColor(r: 100, g: 50, b: 30, w: 200);
    await tester.pumpWidget(_harness(bpp: 4, initial: initial));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    expect(find.byKey(const Key('ww-slider')), findsOneWidget);
    expect(find.byKey(const Key('preview-swatch')), findsOneWidget);

    final swatch = tester.widget<LampColorSwatch>(find.byKey(const Key('preview-swatch')));
    expect(swatch.color.w, equals(initial.w));
  });

  testWidgets('3bpp: ww-slider absent; preview swatch W is 0', (tester) async {
    const initial = LampColor(r: 100, g: 50, b: 30, w: 200);
    await tester.pumpWidget(_harness(bpp: 3, initial: initial));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    expect(find.byKey(const Key('ww-slider')), findsNothing);
    expect(find.byKey(const Key('preview-swatch')), findsOneWidget);

    final swatch = tester.widget<LampColorSwatch>(find.byKey(const Key('preview-swatch')));
    expect(swatch.color.w, equals(0));
  });
}
