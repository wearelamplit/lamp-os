import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../_support/in_memory_ble_client.dart';
import '../../../_support/seed.dart';

const _devId = 'lamp-a';
const _hash = 'abc123';
const _prefsKey = 'exprcat.v1.$_hash';

/// Counts `readSection('exprcat')` calls so the load-bearing assertion
/// (a cache hit skips the read) has something to observe.
class _CountingBle extends InMemoryBleClient {
  int exprcatReads = 0;

  @override
  Future<Uint8List> readSection(String deviceId, String name) {
    if (name == 'exprcat') exprcatReads++;
    return super.readSection(deviceId, name);
  }
}

Future<(ProviderContainer, _CountingBle, ControlNotifier)> _build() async {
  final ble = _CountingBle();
  await seedControlBle(ble, deviceId: _devId);

  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );
  addTearDown(c.dispose);

  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'test-lamp',
        controlPassword: '',
      ));
  c.listen(controlNotifierProvider(_devId), (_, _) {});
  await c.read(controlNotifierProvider(_devId).future);

  final n = c.read(controlNotifierProvider(_devId).notifier);
  ble.exprcatReads = 0; // build() already exercised the absent-hash path
  return (c, ble, n);
}

Future<String?> _stored() async =>
    (await SharedPreferences.getInstance()).getString(_prefsKey);

void main() {
  test('HIT: cached catalog returned without reading exprcat', () async {
    SharedPreferences.setMockInitialValues({_prefsKey: defaultExprcatJson});
    final (_, ble, n) = await _build();

    final catalog = await n.readCatalogForTest(ble, _hash);

    expect(catalog, isNotNull);
    expect(ble.exprcatReads, 0);
  });

  test('MISS: reads exprcat, decodes, and stores under the hash', () async {
    SharedPreferences.setMockInitialValues({});
    final (_, ble, n) = await _build();
    expect(await _stored(), isNull);

    final catalog = await n.readCatalogForTest(ble, _hash);

    expect(catalog, isNotNull);
    expect(ble.exprcatReads, 1);
    expect(await _stored(), defaultExprcatJson);
  });

  test('ABSENT: null hash always reads exprcat, cache untouched', () async {
    SharedPreferences.setMockInitialValues({});
    final (_, ble, n) = await _build();

    final catalog = await n.readCatalogForTest(ble, null);

    expect(catalog, isNotNull);
    expect(ble.exprcatReads, 1);
    expect(await _stored(), isNull);
  });

  test('CORRUPT: malformed entry falls through to a read and self-heals',
      () async {
    SharedPreferences.setMockInitialValues({_prefsKey: 'not-json{{{'});
    final (_, ble, n) = await _build();

    final catalog = await n.readCatalogForTest(ble, _hash);

    expect(catalog, isNotNull);
    expect(ble.exprcatReads, 1);
    expect(await _stored(), defaultExprcatJson);
  });
}
