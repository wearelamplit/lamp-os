import 'dart:async';
import 'dart:typed_data';

import 'package:lamp_app/core/ble/ble_client.dart';

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
    // while breaking on real hardware. Tests that want to
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
