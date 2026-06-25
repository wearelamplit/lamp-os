import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  test('empty store yields empty inventory', () async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final list = await container.read(inventoryNotifierProvider.future);
    expect(list, isEmpty);
  });

  test('add then load returns the lamp', () async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    await container.read(inventoryNotifierProvider.future);
    await container
        .read(inventoryNotifierProvider.notifier)
        .add(const InventoryLamp(
          id: 'aa:bb:cc:dd:ee:ff',
          name: 'jacko',
        ));
    final list = await container.read(inventoryNotifierProvider.future);
    expect(list, hasLength(1));
    expect(list.first.name, 'jacko');
  });

  test('add survives a fresh container (persisted to prefs)', () async {
    final c1 = ProviderContainer();
    await c1.read(inventoryNotifierProvider.future);
    await c1
        .read(inventoryNotifierProvider.notifier)
        .add(const InventoryLamp(id: 'aa', name: 'a'));
    c1.dispose();

    final c2 = ProviderContainer();
    addTearDown(c2.dispose);
    final list = await c2.read(inventoryNotifierProvider.future);
    expect(list, hasLength(1));
  });

  test('remove drops by id', () async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    await container.read(inventoryNotifierProvider.future);
    final notifier = container.read(inventoryNotifierProvider.notifier);
    await notifier.add(const InventoryLamp(id: '1', name: 'a'));
    await notifier.add(const InventoryLamp(id: '2', name: 'b'));
    await notifier.remove('1');
    final list = await container.read(inventoryNotifierProvider.future);
    expect(list.map((l) => l.id).toList(), ['2']);
  });

  test('updateName changes the lamp name and persists', () async {
    final c1 = ProviderContainer();
    await c1.read(inventoryNotifierProvider.future);
    final n1 = c1.read(inventoryNotifierProvider.notifier);
    await n1.add(const InventoryLamp(id: 'aa', name: 'original'));
    await n1.updateName('aa', 'renamed');
    final after = await c1.read(inventoryNotifierProvider.future);
    expect(after.single.name, 'renamed');
    c1.dispose();

    // Survives container teardown — actually went to prefs.
    final c2 = ProviderContainer();
    addTearDown(c2.dispose);
    final list = await c2.read(inventoryNotifierProvider.future);
    expect(list.single.name, 'renamed');
  });

  test('updateName is a no-op for unknown ids', () async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    await container.read(inventoryNotifierProvider.future);
    final notifier = container.read(inventoryNotifierProvider.notifier);
    await notifier.add(const InventoryLamp(id: 'aa', name: 'alpha'));
    await notifier.updateName('bb', 'beta');
    final list = await container.read(inventoryNotifierProvider.future);
    expect(list.single.name, 'alpha');
  });
}
