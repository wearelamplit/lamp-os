import 'dart:async';

import 'ble_client.dart';
import 'ble_timing.dart';

/// The app's connect retry policy, shared by opening a lamp from My Lamps and
/// by onboarding. Android throws GATT_ERROR(133) / a deviceIsDisconnected race
/// intermittently on a first connect in a crowded BLE environment, and the
/// stack needs about a second to release the slot after a failed attempt.
/// Makes at most [attempts] tries and rethrows the last failure so the caller
/// can map it to a user-visible error.
///
/// Claims [deviceId] for [owner] (see [BleClient.claimLink]). Returns true
/// once connected. Returns false when [shouldAbort] turns true, checked before
/// each attempt and after a successful one. On false or a rethrow, a link left
/// up is dropped unless a later connect has claimed the lamp.
///
/// The post-reboot reconnect loop in AddLampNotifier is a different policy. It
/// disconnects a stale handle, waits out the firmware reboot, and wraps a
/// per-attempt timeout.
Future<bool> connectWithRetry(
  BleClient ble,
  String deviceId, {
  required Object owner,
  int attempts = 4,
  Duration backoff = const Duration(milliseconds: 1200),
  bool Function()? shouldAbort,
}) async {
  // A caller that has already given up must not take the claim from a newer
  // connect, or its release would drop that connect's link.
  if (shouldAbort?.call() ?? false) return false;
  ble.claimLink(deviceId, owner);
  // The check and the disconnect stay in one synchronous step, so a connect
  // that claims the lamp afterwards queues behind this disconnect.
  Future<void> release() async {
    if (ble.ownsLink(deviceId, owner) && ble.isConnected(deviceId)) {
      await ble.disconnect(deviceId);
    }
  }

  final callStart = bleTimings.now();
  for (var i = 0; i < attempts; i++) {
    if (shouldAbort?.call() ?? false) {
      await release();
      return false;
    }
    final attemptStart = bleTimings.now();
    final wasUp = ble.isConnected(deviceId);
    try {
      await ble.connect(deviceId);
    } catch (_) {
      bleTimings.connectFailed(deviceId, attemptStart, i + 1, attempts);
      if (i == attempts - 1) {
        await release();
        rethrow;
      }
      await Future<void>.delayed(backoff);
      continue;
    }
    if (shouldAbort?.call() ?? false) {
      bleTimings.connectAborted(deviceId, attemptStart, i + 1, attempts);
      await release();
      return false;
    }
    bleTimings.connectSucceeded(
      deviceId,
      callStart,
      attemptStart,
      i + 1,
      attempts,
      wasUp: wasUp,
    );
    return true;
  }
  return false;
}
