import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/held_save_ble_client.dart';
import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/core/widgets/form_section.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/application/dev_mode.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/lamp_shell/presentation/advanced_leds_screen.dart';

import '../../_support/seed.dart';

const _devId = 'lamp-x';

Future<ProviderContainer> _makeContainer({
  InMemoryBleClient? client,
  String? shadeSegmentsJson,
}) async {
  SharedPreferences.setMockInitialValues({});
  final ble = client ?? InMemoryBleClient();
  await seedControlBle(ble,
      deviceId: _devId,
      baseKnockoutJson: '[100,50,0]',
      shadeSegmentsJson: shadeSegmentsJson);
  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'jacko',
        controlPassword: 'secret',
      ));
  return c;
}

Future<void> _pumpToData(WidgetTester tester) async {
  for (var i = 0; i < 30; i++) {
    await tester.pump(const Duration(milliseconds: 16));
    if (find.text('SHADE STRIP').evaluate().isNotEmpty) return;
  }
}

Widget _wrap(ProviderContainer c) => UncontrolledProviderScope(
      container: c,
      // Plain MaterialApp: these tests assert row presence + order, they
      // don't tap the knockout row (which would need a Router for push).
      child: const MaterialApp(home: AdvancedLedsScreen(lampId: _devId)),
    );

void main() {
  testWidgets('a save too big to send shows the friendly error, writes nothing',
      (tester) async {
    // More segments than any firmware reports, so the blob passes the cap.
    final segs = [
      for (var k = 0; k < 60; k++) '{"name":"s$k","px":200,"colors":[]}',
    ].join(',');
    final ble = InMemoryBleClient();
    final c = await _makeContainer(client: ble, shadeSegmentsJson: '[$segs]');
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    await tester.tap(find.text('Update'));
    for (var i = 0; i < 10; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }

    expect(find.text("Couldn't save: too much to send to the lamp at once."),
        findsOneWidget);
    expect(ble.writesTo(_devId, BleUuids.settingsBlob), isEmpty);
  });

  testWidgets('an Update in flight disables Update and shows progress',
      (tester) async {
    final ble = HeldSaveBleClient();
    final c = await _makeContainer(client: ble);
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    final hold = ble.hold = Completer<void>();
    final update = find.ancestor(
      of: find.text('Update'),
      matching: find.byWidgetPredicate((w) => w is FilledButton),
    );
    await tester.tap(find.text('Update'));
    for (var i = 0; i < 10; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }

    expect(ble.saves, 1);
    expect(tester.widget<FilledButton>(update).onPressed, isNull);
    expect(
      find.descendant(
          of: update, matching: find.byType(CircularProgressIndicator)),
      findsOneWidget,
    );
    await tester.tap(find.text('Update'), warnIfMissed: false);
    await tester.pump();
    expect(ble.saves, 1);

    hold.completeError(const BleEncryptionRequired(_devId));
    for (var i = 0; i < 10; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }
    expect(find.textContaining('Save failed'), findsOneWidget);
    expect(tester.widget<FilledButton>(update).onPressed, isNotNull);
  });

  testWidgets('Update stays disabled while the lamp restarts after it',
      (tester) async {
    final ble = InMemoryBleClient();
    final c = await _makeContainer(client: ble);
    addTearDown(c.dispose);
    final nav = GlobalKey<NavigatorState>();
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        navigatorKey: nav,
        home: const Scaffold(body: Text('home')),
      ),
    ));
    unawaited(nav.currentState!.push(MaterialPageRoute<void>(
      builder: (_) => const AdvancedLedsScreen(lampId: _devId),
    )));
    await _pumpToData(tester);
    await tester.pump(const Duration(milliseconds: 500));

    await tester.tap(find.text('Update'));
    await tester.pump(const Duration(milliseconds: 16));
    // The restart drops the link; the screen shows its form again while the
    // save waits to reconnect.
    await ble.disconnect(_devId);
    for (var i = 0; i < 10; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }
    final update = find.ancestor(
      of: find.text('Update'),
      matching: find.byWidgetPredicate((w) => w is FilledButton),
    );
    expect(update, findsOneWidget);
    expect(tester.widget<FilledButton>(update).onPressed, isNull);

    for (var i = 0; i < 40; i++) {
      await tester.pump(const Duration(milliseconds: 100));
    }
    expect(find.text('home'), findsOneWidget, reason: 'the save finished');
  });

  testWidgets('Shade strip section renders above Base strip', (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    expect(find.text('SHADE STRIP'), findsOneWidget);
    expect(find.text('BASE STRIP'), findsOneWidget);
    expect(
      tester.getCenter(find.text('SHADE STRIP')).dy,
      lessThan(tester.getCenter(find.text('BASE STRIP')).dy),
    );
  });

  testWidgets('Per-pixel knockout entry is nested in LED setup', (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    expect(find.text('Per-pixel knockout'), findsOneWidget);
  });

  testWidgets('knockout entry sits under the Base strip section',
      (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    // Knockout masks base pixels only, so its entry belongs below the
    // Base strip header, not the Shade one.
    expect(
      tester.getCenter(find.text('BASE STRIP')).dy,
      lessThan(tester.getCenter(find.text('Per-pixel knockout')).dy),
    );
  });

  testWidgets('reboot warning is visible near the Update action', (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    expect(find.textContaining('restarts the lamp'), findsOneWidget);
  });

  testWidgets('renders two FormSection cards with correct titles', (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    // SectionHeader uppercases the title, so find by the rendered text.
    expect(find.widgetWithText(FormSection, 'SHADE STRIP'), findsOneWidget);
    expect(find.widgetWithText(FormSection, 'BASE STRIP'), findsOneWidget);
  });

  testWidgets('byte-order pickers hidden when advanced is locked', (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    expect(find.text('GRBW'), findsNothing);
  });

  testWidgets('byte-order pickers visible when advanced is unlocked', (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await c.read(devModeProvider.notifier).enable();
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    expect(find.text('GRBW'), findsWidgets);
  });

  group('data pin', () {
    const pinned = '[{"name":"Shade","px":38,"pin":27,"colors":[]}]';

    testWidgets('hidden when advanced is locked', (tester) async {
      final c = await _makeContainer(shadeSegmentsJson: pinned);
      addTearDown(c.dispose);
      await tester.pumpWidget(_wrap(c));
      await _pumpToData(tester);

      expect(find.text('Data pin'), findsNothing);
      expect(find.text('GPIO 27'), findsNothing);
    });

    testWidgets('shows the saved pin and stages a new pick when unlocked',
        (tester) async {
      final c = await _makeContainer(shadeSegmentsJson: pinned);
      addTearDown(c.dispose);
      await c.read(devModeProvider.notifier).enable();
      await tester.pumpWidget(_wrap(c));
      await _pumpToData(tester);

      expect(find.text('Data pin'), findsOneWidget);
      expect(find.text('GPIO 27'), findsOneWidget);

      await tester.tap(find.text('Data pin'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('GPIO 13'));
      await tester.pumpAndSettle();

      final state = c.read(controlNotifierProvider(_devId)).value!;
      expect(state.shade.segments.single.pin, 13);
      expect(find.text('GPIO 13'), findsOneWidget);
    });

    testWidgets('Cancel puts a picked pin back', (tester) async {
      final c = await _makeContainer(shadeSegmentsJson: pinned);
      addTearDown(c.dispose);
      await c.read(devModeProvider.notifier).enable();
      // Holds the notifier across the pop, so the restored state is readable.
      final sub = c.listen(controlNotifierProvider(_devId), (_, _) {});
      addTearDown(sub.close);
      final nav = GlobalKey<NavigatorState>();
      await tester.pumpWidget(UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          navigatorKey: nav,
          home: const Scaffold(body: Text('home')),
        ),
      ));
      unawaited(nav.currentState!.push(MaterialPageRoute<void>(
        builder: (_) => const AdvancedLedsScreen(lampId: _devId),
      )));
      await _pumpToData(tester);
      await tester.pumpAndSettle();

      await tester.tap(find.text('Data pin'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('GPIO 13'));
      await tester.pumpAndSettle();
      expect(
          c.read(controlNotifierProvider(_devId)).value!.shade.segments.single.pin,
          13);

      await tester.tap(find.text('Cancel'));
      await tester.pumpAndSettle();

      expect(find.text('home'), findsOneWidget);
      expect(
          c.read(controlNotifierProvider(_devId)).value!.shade.segments.single.pin,
          27);
    });
  });
}
