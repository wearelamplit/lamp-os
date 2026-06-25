import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/connection_notifier.dart';

void main() {
  test('connect transitions disconnected → connecting → connected', () async {
    final ble = InMemoryBleClient();
    final container = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(container.dispose);

    expect(container.read(connectionNotifierProvider('dev1')),
        ConnectionStatus.disconnected);

    final future =
        container.read(connectionNotifierProvider('dev1').notifier).connect();
    expect(container.read(connectionNotifierProvider('dev1')),
        ConnectionStatus.connecting);

    await future;
    expect(container.read(connectionNotifierProvider('dev1')),
        ConnectionStatus.connected);
  });

  test('disconnect transitions connected → disconnected', () async {
    final ble = InMemoryBleClient();
    final container = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(container.dispose);
    await container.read(connectionNotifierProvider('dev1').notifier).connect();
    await container
        .read(connectionNotifierProvider('dev1').notifier)
        .disconnect();
    expect(container.read(connectionNotifierProvider('dev1')),
        ConnectionStatus.disconnected);
  });
}
