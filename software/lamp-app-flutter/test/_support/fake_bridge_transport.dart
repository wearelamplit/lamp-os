import 'dart:async';
import 'package:lamp_app/core/ble/bridge_transport.dart';

class FakeBridgeTransport implements BridgeTransport {
  final List<String> calls = [];
  final Map<String, BridgeResponse> responses = {}; // 'METHOD path' -> resp
  final Map<String, StreamController<Map<String, dynamic>>> channels = {};

  @override
  Future<BridgeResponse> request(String method, String path, {Object? body}) async {
    calls.add('$method $path');
    return responses['$method $path'] ?? const BridgeResponse(200, {});
  }

  @override
  Stream<Map<String, dynamic>> openChannel(String path) =>
      channels.putIfAbsent(path, () => StreamController.broadcast()).stream;

  void pushChannel(String path, Map<String, dynamic> msg) =>
      channels[path]!.add(msg);

  @override
  Future<void> close() async {}
}
