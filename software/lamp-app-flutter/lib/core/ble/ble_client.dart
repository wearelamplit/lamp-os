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

/// Thrown when a BLE op fails because the underlying link dropped — most
/// commonly because the lamp rebooted mid-write (settings_blob persist +
/// fade-out + reset is the standard "save" path; the link drops as the
/// radio comes down). Distinct from [BleNotConnected]: that one means
/// "we never had a connection," this one means "we did, the lamp
/// disconnected us."
///
/// Production callers should `on BleDisconnectedException catch (_)`
/// instead of string-matching `e.toString().contains('disconnect')` —
/// the latter breaks every time flutter_blue_plus reworks its error
/// surface. Audit cq-H (W7.8).
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

/// Cap on the size of a single GATT read or notification payload before
/// we surface a typed exception. 4 KB comfortably exceeds every
/// legitimate lamp-side payload (the largest is the wispStatus JSON
/// at ~230 bytes per CONTROL_MAX_PAYLOAD); anything bigger is either
/// malformed firmware or an attacker probing for OOM.
const int kBleMaxReadBytes = 4096;

/// True when [e] is shaped like a disconnect / link-dropped error from
/// any source (the typed exception from this module, fbp's various
/// reworded "device disconnected" / "not connected" messages, etc.).
///
/// Use this only at boundaries where the underlying client hasn't
/// already wrapped the failure into [BleDisconnectedException] — e.g.
/// retry-loop classifiers that compose other transient signals (auth
/// timeouts, discoverServices failures) and want one helper that
/// recognises them all. Single-exception call sites should prefer
/// `on BleDisconnectedException catch (_)` directly.
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
  /// service-handle cache so that a subsequent [connect] / first
  /// read/write skips the cold-connect latency (200-500 ms GATT
  /// handshake + 100-400 ms service discovery on most platforms).
  ///
  /// Designed to be called fire-and-forget from the BLE adv stream as
  /// soon as a paired lamp pops into range. Failures are swallowed —
  /// pre-warming is opportunistic, never on the critical path. If a
  /// pre-warm is already in flight or the device is already connected,
  /// this should be a no-op.
  ///
  /// Implementations MUST guarantee that at most ONE pre-warm is in
  /// flight at a time across all device ids — pre-warming holds the
  /// lamp's BLE peripheral and degrades its mesh airtime even at WIDE
  /// conn-params, so fanning out to every paired lamp in range would
  /// hurt the fleet.
  Future<void> prewarm(String deviceId) async {}

  Future<Uint8List> read(String deviceId, String serviceUuid, String charUuid);
  /// Writes [value] to the characteristic.
  ///
  /// [withoutResponse] = true picks the GATT write-no-response op (the
  /// characteristic must advertise WRITE_NR). The peer doesn't ACK each
  /// write, which lets a continuous stream of slider-rate writes go out
  /// at the radio's raw rate instead of round-tripping per write — the
  /// difference was ~5 Hz vs ~30 Hz of color updates landing on the lamp
  /// at the standard ~49 ms connection interval. Use it for live-preview
  /// channels (brightness, colors, knockout, home-mode-focus). Leave
  /// false for ops where the caller needs to know the write landed
  /// (auth, settings_blob, expression_op).
  ///
  /// [allowLongWrite] = true enables fbp's prepare/execute-write
  /// sequence for payloads > MTU (up to 512 bytes — BLE protocol max
  /// for a characteristic value). Mutually exclusive with
  /// `withoutResponse`. Set this for settings_blob writes which can
  /// grow to several hundred bytes once expressions are added.
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

  /// Reads a named section from the lamp via the page protocol. Writes
  /// the section name to CHAR_PAGE_CTRL, then loops reading CHAR_PAGE_DATA
  /// until an empty chunk arrives (the lamp's end-of-snapshot signal).
  /// Returns the concatenated bytes; the caller jsonDecodes them.
  ///
  /// Known section names match the lamp's dispatch table: "lamp", "base",
  /// "shade", "expr", "home", "nearby". An unknown name results in
  /// 0 bytes (the lamp's CHAR_PAGE_DATA returns empty when the snapshot
  /// is empty); the caller will see an empty Uint8List which jsonDecode
  /// rejects.
  ///
  /// Throws [BleDisconnectedException] mid-stream if the link drops
  /// between the CTRL write and the final DATA read. Partial bytes are
  /// discarded — the caller should let the surrounding reconnect ladder
  /// retry the whole section.
  Future<Uint8List> readSection(String deviceId, String name);

  /// Emits the current connection state immediately on listen, then emits on
  /// every change. Used by callers to react to unsolicited link drops
  /// (e.g. LINK_SUPERVISION_TIMEOUT on Android).
  Stream<bool> watchConnected(String deviceId);

  /// Tier-3 recovery: force-drop and re-establish the link to [deviceId].
  /// Soft cycle (explicit disconnect + delay + connect), NOT a literal
  /// BT adapter toggle — Android 12+ requires user dialogs for
  /// programmatic `BluetoothAdapter.disable()`, which is unacceptable
  /// in a recovery path that runs after the link has silently zombified.
  ///
  /// Intended for the "force-stop fixes it" pattern: after N soft
  /// reconnects have failed, the Android `gatts_if` slot may be held by
  /// fbp internally on a dead connection. An explicit disconnect +
  /// short delay gives the OS a chance to release the slot before the
  /// next connect attempt.
  ///
  /// Best-effort: implementations should catch their own inner errors so
  /// the caller doesn't have to. Returns when the cycle attempt completes
  /// (success or quiet failure).
  ///
  /// Abstract — a default no-op would let a future test fake silently
  /// swallow Tier-3 escalation, hiding a regression where the ladder
  /// never actually cycles the slot. Implementations must override.
  Future<void> cycleAdapter(String deviceId);
}
