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
  String? baseSegmentsJson,
}) async {
  SharedPreferences.setMockInitialValues({});
  final ble = client ?? InMemoryBleClient();
  await seedControlBle(ble,
      deviceId: _devId,
      baseKnockoutJson: '[100,50,0]',
      shadeSegmentsJson: shadeSegmentsJson,
      baseSegmentsJson: baseSegmentsJson);
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
    const pinned =
        '[{"name":"Shade","px":38,"pin":27,"builtinPin":12,"colors":[]}]';
    const builtIn =
        '[{"name":"Shade","px":38,"pin":0,"builtinPin":12,"colors":[]}]';
    const base =
        '[{"name":"Base","px":35,"pin":0,"builtinPin":14,"colors":[]}]';
    // On a pin near the top of the sheet, which builds its list lazily.
    const early =
        '[{"name":"Shade","px":38,"pin":4,"builtinPin":12,"colors":[]}]';

    Future<ProviderContainer> unlocked(WidgetTester tester,
        {String shade = pinned, String? baseSegs}) async {
      final c = await _makeContainer(
          shadeSegmentsJson: shade, baseSegmentsJson: baseSegs);
      addTearDown(c.dispose);
      await c.read(devModeProvider.notifier).enable();
      await tester.pumpWidget(_wrap(c));
      await _pumpToData(tester);
      return c;
    }

    // Pushed over a home route, so Update and Cancel can pop.
    Future<ProviderContainer> pushed(WidgetTester tester,
        {String shade = pinned}) async {
      final c = await _makeContainer(shadeSegmentsJson: shade);
      addTearDown(c.dispose);
      await c.read(devModeProvider.notifier).enable();
      // Holds the notifier across the pop, so the state stays readable.
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
      return c;
    }

    int shadePin(ProviderContainer c) =>
        c.read(controlNotifierProvider(_devId)).value!.shade.segments.single.pin!;

    Future<void> openSheet(WidgetTester tester) async {
      await tester.ensureVisible(find.text('Shade data pin'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Shade data pin'));
      await tester.pumpAndSettle();
    }

    Finder option(String title) => find.descendant(
          of: find.byType(BottomSheet),
          matching: find.ancestor(
              of: find.text(title), matching: find.byType(ListTile)),
        );

    testWidgets('hidden in advanced mode for a lamp that reports no pin',
        (tester) async {
      await unlocked(tester, shade: '[{"name":"Shade","px":38,"colors":[]}]');

      expect(find.text('GRBW'), findsWidgets);
      expect(find.text('Shade data pin'), findsNothing);
      expect(find.textContaining('wrong data pin'), findsNothing);
    });

    testWidgets('hidden for a segment no strip drives', (tester) async {
      await unlocked(tester,
          shade: '[{"name":"Shade","px":38,"pin":0,"builtinPin":0,"colors":[]}]');

      expect(find.text('GRBW'), findsWidgets);
      expect(find.text('Shade data pin'), findsNothing);
    });

    testWidgets('re-picking the built-in entry keeps a pin saved on it',
        (tester) async {
      final c = await unlocked(tester,
          shade: '[{"name":"Shade","px":38,"pin":12,"builtinPin":12,"colors":[]}]');
      expect(find.text('Built-in (GPIO 12)'), findsOneWidget);

      await openSheet(tester);
      await tester.tap(option('Built-in (GPIO 12)'));
      await tester.pumpAndSettle();

      expect(shadePin(c), 12);
    });

    testWidgets('hidden when advanced is locked', (tester) async {
      final c = await _makeContainer(shadeSegmentsJson: pinned);
      addTearDown(c.dispose);
      await tester.pumpWidget(_wrap(c));
      await _pumpToData(tester);

      expect(find.text('Shade data pin'), findsNothing);
      expect(find.text('GPIO 27'), findsNothing);
    });

    testWidgets('names the strip and warns a wrong pin leaves it dark',
        (tester) async {
      await unlocked(tester);

      expect(find.text('Shade data pin'), findsOneWidget);
      expect(find.text('GPIO 27'), findsOneWidget);
      expect(find.textContaining('A wrong data pin leaves that strip dark.'),
          findsOneWidget);
    });

    testWidgets('shows the built-in pin by number', (tester) async {
      await unlocked(tester, shade: builtIn);

      expect(find.text('Built-in (GPIO 12)'), findsOneWidget);
    });

    testWidgets('a pick from the sheet stages the pin', (tester) async {
      final c = await unlocked(tester);

      await openSheet(tester);
      await tester.tap(find.text('GPIO 13'));
      await tester.pumpAndSettle();

      expect(shadePin(c), 13);
      expect(find.text('GPIO 13'), findsOneWidget);
    });

    testWidgets('greys out a pin another strip runs on', (tester) async {
      final c = await unlocked(tester, shade: builtIn, baseSegs: base);

      await openSheet(tester);

      expect(tester.widget<ListTile>(option('GPIO 14')).enabled, isFalse);
      expect(find.text('Used by Base strip'), findsOneWidget);
      expect(tester.widget<ListTile>(option('GPIO 13')).enabled, isTrue);
      await tester.tap(find.text('GPIO 14'), warnIfMissed: false);
      await tester.pumpAndSettle();
      expect(shadePin(c), 0);
    });

    testWidgets('the sheet checks the current pin and picking it changes nothing',
        (tester) async {
      final c = await unlocked(tester, shade: early);
      var notified = 0;
      final sub = c.listen(controlNotifierProvider(_devId), (_, _) => notified++);
      addTearDown(sub.close);

      await openSheet(tester);

      final current = option('GPIO 4');
      expect(
        find.descendant(of: current, matching: find.byIcon(Icons.check)),
        findsOneWidget,
      );
      expect(
        find.descendant(
            of: find.byType(BottomSheet), matching: find.byIcon(Icons.check)),
        findsOneWidget,
      );
      final screen = tester.getSize(find.byType(Scaffold).first).height;
      expect(tester.getSize(find.byType(BottomSheet)).height,
          lessThanOrEqualTo(screen * 0.7 + 1));

      await tester.tap(option('GPIO 4'));
      await tester.pumpAndSettle();
      expect(notified, 0);
      expect(shadePin(c), 4);
    });

    testWidgets('sheet options are 48dp targets and announce the selection',
        (tester) async {
      final handle = tester.ensureSemantics();
      await unlocked(tester, shade: early);

      await openSheet(tester);

      for (final title in ['Built-in (GPIO 12)', 'GPIO 2', 'GPIO 4']) {
        expect(tester.getSize(option(title)).height,
            greaterThanOrEqualTo(48), reason: title);
      }
      expect(
        tester.getSemantics(option('GPIO 4')),
        matchesSemantics(
          label: 'GPIO 4',
          isSelected: true,
          hasSelectedState: true,
          isEnabled: true,
          hasEnabledState: true,
          isButton: true,
          hasTapAction: true,
          isFocusable: true,
          hasFocusAction: true,
        ),
      );
      handle.dispose();
    });

    testWidgets('a pin that did not take says which and why', (tester) async {
      await pushed(tester);

      await openSheet(tester);
      await tester.tap(find.text('GPIO 13'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Update'));
      for (var i = 0; i < 40; i++) {
        await tester.pump(const Duration(milliseconds: 100));
      }

      expect(
        find.text("Shade data pin didn't take. It clashes with another strip, "
            'a button or a touch pad, so the lamp kept its built-in pin.'),
        findsOneWidget,
      );
      expect(find.textContaining('shade.pin'), findsNothing);
    });

    testWidgets('a strip type that did not take says so', (tester) async {
      await pushed(tester);

      await tester.ensureVisible(find.text('GRB').first);
      await tester.pumpAndSettle();
      await tester.tap(find.text('GRB').first);
      await tester.pumpAndSettle();
      await tester.tap(find.text('Update'));
      for (var i = 0; i < 40; i++) {
        await tester.pump(const Duration(milliseconds: 100));
      }

      expect(
        find.text("Shade strip type didn't take, so the lamp kept its old one."),
        findsOneWidget,
      );
    });

    testWidgets('Cancel puts a picked pin back', (tester) async {
      final c = await pushed(tester);

      await openSheet(tester);
      await tester.tap(find.text('GPIO 13'));
      await tester.pumpAndSettle();
      expect(shadePin(c), 13);

      await tester.tap(find.text('Cancel'));
      await tester.pumpAndSettle();

      expect(find.text('home'), findsOneWidget);
      expect(shadePin(c), 27);
    });
  });

  group('advancedLedsSaveMessage', () {
    test('names each field that did not take, once per strip', () {
      expect(
        advancedLedsSaveMessage(['shade.byteOrder', 'shade.bpp', 'base.px']),
        "Shade strip type didn't take, so the lamp kept its old one. "
        "Base LED count didn't take, so the lamp kept its old count.",
      );
    });

    test('names a pin on a role of several strips without guessing which', () {
      expect(
        advancedLedsSaveMessage(['shade.pin'], shadeStrips: 3),
        "A shade strip data pin didn't take. It clashes with another strip, "
        'a button or a touch pad, so the lamp kept its built-in pin.',
      );
    });

    test('a lamp that did not come back says so in plain words', () {
      final message = advancedLedsSaveMessage(
          ['lamp did not reconnect after Advanced LED save: timeout']);
      expect(message,
          "Your lamp didn't come back after restarting. Bring your phone "
          'closer and try again.');
      expect(message, isNot(contains('timeout')));
    });
  });
}
