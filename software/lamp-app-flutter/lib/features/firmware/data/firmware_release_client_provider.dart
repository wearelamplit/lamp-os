// Standalone provider so both firmware_notifier (per-lamp OTA driver)
// and cached_firmware_notifier (app-wide cache) share one HttpClient
// instance without creating a circular import between them.

import 'package:riverpod_annotation/riverpod_annotation.dart';

import 'firmware_release_client.dart';

part 'firmware_release_client_provider.g.dart';

@Riverpod(name: 'firmwareReleaseClientProvider', keepAlive: true)
FirmwareReleaseClient firmwareReleaseClient(Ref ref) {
  final client = FirmwareReleaseClient();
  ref.onDispose(client.close);
  return client;
}
