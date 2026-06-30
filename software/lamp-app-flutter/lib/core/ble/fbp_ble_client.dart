import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_blue_plus/flutter_blue_plus.dart' as fbp;

import 'ble_client.dart';
import 'uuids.dart';

/// Production [BleClient] backed by flutter_blue_plus. Each public method
/// resolves the device + service + characteristic from the plugin's live
/// registry, errors out with a friendly exception type when it can't.
class FbpBleClient implements BleClient {
  /// Cache of discovered GATT services per device id. Populated lazily on the
  /// first `read`/`write`/`subscribe` after a successful connect. Without this
  /// every BLE write would re-run a full service discovery on the radio,
  /// which is what tipped the lamp into LINK_SUPERVISION_TIMEOUT during rapid
  /// slider drags. Cleared on disconnect so a reconnect gets a fresh discovery.
  final Map<String, List<fbp.BluetoothService>> _serviceCache = {};

  /// Single-slot mutex for pre-warm. Pre-warming holds the lamp's BLE
  /// peripheral and consumes some of its mesh airtime even at WIDE
  /// conn-params, so we cap concurrent pre-warms at one — the latest
  /// caller waits behind the in-flight one or no-ops if it's already
  /// connected by then.
  Future<void>? _prewarmInFlight;

  @override
  Future<void> prewarm(String deviceId) async {
    if (isConnected(deviceId)) return;
    final inFlight = _prewarmInFlight;
    if (inFlight != null) {
      // Another pre-warm is in progress. Drop this request — the
      // pre-warm payoff is "have a hot connection ready when the user
      // taps"; if the in-flight pre-warm targets a different lamp, the
      // user is no worse off than today.
      return;
    }
    _prewarmInFlight = (() async {
      try {
        await connect(deviceId);
        // Force service discovery now so the cache is hot before the
        // user taps. _resolve() does this lazily on first I/O — we just
        // do it eagerly here.
        final device = fbp.BluetoothDevice(
          remoteId: fbp.DeviceIdentifier(deviceId),
        );
        _serviceCache[deviceId] = await device.discoverServices();
      } catch (_) {
        // Best-effort — never let a pre-warm failure surface. If the
        // user does end up tapping this lamp, the normal connect path
        // will re-try and report any real failure.
        try {
          await disconnect(deviceId);
        } catch (_) {}
      } finally {
        _prewarmInFlight = null;
      }
    })();
    await _prewarmInFlight;
  }

  @override
  Future<void> connect(String deviceId) async {
    final device = fbp.BluetoothDevice(
      remoteId: fbp.DeviceIdentifier(deviceId),
    );
    // NOTE on scan/connect coex (audit L2): we do NOT explicitly pause
    // the background scan here. flutter_blue_plus serializes scan and
    // connect operations internally on both Android (BluetoothLeScanner
    // pause during gatt.connect) and iOS (CoreBluetooth's
    // centralManager will defer scan callbacks during a connect-in-
    // progress). If we ever see GATT MTU exchange failures correlated
    // with continuous scanning (the cited iOS edge case), revisit by
    // calling `FlutterBluePlus.stopScan()` here + a hook to re-issue
    // start in NearbyLampsNotifier after `disconnect()`. Until then,
    // the extra coordination cost (passing scanner refs into the BLE
    // client) isn't worth the imagined gain.
    //
    await device.connect(
      license: fbp.License.nonprofit,
      autoConnect: false,
      timeout: const Duration(seconds: 8),
    );
    // Ask Android for a tighter connection interval (11.25-15ms vs the
    // default ~49ms) — wraps BluetoothGatt.requestConnectionPriority(
    // CONNECTION_PRIORITY_HIGH). Without this, slider-rate live writes
    // ceiling at ~11Hz (one write per pair of connection events) even
    // with WRITE_NR and a rate-paced WriteCoalescer. HIGH costs the
    // phone some battery while connected, but the lamp is line-powered
    // and the user is actively interacting — fair trade. iOS ignores;
    // some Androids decline; either way swallow the error.
    try {
      await device.requestConnectionPriority(
        connectionPriorityRequest: fbp.ConnectionPriority.high,
      );
    } catch (_) {
      // best-effort — not all platforms honor this
    }
    // Drop our Dart-side service cache — handles may have changed since
    // last connect (e.g. firmware re-registered the GATT database), so
    // the next _resolve() does a fresh discoverServices().
    _serviceCache.remove(deviceId);
  }

  @override
  Future<void> disconnect(String deviceId) async {
    final device = fbp.BluetoothDevice(
      remoteId: fbp.DeviceIdentifier(deviceId),
    );
    _serviceCache.remove(deviceId);
    await device.disconnect();
  }

  @override
  bool isConnected(String deviceId) {
    return fbp.FlutterBluePlus.connectedDevices.any(
      (d) => d.remoteId.str == deviceId,
    );
  }

  Future<fbp.BluetoothCharacteristic> _resolve(
    String deviceId,
    String serviceUuid,
    String charUuid,
  ) async {
    final device = fbp.FlutterBluePlus.connectedDevices
        .firstWhere(
          (d) => d.remoteId.str == deviceId,
          orElse: () => throw BleNotFound('device $deviceId not connected'),
        );

    // Reuse the cached service list when present — discoverServices() is a
    // multi-round-trip GATT exchange and re-running it per write floods the
    // link. Only call it on a cold cache (first I/O after connect).
    final services = _serviceCache[deviceId] ??=
        await device.discoverServices();

    final service = services.firstWhere(
      (s) => s.uuid.str128.toLowerCase() == serviceUuid.toLowerCase(),
      orElse: () => throw BleNotFound('service $serviceUuid on $deviceId'),
    );
    final ch = service.characteristics.firstWhere(
      (c) => c.uuid.str128.toLowerCase() == charUuid.toLowerCase(),
      orElse: () => throw BleNotFound('char $charUuid in $serviceUuid'),
    );
    return ch;
  }

  /// Inspect an fbp exception and rethrow it as one of our typed
  /// exceptions when the shape is unambiguous. Centralised so the
  /// string-match for fbp's reworded error messages lives in exactly
  /// ONE place at the platform boundary (audit cq-H / W7.8).
  Never _classifyAndRethrow(String deviceId, Object e) {
    final msg = e.toString().toLowerCase();
    if (msg.contains('encryption')) {
      throw BleEncryptionRequired(deviceId);
    }
    if (msg.contains('disconnect') || msg.contains('not connected')) {
      throw BleDisconnectedException(deviceId, e);
    }
    throw e;
  }

  @override
  Future<Uint8List> read(String d, String s, String c) async {
    try {
      final ch = await _resolve(d, s, c);
      final bytes = await ch.read();
      // Size cap (audit sec-M3) — refuses payloads > kBleMaxReadBytes
      // before they reach any jsonDecode path. Protects the app from a
      // hostile lamp or a runaway firmware notification that would
      // otherwise burn unbounded memory.
      if (bytes.length > kBleMaxReadBytes) {
        throw BleReadTooLarge(d, bytes.length, kBleMaxReadBytes);
      }
      return Uint8List.fromList(bytes);
    } on fbp.FlutterBluePlusException catch (e) {
      _classifyAndRethrow(d, e);
    }
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
    try {
      final ch = await _resolve(d, s, c);
      await ch.write(
        v,
        withoutResponse: withoutResponse,
        allowLongWrite: allowLongWrite,
      );
    } on fbp.FlutterBluePlusException catch (e) {
      _classifyAndRethrow(d, e);
    }
  }

  @override
  Stream<Uint8List> subscribe(String d, String s, String c) async* {
    final ch = await _resolve(d, s, c);
    await ch.setNotifyValue(true);
    yield* ch.lastValueStream.map(Uint8List.fromList);
  }

  @override
  Future<Uint8List> readSection(String deviceId, String name) async {
    try {
      // Write the section name to PAGE_CTRL. The firmware snapshots the
      // section into the conn slot + resets a cursor. CTRL is a normal
      // write-with-response so we know the snapshot is ready before
      // the first DATA read fires.
      await write(
        deviceId,
        BleUuids.controlService,
        BleUuids.pageCtrl,
        Uint8List.fromList(utf8.encode(name)),
      );
      // Pull DATA chunks until an EMPTY one arrives — the lamp's
      // end-of-snapshot signal. Reading until empty is MTU-agnostic: a
      // sub-247-MTU link just serves more, smaller chunks, and a short
      // NON-final chunk must not be mistaken for the end (the bug when
      // this keyed off a hardcoded 244-byte "short = done" threshold).
      // fbp 2.x serializes GATT ops per-device internally, so writes
      // and reads can't interleave on the wire even if our awaits are
      // back-to-back.
      final out = BytesBuilder(copy: false);
      while (true) {
        final chunk = await read(
          deviceId,
          BleUuids.controlService,
          BleUuids.pageData,
        );
        if (chunk.isEmpty) {
          return out.toBytes();
        }
        out.add(chunk);
      }
    } on BleDisconnectedException {
      // Mid-stream drop. Partial bytes are discarded; the surrounding
      // reconnect ladder re-runs the section sweep.
      rethrow;
    }
  }

  @override
  Stream<bool> watchConnected(String deviceId) {
    final device = fbp.BluetoothDevice(
      remoteId: fbp.DeviceIdentifier(deviceId),
    );
    return device.connectionState
        .map((s) => s == fbp.BluetoothConnectionState.connected);
  }

  @override
  Future<void> cycleAdapter(String deviceId) async {
    final device = fbp.BluetoothDevice(
      remoteId: fbp.DeviceIdentifier(deviceId),
    );
    // Force-release any handles fbp holds on this device. Clearing our
    // own service cache too — we'll re-discover on the next connect.
    _serviceCache.remove(deviceId);
    try {
      await device.disconnect();
    } catch (_) {
      // already disconnected, or fbp didn't have a handle — both fine.
    }
    // Give the Android BT stack a beat to actually release the
    // gatts_if slot before the next connect. Empirically, < 500ms
    // tends to hit the same dead slot; 1.5s is the smallest delay
    // that reliably clears in the field.
    await Future<void>.delayed(const Duration(milliseconds: 1500));
  }
}
