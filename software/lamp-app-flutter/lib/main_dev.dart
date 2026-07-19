import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'app.dart';
import 'core/ble/always_granted_permissions.dart';
import 'core/ble/ble_client_provider.dart';
import 'core/ble/bridge_transport.dart';
import 'core/ble/network_dev_ble_client.dart';
import 'core/ble/network_dev_ble_scanner.dart';
import 'features/nearby/application/nearby_lamps_notifier.dart';

void main() {
  // Namespaces all persisted state away from the real app's flutter. keys so
  // bridge-adopted inventory can never corrupt real-phone inventory.
  SharedPreferences.setPrefix('flutter.devharness.');

  const host = String.fromEnvironment('BRIDGE_HOST', defaultValue: 'localhost:8080');
  final transport = HttpBridgeTransport(host);
  runApp(ProviderScope(
    overrides: [
      bleClientProvider.overrideWithValue(NetworkDevBleClient(transport)),
      bleScannerProvider.overrideWithValue(NetworkDevBleScanner(transport)),
    ],
    child: LampApp(permissions: AlwaysGrantedPermissions()),
  ));
}
