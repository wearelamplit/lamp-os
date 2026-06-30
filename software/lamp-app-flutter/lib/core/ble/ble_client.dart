import 'dart:async';
import 'dart:typed_data';


class BleNotFound implements Exception {
  const BleNotFound(this.message);
  final String message;
  @override
  String toString() => 'BleNotFound: $message';
}

class BleEncryptionRequired implements Exception {
  const BleEncryptionRequired(this.deviceId);
  final String deviceId;
  @override
  String toString() => 'BleEncryptionRequired: $deviceId';
}

class BleNotConnected implements Exception {
  const BleNotConnected(this.deviceId);
  final String deviceId;
  @override
  String toString() => 'BleNotConnected: $deviceId';
}

/// Thrown when a BLE op fails because the link dropped (commonly the lamp
/// rebooting mid-write: settings_blob persist + fade-out + reset). Distinct
/// from [BleNotConnected], which means no connection was ever established.
/// Catch `on BleDisconnectedException` rather than string-matching
/// `e.toString().contains('disconnect')`, which breaks whenever
/// flutter_blue_plus rewords its error surface.
class BleDisconnectedException implements Exception {
  const BleDisconnectedException(this.deviceId, [this.cause]);
  final String deviceId;
  final Object? cause;
  @override
  String toString() =>
      'BleDisconnectedException: $deviceId${cause == null ? '' : ' ($cause)'}';
}

class BleReadTooLarge implements Exception {
  const BleReadTooLarge(this.deviceId, this.length, this.cap);
  final String deviceId;
  final int length;
  final int cap;
  @override
  String toString() =>
      'BleReadTooLarge: $deviceId returned $length bytes (cap $cap)';
}

/// Cap on a single GATT read / notification payload before surfacing a typed
/// exception. 4 KB exceeds every legitimate lamp payload (largest is the
/// wispStatus JSON at ~230 bytes); bigger means malformed firmware or an OOM
/// probe.
const int kBleMaxReadBytes = 4096;

/// True when [e] looks like a disconnect / link-dropped error from any source
/// (this module's typed exception, fbp's various reworded "disconnected" /
/// "not connected" messages). Use only at boundaries that compose other
/// transient signals (auth timeouts, discoverServices failures); single-
/// exception sites should prefer `on BleDisconnectedException catch (_)`.
bool isBleDisconnectError(Object e) {
  if (e is BleDisconnectedException || e is BleNotConnected) return true;
  final msg = e.toString().toLowerCase();
  return msg.contains('disconnect') || msg.contains('not connected');
}

abstract class BleClient {
  Future<void> connect(String deviceId);
  Future<void> disconnect(String deviceId);
  bool isConnected(String deviceId);

  /// Best-effort: open a GATT connection to [deviceId] and prime the
  /// service-handle cache so a later [connect] / first op skips cold-connect
  /// latency. Fire-and-forget from the adv stream; failures are swallowed and
  /// an already-connected / in-flight device is a no-op. Implementations MUST
  /// keep at most ONE prewarm in flight across all device ids: it holds the
  /// lamp's peripheral and degrades mesh airtime, so fanning out hurts the fleet.
  Future<void> prewarm(String deviceId) async {}

  Future<Uint8List> read(String deviceId, String serviceUuid, String charUuid);
  /// Writes [value] to the characteristic.
  ///
  /// [withoutResponse] = true picks GATT write-no-response (the
  /// characteristic must advertise WRITE_NR): unACKed writes let a stream of
  /// slider-rate updates go out at the radio's raw rate. Use it for live-
  /// preview channels (brightness, colors, knockout, home-mode-focus); leave
  /// false where the caller needs to know the write landed (auth,
  /// settings_blob, expression_op).
  ///
  /// [allowLongWrite] = true enables fbp's prepare/execute-write for payloads
  /// > MTU (up to the 512-byte BLE characteristic-value max). Mutually
  /// exclusive with `withoutResponse`. Use it for settings_blob writes.
  Future<void> write(
    String deviceId,
    String serviceUuid,
    String charUuid,
    Uint8List value, {
    bool withoutResponse = false,
    bool allowLongWrite = false,
  });
  Stream<Uint8List> subscribe(
    String deviceId,
    String serviceUuid,
    String charUuid,
  );

  /// Reads a named section via the page protocol: writes the name to
  /// CHAR_PAGE_CTRL, then loops CHAR_PAGE_DATA until a short chunk
  /// (< kPageChunkSize) arrives. Returns concatenated bytes for the caller to
  /// jsonDecode. Known names: "lamp", "base", "shade", "expr", "home",
  /// "nearby"; unknown yields 0 bytes. Throws [BleDisconnectedException] if
  /// the link drops mid-stream (partial bytes discarded; reconnect retries).
  Future<Uint8List> readSection(String deviceId, String name);

  /// Emits the current connection state immediately on listen, then emits on
  /// every change. Used by callers to react to unsolicited link drops
  /// (e.g. LINK_SUPERVISION_TIMEOUT on Android).
  Stream<bool> watchConnected(String deviceId);

  /// Tier-3 recovery: force-drop and re-establish the link to [deviceId].
  /// Soft cycle (explicit disconnect + delay + connect), NOT a BT adapter
  /// toggle (Android 12+ requires user dialogs for `BluetoothAdapter.disable`).
  /// The disconnect + delay lets the OS release a `gatts_if` slot fbp may be
  /// holding on a dead connection. Best-effort: implementations catch their
  /// own errors. Abstract so a test fake can't silently no-op the escalation.
  Future<void> cycleAdapter(String deviceId);
}

/// Per-chunk payload on the BLE page protocol: ATT_MTU 247 minus the 3-byte
/// header. Hardcoded on both sides (fbp 2.x doesn't reliably surface the
/// negotiated MTU); if firmware's kPageMaxChunkSize changes, move this in
/// lockstep.
const int kPageChunkSize = 244;
