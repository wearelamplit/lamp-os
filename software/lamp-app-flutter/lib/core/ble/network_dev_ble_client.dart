import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'ble_client.dart';
import 'bridge_protocol.dart';
import 'bridge_transport.dart';

class NetworkDevBleClient implements BleClient {
  NetworkDevBleClient(this._t);
  final BridgeTransport _t;
  final Map<String, bool> _connected = {};
  final Map<String, StreamController<bool>> _connCtrls = {};
  final Set<String> _watchStarted = {};

  Never _throw(String deviceId, BridgeResponse r) {
    final code = bridgeErrorCodeFromWire(r.json['error'] as String? ?? 'UNKNOWN');
    throw bridgeErrorToException(code, deviceId, message: r.json['message'] as String?);
  }

  void _setConnected(String deviceId, bool v) {
    _connected[deviceId] = v;
    _connCtrls[deviceId]?.add(v);
  }

  @override
  Future<void> connect(String deviceId) async {
    final r = await _t.request('POST', '/connect/$deviceId');
    if (!r.ok) _throw(deviceId, r);
    _setConnected(deviceId, true);
  }

  @override
  Future<void> disconnect(String deviceId) async {
    await _t.request('POST', '/disconnect/$deviceId');
    _setConnected(deviceId, false);
  }

  @override
  bool isConnected(String deviceId) => _connected[deviceId] ?? false;

  // The bridge relays over a WebSocket, not a real ATT link, so there is no
  // negotiated MTU to report; 0 sends OTA callers to the safe baseline.
  @override
  int mtu(String deviceId) => 0;

  @override
  Future<void> prewarm(String deviceId) async {}

  @override
  Future<Uint8List> read(String deviceId, String serviceUuid, String charUuid) async {
    final r = await _t.request('GET', '/read/$deviceId/$serviceUuid/$charUuid');
    if (!r.ok) _throw(deviceId, r);
    return Uint8List.fromList(base64Decode(r.json['data'] as String));
  }

  @override
  Future<void> write(
    String deviceId,
    String serviceUuid,
    String charUuid,
    Uint8List value, {
    bool withoutResponse = false,
    bool allowLongWrite = false,
  }) async {
    final r = await _t.request('POST', '/write/$deviceId/$serviceUuid/$charUuid', body: {
      'data': base64Encode(value),
      'withoutResponse': withoutResponse,
      'allowLongWrite': allowLongWrite,
    });
    if (!r.ok) _throw(deviceId, r);
  }

  @override
  Future<Uint8List> readSection(String deviceId, String name) =>
      readSectionVia(this, deviceId, name);

  @override
  Stream<bool> watchConnected(String deviceId) {
    // Subscribe before seeding so no edge is dropped between the seed add and
    // the pipe subscription (firstWhere callers depend on this).
    // ignore: close_sinks
    final ctrl = _connCtrls.putIfAbsent(
        deviceId, () => StreamController<bool>.broadcast());
    // ignore: close_sinks
    final out = StreamController<bool>();
    final sub = ctrl.stream.listen(out.add, onError: out.addError, onDone: out.close);
    out.onCancel = sub.cancel;
    out.add(isConnected(deviceId));
    if (_watchStarted.add(deviceId)) {
      _t.openChannel('/watch/$deviceId').listen((m) => _setConnected(deviceId, m['connected'] == true));
    }
    return out.stream;
  }

  @override
  Future<void> cycleAdapter(String deviceId) async {
    await _t.request('POST', '/disconnect/$deviceId');
    _setConnected(deviceId, false);
    await Future<void>.delayed(const Duration(milliseconds: 300));
    final r = await _t.request('POST', '/connect/$deviceId');
    if (r.ok) _setConnected(deviceId, true);
  }

  @override
  Stream<Uint8List> subscribe(String d, String s, String c) async* {
    final r = await _t.request('POST', '/subscribe/$d/$s/$c');
    if (!r.ok) _throw(d, r);
    yield* capNotifyBytes(_t.openChannel('/notify/$d/$s/$c')
        .map((m) => base64Decode(m['data'] as String)));
  }

  // The bridge notify channel carries only pushed notifications; nothing to
  // strip.
  @override
  Stream<Uint8List> subscribeNotifyOnly(String d, String s, String c) =>
      subscribe(d, s, c);
}
