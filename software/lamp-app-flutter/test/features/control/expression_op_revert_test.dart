import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/domain/sections.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';

const _devId = 'lamp-a';

Future<(ProviderContainer, InMemoryBleClient)> _build() async {
  final ble = InMemoryBleClient();
  await seedControlBle(
    ble,
    deviceId: _devId,
    expressionsJson: '[{"type":"pulse","enabled":true,"target":3,'
        '"colors":["#300783FF"],"intervalMin":60,"intervalMax":900}]',
  );

  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'test-lamp',
        controlPassword: '',
      ));
  c.listen(controlNotifierProvider(_devId), (_, _) {});
  await c.read(controlNotifierProvider(_devId).future);
  return (c, ble);
}

List<ExpressionConfig> _exprs(ProviderContainer c) =>
    c.read(controlNotifierProvider(_devId)).value!.expressions.expressions;

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  test('upsertExpression reverts on a failed write', () async {
    final (c, ble) = await _build();
    addTearDown(c.dispose);

    final before = _exprs(c);
    expect(before.single.enabled, isTrue);

    ble.scheduleEncryptionFailure(
        _devId, BleUuids.controlService, BleUuids.expressionOp);

    final n = c.read(controlNotifierProvider(_devId).notifier);
    await expectLater(
      () => n.upsertExpression(const ExpressionConfig(
        type: 'pulse',
        enabled: false,
        colors: [],
        intervalMin: 60,
        intervalMax: 900,
        target: 3,
        parameters: {},
      )),
      throwsA(anything),
    );

    final after = _exprs(c);
    expect(after.single.enabled, isTrue, reason: 'optimistic edit reverted');
  });

  test('removeExpression reverts on a failed write', () async {
    final (c, ble) = await _build();
    addTearDown(c.dispose);

    expect(_exprs(c).length, 1);

    ble.scheduleEncryptionFailure(
        _devId, BleUuids.controlService, BleUuids.expressionOp);

    final n = c.read(controlNotifierProvider(_devId).notifier);
    await expectLater(
      () => n.removeExpression(type: 'pulse', target: 3),
      throwsA(anything),
    );

    expect(_exprs(c).length, 1, reason: 'removed entry restored');
  });
}
