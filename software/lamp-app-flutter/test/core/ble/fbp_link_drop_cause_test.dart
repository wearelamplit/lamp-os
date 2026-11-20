import 'dart:async';

import 'package:flutter/services.dart' show PlatformException;
import 'package:flutter_blue_plus/flutter_blue_plus.dart' show ErrorPlatform;
import 'package:flutter_blue_plus_platform_interface/flutter_blue_plus_platform_interface.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/fbp_ble_client.dart';

/// A lamp that connects, drops the link with a given reason code, or has its
/// connect refused by the platform before any connection event.
final class _FakeLamp extends FlutterBluePlusPlatform {
  final _conn = StreamController<BmConnectionStateResponse>.broadcast();
  bool refuse = false;

  @override
  Stream<BmConnectionStateResponse> get onConnectionStateChanged =>
      _conn.stream;

  void _state(String id, BmConnectionStateEnum s, {int? code}) =>
      _conn.add(BmConnectionStateResponse(
        remoteId: DeviceIdentifier(id),
        connectionState: s,
        disconnectReasonCode: code,
        disconnectReasonString: code == null ? null : 'code $code',
      ));

  void drop(String id, int code) =>
      _state(id, BmConnectionStateEnum.disconnected, code: code);

  @override
  Future<bool> connect(BmConnectRequest request) {
    if (refuse) {
      throw PlatformException(code: 'connect', message: 'Bluetooth off');
    }
    scheduleMicrotask(
        () => _state(request.remoteId.str, BmConnectionStateEnum.connected));
    return Future.value(true);
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  // flutter_blue_plus listens to the first platform instance it meets.
  final lamp = _FakeLamp();
  FlutterBluePlusPlatform.instance = lamp;
  final client = FbpBleClient(errorPlatform: ErrorPlatform.android);

  setUp(() => lamp.refuse = false);

  test('the lamp ending a working link reads as a hang-up', () async {
    const id = 'AA:00:00:00:00:01';
    await client.connect(id);
    expect(client.lastDropCause(id), LinkDropCause.other,
        reason: 'a live link has no drop cause');
    lamp.drop(id, 0x13);
    await pumpEventQueue();
    expect(client.lastDropCause(id), LinkDropCause.lampHungUp);
  });

  test('a supervision timeout reads as some other drop', () async {
    const id = 'AA:00:00:00:00:02';
    await client.connect(id);
    lamp.drop(id, 0x08);
    await pumpEventQueue();
    expect(client.lastDropCause(id), LinkDropCause.other);
  });

  test('a connect refused with no event forgets the earlier hang-up',
      () async {
    const id = 'AA:00:00:00:00:03';
    await client.connect(id);
    lamp.drop(id, 0x13);
    await pumpEventQueue();
    lamp.refuse = true;
    await expectLater(client.connect(id), throwsA(anything));
    expect(client.lastDropCause(id), LinkDropCause.other);
  });

  test('a hang-up after a reconnect reads as a hang-up again', () async {
    const id = 'AA:00:00:00:00:04';
    await client.connect(id);
    lamp.drop(id, 0x13);
    await pumpEventQueue();
    await client.connect(id);
    await pumpEventQueue();
    lamp.drop(id, 0x13);
    await pumpEventQueue();
    expect(client.lastDropCause(id), LinkDropCause.lampHungUp);
  });
}
