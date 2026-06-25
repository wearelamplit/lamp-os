import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';

void main() {
  test('default provider returns a BleClient instance', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final ble = c.read(bleClientProvider);
    expect(ble, isA<BleClient>());
  });

  test('tests can override with InMemoryBleClient', () {
    final fake = InMemoryBleClient();
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(fake)],
    );
    addTearDown(c.dispose);
    expect(identical(c.read(bleClientProvider), fake), isTrue);
  });
}
