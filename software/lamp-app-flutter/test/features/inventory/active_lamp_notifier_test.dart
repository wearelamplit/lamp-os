import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/inventory/application/active_lamp_notifier.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  test('starts null', () async {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    expect(await c.read(activeLampNotifierProvider.future), null);
  });

  test('set persists across containers', () async {
    final c1 = ProviderContainer();
    await c1.read(activeLampNotifierProvider.future);
    await c1.read(activeLampNotifierProvider.notifier).set('lamp-1');
    c1.dispose();

    final c2 = ProviderContainer();
    addTearDown(c2.dispose);
    expect(await c2.read(activeLampNotifierProvider.future), 'lamp-1');
  });

  test('clear resets to null', () async {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    await c.read(activeLampNotifierProvider.future);
    final n = c.read(activeLampNotifierProvider.notifier);
    await n.set('a');
    await n.clear();
    expect(await c.read(activeLampNotifierProvider.future), null);
  });
}
