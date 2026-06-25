import 'package:riverpod_annotation/riverpod_annotation.dart';

import 'ble_client.dart';
import 'fbp_ble_client.dart';

part 'ble_client_provider.g.dart';

@Riverpod(keepAlive: true, name: 'bleClientProvider')
BleClient bleClient(Ref ref) => FbpBleClient();
