import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../../../_support/in_memory_ble_client.dart';
import '../../../../_support/seed.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/control/presentation/widgets/shade_editor_sheet.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

const _devId = 'aa:bb:cc:dd:ee:ff';

// Two distinct colors so removing a stop is a detectable change from baseline.
const _twoStops = '["#300783FF","#FF0000FF"]';
const _baselineColor = LampColor(r: 0x30, g: 0x07, b: 0x83, w: 0xFF);

Future<({ProviderContainer container, InMemoryBleClient ble})>
    _buildContainer() async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble,
      deviceId: _devId,
      shadeColorsJson: _twoStops,
      shadeBpp: 4);
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
  return (container: c, ble: ble);
}

Widget _wrap(ProviderContainer c) => UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        theme: appTheme,
        home: Builder(
          builder: (ctx) => Scaffold(
            body: TextButton(
              onPressed: () => showShadeEditorSheet(ctx, lampId: _devId),
              child: const Text('open'),
            ),
          ),
        ),
      ),
    );

void main() {
  testWidgets('Cancel reverts shade colors and closes', (tester) async {
    final (:container, :ble) = await _buildContainer();
    addTearDown(container.dispose);
    // Keep provider alive so we can read state after the sheet (and its
    // watchers) are dismissed — controlNotifierProvider is auto-dispose.
    final sub = container.listen(controlNotifierProvider(_devId), (_, __) {});
    addTearDown(sub.close);

    await tester.pumpWidget(_wrap(container));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();
    expect(find.byType(ShadeEditorSheet), findsOneWidget);

    // Remove the second stop — shade.colors goes from 2 → 1.
    await tester.tap(find.byIcon(Icons.close).last);
    await tester.pumpAndSettle();

    await tester.tap(find.text('Cancel'));
    await tester.pumpAndSettle();

    expect(find.byType(ShadeEditorSheet), findsNothing);
    final colors =
        container.read(controlNotifierProvider(_devId)).value!.shade.colors;
    expect(colors.length, 2);
    expect(colors.first, _baselineColor);
  });

  testWidgets('Save commits (writes to commit char) and closes',
      (tester) async {
    final (:container, :ble) = await _buildContainer();
    addTearDown(container.dispose);
    final sub = container.listen(controlNotifierProvider(_devId), (_, __) {});
    addTearDown(sub.close);

    await tester.pumpWidget(_wrap(container));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    // Remove a stop to make an edit.
    await tester.tap(find.byIcon(Icons.close).last);
    await tester.pumpAndSettle();

    await tester.tap(find.widgetWithText(FilledButton, 'Save'));
    await tester.pumpAndSettle();

    expect(ble.writesTo(_devId, BleUuids.commit), isNotEmpty);
    expect(find.byType(ShadeEditorSheet), findsNothing);
    // Edited value (1 stop) is preserved, not reverted.
    final colors =
        container.read(controlNotifierProvider(_devId)).value!.shade.colors;
    expect(colors.length, 1);
  });

  testWidgets(
      'System-back with unsaved changes shows discard dialog; Discard reverts+closes',
      (tester) async {
    final (:container, :ble) = await _buildContainer();
    addTearDown(container.dispose);
    final sub = container.listen(controlNotifierProvider(_devId), (_, __) {});
    addTearDown(sub.close);

    await tester.pumpWidget(_wrap(container));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    // Remove a stop to dirty the state.
    await tester.tap(find.byIcon(Icons.close).last);
    await tester.pumpAndSettle();

    // Simulate system back.
    await tester.binding.handlePopRoute();
    await tester.pumpAndSettle();

    expect(find.text('Discard changes?'), findsOneWidget);
    await tester.tap(find.text('Discard'));
    await tester.pumpAndSettle();

    expect(find.byType(ShadeEditorSheet), findsNothing);
    final colors =
        container.read(controlNotifierProvider(_devId)).value!.shade.colors;
    expect(colors.length, 2);
    expect(colors.first, _baselineColor);
  });
}
