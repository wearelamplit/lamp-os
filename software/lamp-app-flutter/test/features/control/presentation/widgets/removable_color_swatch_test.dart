import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/control/presentation/widgets/removable_color_swatch.dart';

void main() {
  const c = LampColor(r: 0xEF, g: 0xA8, b: 0xF0, w: 0);

  Widget host({required VoidCallback onEdit, VoidCallback? onRemove}) =>
      MaterialApp(
        home: Scaffold(
          body: Center(
            child: RemovableColorSwatch(
              color: c,
              onEdit: onEdit,
              onRemove: onRemove,
            ),
          ),
        ),
      );

  testWidgets('tapping the swatch face fires onEdit', (tester) async {
    var edits = 0;
    await tester.pumpWidget(host(onEdit: () => edits++, onRemove: () {}));
    // Centre of the swatch is clear of the corner remove button.
    await tester.tapAt(tester.getCenter(find.byType(RemovableColorSwatch)));
    expect(edits, 1);
  });

  testWidgets('tapping the remove button fires onRemove', (tester) async {
    var removes = 0;
    await tester.pumpWidget(host(onEdit: () {}, onRemove: () => removes++));
    await tester.tap(find.bySemanticsLabel('Remove color'));
    expect(removes, 1);
  });

  testWidgets('onRemove null hides the remove button', (tester) async {
    await tester.pumpWidget(host(onEdit: () {}, onRemove: null));
    expect(find.bySemanticsLabel('Remove color'), findsNothing);
  });

  testWidgets('tapping the swatch face does not fire onRemove', (tester) async {
    var removes = 0;
    await tester.pumpWidget(host(onEdit: () {}, onRemove: () => removes++));
    await tester.tapAt(tester.getCenter(find.byType(RemovableColorSwatch)));
    expect(removes, 0);
  });
}
