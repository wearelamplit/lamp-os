import 'dart:async';
import 'dart:convert';

import 'package:http/http.dart' as http;
import 'package:web_socket_channel/web_socket_channel.dart';

class BridgeResponse {
  const BridgeResponse(this.status, this.json);
  final int status;
  final Map<String, dynamic> json;
  bool get ok => status >= 200 && status < 300;
}

/// Transport seam between the dev BLE impls and the host bridge. Abstract so
/// NetworkDevBleClient/Scanner are unit-testable against a fake transport with
/// no sockets. The real impl targets `http://<host>` + `ws://<host>`.
abstract class BridgeTransport {
  Future<BridgeResponse> request(String method, String path, {Object? body});

  /// Opens a persistent WS channel; each message is a decoded JSON map.
  /// Reconnects internally; a WS blip must NOT be surfaced as a BLE event.
  Stream<Map<String, dynamic>> openChannel(String path);

  Future<void> close();
}

class HttpBridgeTransport implements BridgeTransport {
  HttpBridgeTransport(this.host);
  final String host; // e.g. 'localhost:8080' or '10.0.2.2:8080'

  static const _requestTimeout = Duration(seconds: 10);

  final List<StreamController<Map<String, dynamic>>> _channels = [];
  final List<WebSocketChannel> _sockets = [];
  bool _closed = false;

  @override
  Future<BridgeResponse> request(String method, String path,
      {Object? body}) async {
    final uri = Uri.parse('http://$host$path');
    final req = http.Request(method, uri);
    if (body != null) {
      req.headers['content-type'] = 'application/json';
      req.body = jsonEncode(body);
    }
    Future<http.Response> send() async =>
        http.Response.fromStream(await req.send());
    final resp = await send().timeout(_requestTimeout,
        onTimeout: () =>
            throw TimeoutException('bridge $method $path', _requestTimeout));
    final map = resp.body.isEmpty
        ? const <String, dynamic>{}
        : jsonDecode(resp.body) as Map<String, dynamic>;
    return BridgeResponse(resp.statusCode, map);
  }

  @override
  Stream<Map<String, dynamic>> openChannel(String path) {
    final ctrl = StreamController<Map<String, dynamic>>.broadcast();
    _channels.add(ctrl);
    void connect() {
      final ch = WebSocketChannel.connect(Uri.parse('ws://$host$path'));
      _sockets.add(ch);
      ch.stream.listen(
        (data) {
          if (!ctrl.isClosed) {
            ctrl.add(jsonDecode(data as String) as Map<String, dynamic>);
          }
        },
        onDone: () {
          if (!_closed && !ctrl.isClosed) {
            Future.delayed(
                const Duration(seconds: 1), connect); // WS blip ≠ BLE event
          }
        },
        onError: (_) {},
      );
    }

    connect();
    return ctrl.stream;
  }

  @override
  Future<void> close() async {
    _closed = true;
    for (final ch in _sockets) {
      await ch.sink.close();
    }
    _sockets.clear();
    for (final ctrl in _channels) {
      await ctrl.close();
    }
    _channels.clear();
  }
}
