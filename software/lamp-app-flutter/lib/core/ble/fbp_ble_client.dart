import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_blue_plus/flutter_blue_plus.dart' as fbp;

import 'ble_client.dart';
import 'scan_pause.dart';

/// Production [BleClient] backed by flutter_blue_plus. Each public method
/// resolves the device + service + characteristic from the plugin's live
/// registry, errors out with a friendly exception type when it can't.
class FbpBleClient extends BleClient {
  /// Cache of discovered GATT services per device id. Populated lazily on the
  /// first `read`/`write`/`subscribe` after a successful connect. Without this
  /// every BLE write would re-run a full service discovery on the radio,
  /// which is what tipped the lamp into LINK_SUPERVISION_TIMEOUT during rapid
  /// slider drags. Cleared on disconnect so a reconnect gets a fresh discovery.
  final Map<String, List<fbp.BluetoothService>> _serviceCache = {};

  // Also clears the cache when a link drops on its own: a caller woken by the
  // next connected edge resolves before [connect] returns.
  StreamSubscription<fbp.OnConnectionStateChangedEvent>? _dropWatch;

  @override
  Future<void> connect(String deviceId) async {
    _dropWatch ??= fbp.FlutterBluePlus.events.onConnectionStateChanged
        .listen((e) {
      if (e.connectionState == fbp.BluetoothConnectionState.disconnected) {
        _serviceCache.remove(e.device.remoteId.str);
      }
    });
    final device = fbp.BluetoothDevice(
      remoteId: fbp.DeviceIdentifier(deviceId),
    );
    await scanPause.during(() async {
      if (fbp.FlutterBluePlus.isScanningNow) {
        await fbp.FlutterBluePlus.stopScan();
      }
      await device.connect(
        license: fbp.License.nonprofit,
        autoConnect: false,
        timeout: const Duration(seconds: 8),
      );
    });
    // Ask Android for a tighter connection interval (11.25-15ms vs the
    // default ~49ms), wraps BluetoothGatt.requestConnectionPriority(
    // CONNECTION_PRIORITY_HIGH). Without this, slider-rate live writes
    // ceiling at ~11Hz (one write per pair of connection events) even
    // with WRITE_NR and a rate-paced WriteCoalescer. HIGH costs the
    // phone some battery while connected, but the lamp is line-powered
    // and the user is actively interacting: fair trade. iOS ignores;
    // some Androids decline; either way swallow the error.
    try {
      await device.requestConnectionPriority(
        connectionPriorityRequest: fbp.ConnectionPriority.high,
      );
    } catch (_) {
      // best-effort: not all platforms honor this
    }
    // Drop the Dart-side service cache. Handles may have changed since
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

  @override
  int mtu(String deviceId) {
    for (final d in fbp.FlutterBluePlus.connectedDevices) {
      if (d.remoteId.str == deviceId) return d.mtuNow;
    }
    return 0;
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

    // Reuse the cached service list when present. discoverServices() is a
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

  /// Inspect an fbp exception and rethrow it as one of the typed
  /// exceptions here when the shape is unambiguous. Centralised so the
  /// string-match for fbp's reworded error messages lives in exactly
  /// ONE place at the platform boundary.
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
      // Size cap: refuses payloads > kBleMaxReadBytes before they reach any
      // jsonDecode path, bounding memory against a hostile lamp or a runaway
      // firmware notification.
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
    // lastValueStream replays fbp's cached value on listen (a disconnect
    // clears the cache) and echoes this app's own writes.
    yield* capNotifyBytes(ch.lastValueStream);
  }

  @override
  Stream<Uint8List> subscribeNotifyOnly(String d, String s, String c) async* {
    final ch = await _resolve(d, s, c);
    await ch.setNotifyValue(true);
    yield* capNotifyBytes(ch.onValueReceived);
  }

  @override
  Future<Uint8List> readSection(String deviceId, String name) =>
      readSectionVia(this, deviceId, name);

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
    // Force-release any handles fbp holds on this device. Clears the
    // service cache too; re-discovers on the next connect.
    _serviceCache.remove(deviceId);
    try {
      await device.disconnect();
    } catch (_) {
      // already disconnected, or fbp didn't have a handle: both fine.
    }
    // Give the Android BT stack a beat to actually release the
    // gatts_if slot before the next connect. Empirically, < 500ms
    // tends to hit the same dead slot; 1.5s is the smallest delay
    // that reliably clears in the field.
    await Future<void>.delayed(const Duration(milliseconds: 1500));
  }
}
