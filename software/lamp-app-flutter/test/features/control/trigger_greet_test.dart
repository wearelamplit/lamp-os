import 'dart:convert';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';

const _devId = 'lamp-greet';
const _peerAddr = 'AA:BB:CC:DD:EE:FF';

Future<(ProviderContainer, InMemoryBleClient)> _buildContainer() async {
  final ble = InMemoryBleClient();
  await seedControlBle(ble, deviceId: _devId);

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

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  test('triggerGreet writes exact JSON to CHAR_EXPRESSION_TEST', () async {
    final (c, ble) = await _buildContainer();
    addTearDown(c.dispose);

    final n = c.read(controlNotifierProvider(_devId).notifier);
    await n.triggerGreet(_peerAddr);

    final writes = ble.writesTo(_devId, BleUuids.expressionTest);
    // At least one write landed (build() may emit test_expression_complete).
    final greetWrite = writes.lastWhere(
      (b) {
        final s = utf8.decode(b);
        return s.contains('triggerGreet');
      },
      orElse: () => throw TestFailure('no triggerGreet write found'),
    );

    final Map<String, dynamic> payload =
        jsonDecode(utf8.decode(greetWrite)) as Map<String, dynamic>;
    expect(payload['a'], 'triggerGreet');
    expect(payload['bdAddr'], _peerAddr);
    // Exact shape: only two keys.
    expect(payload.keys.toSet(), {'a', 'bdAddr'});
  });

  test('no advanced-mode gate: triggerGreet is callable without advanced session',
      () async {
    final (c, ble) = await _buildContainer();
    addTearDown(c.dispose);

    // No advanced session unlock — triggerGreet still works.
    final n = c.read(controlNotifierProvider(_devId).notifier);
    await expectLater(n.triggerGreet(_peerAddr), completes);

    final writes = ble.writesTo(_devId, BleUuids.expressionTest);
    expect(
      writes.any((b) => utf8.decode(b).contains('triggerGreet')),
      isTrue,
    );
  });
}
