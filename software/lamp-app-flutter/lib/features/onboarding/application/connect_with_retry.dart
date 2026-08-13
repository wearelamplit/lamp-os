import 'dart:async';

import '../../../core/ble/ble_client.dart';

/// Initial-connect retry shared by the onboarding sites. Android throws
/// GATT_ERROR(133) / a deviceIsDisconnected race intermittently on the first
/// connect in a crowded BLE environment; retry with backoff, rethrowing the
/// last failure so the caller can map it to a user-visible error. [shouldAbort]
/// lets a caller bail between attempts (the pulse controller's stop()).
///
/// This is NOT the post-reboot reconnect loop in AddLampNotifier: that one
/// disconnects a stale handle, waits out the firmware reboot, and wraps a
/// per-attempt timeout, deliberately different, so it stays hand-rolled.
Future<void> connectWithRetry(
  BleClient ble,
  String deviceId, {
  int attempts = 4,
  Duration backoff = const Duration(milliseconds: 1200),
  bool Function()? shouldAbort,
}) async {
  for (var i = 0; i < attempts; i++) {
    if (shouldAbort?.call() ?? false) return;
    try {
      await ble.connect(deviceId);
      return;
    } catch (_) {
      if (i == attempts - 1) rethrow;
      await Future<void>.delayed(backoff);
    }
  }
}
