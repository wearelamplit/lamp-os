// Parsing tests for the `greeting` object in CHAR_STATE_NOTIFY payloads.
//
// Three cases:
// 1. Active greeting: parses to GreetingState with peer+kind; peer is uppercased.
// 2. Idle greeting: `{"greeting":{"active":false}}` parses to null (inactive).
// 3. Missing `greeting` key: absent key → null (backward-compat with older firmware).

import 'dart:convert';
import 'dart:typed_data';

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

const _devId = 'lamp-greeting-test';

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

void _emitStateNotify(InMemoryBleClient ble, Map<String, dynamic> payload) {
  ble.simulateNotify(
    _devId,
    BleUuids.controlService,
    BleUuids.stateNotify,
    Uint8List.fromList(utf8.encode(jsonEncode(payload))),
  );
}

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  test('active greeting parses to GreetingState with peer + kind', () async {
    final (c, ble) = await _buildContainer();
    addTearDown(c.dispose);

    _emitStateNotify(ble, {
      'previewActive': false,
      'greeting': {
        'active': true,
        'peer': 'aa:bb:cc:dd:ee:ff',
        'kind': 'glitch',
      },
    });
    await Future<void>.delayed(Duration.zero);

    final g = c.read(controlNotifierProvider(_devId)).value?.greeting;
    expect(g, isNotNull);
    expect(g!.peer, 'AA:BB:CC:DD:EE:FF'); // uppercased
    expect(g.kind, 'glitch');
  });

  test('idle greeting emits null', () async {
    final (c, ble) = await _buildContainer();
    addTearDown(c.dispose);

    // First set an active greeting.
    _emitStateNotify(ble, {
      'greeting': {'active': true, 'peer': 'AA:BB:CC:DD:EE:FF', 'kind': 'warm'},
    });
    await Future<void>.delayed(Duration.zero);
    expect(
        c.read(controlNotifierProvider(_devId)).value?.greeting, isNotNull);

    // Then emit idle.
    _emitStateNotify(ble, {
      'greeting': {'active': false},
    });
    await Future<void>.delayed(Duration.zero);

    expect(c.read(controlNotifierProvider(_devId)).value?.greeting, isNull);
  });

  test('absent greeting key leaves greeting null (backward-compat)', () async {
    final (c, ble) = await _buildContainer();
    addTearDown(c.dispose);

    _emitStateNotify(ble, {'previewActive': false});
    await Future<void>.delayed(Duration.zero);

    expect(c.read(controlNotifierProvider(_devId)).value?.greeting, isNull);
  });
}
