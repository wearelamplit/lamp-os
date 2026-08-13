import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/foundation.dart';

import 'uuids.dart';


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

/// Applies [kBleMaxReadBytes] to a notify stream, dropping an oversized frame
/// (not throwing) so one bad payload keeps the subscription alive. The read()
/// path throws because a caller awaits a single value; a stream must survive.
/// Real BLE notifies are MTU-bounded, but the dev bridge relays them over a
/// WebSocket that is not.
Stream<Uint8List> capNotifyBytes(Stream<List<int>> src) {
  return src.where((b) {
    if (b.length > kBleMaxReadBytes) {
      debugPrint('[ble] dropped oversized notify: ${b.length} > $kBleMaxReadBytes');
      return false;
    }
    return true;
  }).map(Uint8List.fromList);
}

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

/// Runaway guard on a whole page-protocol section (the sum of its DATA
/// chunks), distinct from the per-chunk [kBleMaxReadBytes]. A section is
/// heap-built firmware-side with no fixed cap, so this sits well above any
/// real section (the largest, an expr list, is a few KB); it exists only to
/// bound an unbounded accumulate if the lamp never sends the empty terminator,
/// not to validate size.
const int kBleMaxSectionBytes = 65536;

/// Read page-protocol DATA chunks from [nextChunk] until an empty chunk (the
/// lamp's end-of-snapshot signal), concatenating them. Reading-until-empty is
/// MTU-agnostic: a short NON-final chunk must NOT be mistaken for the end (the
/// bug when this keyed off a hardcoded "short = done" threshold). Throws
/// [BleReadTooLarge] if the total passes [cap] before the terminator arrives.
/// A wedged firmware cursor otherwise loops forever / OOMs the caller.
Future<Uint8List> readPagesUntilEmpty(
  String deviceId,
  Future<Uint8List> Function() nextChunk, {
  int cap = kBleMaxSectionBytes,
}) async {
  final out = BytesBuilder(copy: false);
  while (true) {
    final chunk = await nextChunk();
    if (chunk.isEmpty) return out.toBytes();
    out.add(chunk);
    if (out.length > cap) throw BleReadTooLarge(deviceId, out.length, cap);
  }
}

/// Serializes page-protocol sessions per device. The firmware keeps ONE
/// shared page snapshot + cursor per connection, so two overlapping
/// CTRL+DATA pairs corrupt each other (a CTRL write mid-DATA-drain resets
/// the cursor and the in-flight reader pulls the other section's bytes).
final Map<String, Future<void>> _pageSessions = {};

/// Reads a named page-protocol section from any [BleClient]: write the section
/// name to CHAR_PAGE_CTRL, then pull CHAR_PAGE_DATA chunks until the empty
/// end-of-snapshot chunk. Serialized per [deviceId] so a CTRL write can never
/// interleave into another section's DATA drain on the same connection.
Future<Uint8List> readSectionVia(
  BleClient client,
  String deviceId,
  String name,
) {
  Future<Uint8List> run() => client
      .write(deviceId, BleUuids.controlService, BleUuids.pageCtrl,
          Uint8List.fromList(utf8.encode(name)))
      .then((_) => readPagesUntilEmpty(
            deviceId,
            () => client.read(deviceId, BleUuids.controlService, BleUuids.pageData),
          ));

  final prior = _pageSessions[deviceId] ?? Future<void>.value();
  final result = prior.then((_) => run());
  // Chain the next session behind this one regardless of outcome; a failure
  // must not wedge the queue.
  _pageSessions[deviceId] =
      result.then((_) {}, onError: (_) {});
  return result;
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

  /// Best-effort current negotiated ATT MTU for [deviceId], or 0 if
  /// unknown (not connected yet, negotiation still in flight, or the
  /// transport doesn't expose one). Callers must treat 0 as "fall back
  /// to the safe baseline chunk size", never as a literal MTU.
  int mtu(String deviceId) => 0;

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

  /// Like [subscribe], but emits ONLY frames the lamp pushes: no cached
  /// last-value replay on listen, no echo of the app's own writes. Use for
  /// request/response characteristics (OTA control) where a replayed stale
  /// frame reads as a live response.
  Stream<Uint8List> subscribeNotifyOnly(
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
  /// discarded; the caller should let the surrounding reconnect ladder
  /// retry the whole section.
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
