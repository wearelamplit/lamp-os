import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/presentation/knockout_screen.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';

import '../../../_support/seed.dart';

const _devId = 'lamp-x';

Future<void> _seed(InMemoryBleClient ble) => seedControlBle(
      ble,
      deviceId: _devId,
      // Match the inventory name below — ControlNotifier now syncs
      // inventory.name from the live BLE section on every cold-load, so
      // a divergent fixture would clobber the inventory's 'jacko' with
      // the BLE 'test' and the title assertion would fail.
      name: 'jacko',
      basePx: 3,
      // Positional knockout: pixel 1 at 50%, pixels 0 and 2 default (100%).
      baseKnockoutJson: '[100,50,100]',
    );

Future<ProviderContainer> _withState() async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await _seed(ble);
  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'jacko',
        controlPassword: 'secret',
      ));
  await c.read(controlNotifierProvider(_devId).future);
  return c;
}

void main() {
  testWidgets('renders one row per LED with the right brightness label',
      (tester) async {
    final c = await _withState();
    addTearDown(c.dispose);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: KnockoutScreen(lampId: _devId),
      ),
    ));
    await tester.pumpAndSettle();

    // 3 rows for px = 3.
    expect(find.text('#0'), findsOneWidget);
    expect(find.text('#1'), findsOneWidget);
    expect(find.text('#2'), findsOneWidget);

    // Default rows show 100%, the seeded row shows 50%.
    expect(find.text('100%'), findsNWidgets(2));
    expect(find.text('50%'), findsOneWidget);
  });

  testWidgets('Reset all returns every pixel to 100%', (tester) async {
    final c = await _withState();
    addTearDown(c.dispose);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: KnockoutScreen(lampId: _devId),
      ),
    ));
    await tester.pumpAndSettle();

    await tester.tap(find.text('Reset all'));
    await tester.pumpAndSettle();

    expect(find.text('100%'), findsNWidgets(3));
    expect(find.text('50%'), findsNothing);
    // After clearKnockout, the in-memory map is empty.
    expect(
      c.read(controlNotifierProvider(_devId)).value!.base.knockout,
      isEmpty,
    );
  });

  testWidgets('Reset all is disabled when no pixels are edited',
      (tester) async {
    SharedPreferences.setMockInitialValues({});
    final ble = InMemoryBleClient();
    await seedControlBle(
      ble,
      deviceId: _devId,
      name: 'test',
      basePx: 3,
      // no knockout overrides — all 100%
    );
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
          id: _devId,
          name: 'jacko',
          controlPassword: 'secret',
        ));
    await c.read(controlNotifierProvider(_devId).future);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: KnockoutScreen(lampId: _devId),
      ),
    ));
    await tester.pumpAndSettle();

    final reset = tester.widget<TextButton>(
      find.widgetWithText(TextButton, 'Reset all'),
    );
    expect(reset.onPressed, isNull);
  });

  testWidgets('title shows the lamp name', (tester) async {
    final c = await _withState();
    addTearDown(c.dispose);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: KnockoutScreen(lampId: _devId),
      ),
    ));
    await tester.pumpAndSettle();
    expect(find.text('Pixel Knockout · jacko'), findsOneWidget);
  });

  testWidgets('tapping the bar on a row sets that pixel via setKnockoutPixel',
      (tester) async {
    final c = await _withState();
    addTearDown(c.dispose);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: KnockoutScreen(lampId: _devId),
      ),
    ));
    await tester.pumpAndSettle();

    // Pixel 0 starts at 100. Tap on row #0 near the left of the bar
    // (x < bar midpoint) — should drive it well below 100.
    final row0 = find.text('#0');
    final box = tester.renderObject<RenderBox>(row0);
    // The bar starts ~32px to the right of the label (#0). Tap a bit
    // past that, still well left of mid-screen.
    final tapPosition = box.localToGlobal(const Offset(60, 8));
    await tester.tapAt(tapPosition);
    // Drain the 30ms knockout-write debounce AND the 500ms commit debounce
    // that setKnockoutPixel now schedules. Without the full drain, the
    // pending timer causes an assertion failure on widget-tree dispose.
    await tester.pump(const Duration(milliseconds: 550));

    final knockout = c
        .read(controlNotifierProvider(_devId))
        .value!
        .base
        .knockout;
    expect(knockout.containsKey(0), isTrue);
    expect(knockout[0]! < 100, isTrue);
  });

  testWidgets('vertical drag paints multiple pixels in one stroke',
      (tester) async {
    final c = await _withState();
    addTearDown(c.dispose);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: KnockoutScreen(lampId: _devId),
      ),
    ));
    await tester.pumpAndSettle();

    // Start the stroke near row #0's bar (well left of bar midpoint so
    // value < 100), drag straight down past row #2.
    final row0 = find.text('#0');
    final box = tester.renderObject<RenderBox>(row0);
    final start = box.localToGlobal(const Offset(60, 8));
    final gesture = await tester.startGesture(start);
    // Move through rows 1 and 2 (each row is 28px tall).
    await gesture.moveBy(const Offset(0, 28));
    await tester.pump();
    await gesture.moveBy(const Offset(0, 28));
    await tester.pump();
    await gesture.up();
    // Drain the 30ms knockout-write debounce AND the 500ms commit debounce
    // that setKnockoutPixel now schedules (one timer per pixel touched).
    await tester.pump(const Duration(milliseconds: 550));

    final knockout = c
        .read(controlNotifierProvider(_devId))
        .value!
        .base
        .knockout;
    // The stroke crossed all three rows; each picked up a value below
    // 100 from the same low-x position.
    expect(knockout.containsKey(0), isTrue);
    expect(knockout.containsKey(1), isTrue);
    expect(knockout.containsKey(2), isTrue);
    expect(knockout[0]! < 100, isTrue);
    expect(knockout[1]! < 100, isTrue);
    expect(knockout[2]! < 100, isTrue);
  });

  testWidgets('dragging in the readout zone does not paint',
      (tester) async {
    final c = await _withState();
    addTearDown(c.dispose);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: KnockoutScreen(lampId: _devId),
      ),
    ));
    await tester.pumpAndSettle();

    // Start the stroke INSIDE row 0's "100%" readout text — that's the
    // scroll zone, not the paint zone. The pixel-1 entry (50% from the
    // fixture) should remain unchanged; pixels 0 and 2 should remain
    // unentered.
    final readout = find.text('100%').first;
    final box = tester.renderObject<RenderBox>(readout);
    final start = box.localToGlobal(const Offset(8, 8));
    final gesture = await tester.startGesture(start);
    await gesture.moveBy(const Offset(0, 56));
    await tester.pump();
    await gesture.up();
    await tester.pump(const Duration(milliseconds: 50));

    final knockout = c
        .read(controlNotifierProvider(_devId))
        .value!
        .base
        .knockout;
    // No new entries; pixel 1's seeded 50% is intact; 0 and 2 stayed default.
    expect(knockout.containsKey(0), isFalse);
    expect(knockout.containsKey(2), isFalse);
    expect(knockout[1], 50);
  });
}
