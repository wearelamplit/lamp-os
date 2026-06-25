import 'package:riverpod_annotation/riverpod_annotation.dart';

import 'ble_client_provider.dart';

part 'connection_notifier.g.dart';

enum ConnectionStatus { disconnected, connecting, connected, error }

@Riverpod(keepAlive: true, name: 'connectionNotifierProvider')
class ConnectionNotifier extends _$ConnectionNotifier {
  @override
  ConnectionStatus build(String deviceId) => ConnectionStatus.disconnected;

  Future<void> connect() async {
    state = ConnectionStatus.connecting;
    try {
      await ref.read(bleClientProvider).connect(deviceId);
      state = ConnectionStatus.connected;
    } catch (_) {
      state = ConnectionStatus.error;
      rethrow;
    }
  }

  Future<void> disconnect() async {
    try {
      await ref.read(bleClientProvider).disconnect(deviceId);
    } finally {
      state = ConnectionStatus.disconnected;
    }
  }
}
