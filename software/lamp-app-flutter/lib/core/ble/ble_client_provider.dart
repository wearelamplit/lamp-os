import 'package:riverpod_annotation/riverpod_annotation.dart';

import 'ble_client.dart';
import 'fbp_ble_client.dart';

part 'ble_client_provider.g.dart';

@Riverpod(keepAlive: true, name: 'bleClientProvider')
BleClient bleClient(Ref ref) => FbpBleClient();

/// Live connection state for one lamp. Emits the current value on listen, then
/// on every link edge (including an unsolicited supervision-timeout drop), so a
/// UI watching it repaints the moment the lamp powers off.
@riverpod
Stream<bool> lampConnected(Ref ref, String deviceId) =>
    ref.watch(bleClientProvider).watchConnected(deviceId);
