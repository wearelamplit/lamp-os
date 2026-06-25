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
  /// until a short chunk (< kPageChunkSize) arrives. Returns the
  /// concatenated bytes; the caller jsonDecodes them.
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

/// Per-chunk payload size on the BLE page protocol. Pinned to ATT_MTU
/// 247 minus the 3-byte ATT header. Both sides have this hardcoded so
/// the helper's "short chunk = done" heuristic doesn't need to thread
/// the negotiated MTU through the app — flutter_blue_plus 2.x doesn't
/// reliably surface that value anyway. If the firmware-side
/// kPageMaxChunkSize ever changes, this constant moves in lockstep.
const int kPageChunkSize = 244;

/// Test/dev fake. Records writes per (device, service, char) key, lets tests
/// schedule encryption failures, and broadcasts writes to subscribers.
class InMemoryBleClient implements BleClient {
  final Set<String> _connected = {};
  final Map<String, Uint8List> _values = {};
  final Map<String, StreamController<Uint8List>> _streams = {};
  final Map<String, StreamController<bool>> _connStreams = {};
  final Map<String, int> _pendingEncryptionFails = {};
  // Page-protocol section bytes keyed by 'deviceId|sectionName'. Tests
  // seed entries via [seedSection] (or the `seedControlBle` helper);
  // [readSection] returns the seeded bytes verbatim. Modelling the
  // wire CTRL+DATA cursor in the fake adds bugs without value — the
  // observable contract is "given a section name, return its bytes",
  // matching the production helper's caller-visible behavior.
  final Map<String, Uint8List> _sectionValues = {};

  // Write log — captures every write call so tests can assert what
  // landed on a given (deviceId, charUuid) pair. The existing _values
  // map keys by (deviceId, serviceUuid, charUuid) and stores only the
  // LATEST value; the log preserves order + history.
  final List<({String deviceId, String charUuid, Uint8List value})>
      _writeLog = [];

  String _key(String d, String s, String c) => '$d|$s|$c';

  StreamController<bool> _ensureConnStream(String deviceId) {
    // Intentionally never closed — same lifetime contract as _streams
    // above. Lives with the InMemoryBleClient.
    return _connStreams.putIfAbsent(
      deviceId,
      // ignore: close_sinks
      () => StreamController<bool>.broadcast(),
    );
  }

  void scheduleEncryptionFailure(String d, String s, String c) {
    final key = _key(d, s, c);
    _pendingEncryptionFails[key] = (_pendingEncryptionFails[key] ?? 0) + 1;
  }

  /// Seed the bytes [readSection] will return for the given device + name.
  /// Used by `seedControlBle` and tests that drive the per-section sweep.
  void seedSection(String deviceId, String name, Uint8List bytes) {
    _sectionValues['$deviceId|$name'] = bytes;
  }

  /// Returns all values written to [charUuid] for [deviceId], in order.
  /// Unlike [_values] (which stores only the latest value per key),
  /// this log preserves every write call — useful for asserting write
  /// counts and payloads in order-sensitive tests.
  List<Uint8List> writesTo(String deviceId, String charUuid) {
    return _writeLog
        .where((w) => w.deviceId == deviceId && w.charUuid == charUuid)
        .map((w) => w.value)
        .toList();
  }

  @override
  Future<void> connect(String deviceId) async {
    _connected.add(deviceId);
    _ensureConnStream(deviceId).add(true);
  }

  @override
  Future<void> prewarm(String deviceId) async {
    // No-op for tests. Production tests that want to assert pre-warm
    // behavior can use `connect()` directly + verify isConnected — the
    // observable side-effect is the same.
  }

  @override
  Future<void> disconnect(String deviceId) async {
    _connected.remove(deviceId);
    _ensureConnStream(deviceId).add(false);
  }

  @override
  bool isConnected(String deviceId) => _connected.contains(deviceId);

  @override
  Future<Uint8List> read(String d, String s, String c) async {
    if (!_connected.contains(d)) throw BleNotConnected(d);
    final v = _values[_key(d, s, c)];
    if (v == null) throw BleNotFound('$d/$s/$c');
    if (v.length > kBleMaxReadBytes) {
      throw BleReadTooLarge(d, v.length, kBleMaxReadBytes);
    }
    return v;
  }

  @override
  Future<Uint8List> readSection(String deviceId, String name) async {
    if (!_connected.contains(deviceId)) throw BleNotConnected(deviceId);
    final v = _sectionValues['$deviceId|$name'];
    // Fake parity: an unseeded section returns 0 bytes — matches the
    // firmware behavior of returning empty when the snapshot is empty
    // (e.g. unknown section name).
    return v ?? Uint8List(0);
  }

  @override
  Future<void> write(
    String d,
    String s,
    String c,
    Uint8List v, {
    bool withoutResponse = false,
    bool allowLongWrite = false,
  }) async {
    if (!_connected.contains(d)) throw BleNotConnected(d);
    final key = _key(d, s, c);
    final count = _pendingEncryptionFails[key] ?? 0;
    if (count > 0) {
      _pendingEncryptionFails[key] = count - 1;
      throw BleEncryptionRequired(d);
    }
    _values[key] = v;
    // Production semantics: `FbpBleClient` does NOT echo writes back to
    // subscribers — the only thing that triggers a notification is when
    // the lamp explicitly pushes. The fake used to echo here, which let
    // subscribe-then-write-and-expect-the-write-back patterns pass tests
    // while breaking on real hardware. Audit cq-C1. Tests that want to
    // observe writes use `simulateNotify` (see notifyable_ble_client
    // patterns in firmware_ota_pusher_test).
    // Append to write log so tests can query full write history.
    _writeLog.add((
      deviceId: d,
      charUuid: c,
      value: Uint8List.fromList(v),
    ));
  }

  /// Test-only: inject `value` into the (d, s, c) notification stream
  /// AS IF the lamp had pushed it. Use this instead of relying on
  /// `write()` to echo to subscribers.
  void simulateNotify(String d, String s, String c, Uint8List value) {
    final key = _key(d, s, c);
    _streams[key]?.add(value);
  }

  @override
  Stream<Uint8List> subscribe(String d, String s, String c) {
    if (!_connected.contains(d)) throw BleNotConnected(d);
    final key = _key(d, s, c);
    // Intentionally never closed — _streams is a per-key cache of
    // broadcast controllers that lives for the InMemoryBleClient's
    // process lifetime. Tests reuse the same client across calls;
    // closing on first subscriber cancel would break the next read.
    // ignore: close_sinks
    final ctrl = _streams.putIfAbsent(
      key,
      () => StreamController<Uint8List>.broadcast(),
    );
    return ctrl.stream;
  }

  @override
  Future<void> cycleAdapter(String deviceId) async {
    // Test/dev fake: model the soft-cycle's observable effect (the
    // connection state goes false then back true if we were connected)
    // so reconnect-ladder tests can assert that a Tier-3 escalation
    // emits a fresh disconnect/connect edge pair.
    if (_connected.contains(deviceId)) {
      _ensureConnStream(deviceId).add(false);
      _connected.remove(deviceId);
    }
  }

  @override
  Stream<bool> watchConnected(String deviceId) {
    // ignore: close_sinks (lives in _connStreams cache; never closed)
    final ctrl = _ensureConnStream(deviceId);
    // Subscribe to the broadcast stream BEFORE emitting the initial value so
    // that no events are dropped between the seed yield and yield*. Using a
    // StreamController lets us prepend the current state without the async*
    // generator's subscription-before-yield race condition. Closed via
    // `out.onCancel = sub.cancel` (below) when the consumer cancels.
    // ignore: close_sinks
    final out = StreamController<bool>();
    final sub = ctrl.stream.listen(
      out.add,
      onError: out.addError,
      onDone: out.close,
    );
    out.onCancel = sub.cancel;
    // Emit current state immediately so callers don't have to call isConnected
    // separately to seed their state.
    out.add(isConnected(deviceId));
    return out.stream;
  }
}
