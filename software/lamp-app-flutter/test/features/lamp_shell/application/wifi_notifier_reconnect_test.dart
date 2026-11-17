import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/lamp_shell/application/wifi_notifier.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../_support/in_memory_ble_client.dart';

const _devId = 'lamp-x';

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

/// Notify delivery as flutter_blue_plus gives it against an unbonded lamp: a
/// new link starts with notifications off until the app subscribes again,
/// and once it has, every listener on the characteristic receives, a
/// listener left over from the old link included.
class _LinkScopedBle extends InMemoryBleClient {
  int _link = 0;
  int? _notifyingLink;

  /// Live listeners on the notify.
  int listeners = 0;

  /// Subscribe calls, each one a notify enable on the lamp.
  int subscribes = 0;

  @override
  Future<void> disconnect(String deviceId) {
    _link++;
    return super.disconnect(deviceId);
  }

  @override
  Stream<Uint8List> subscribe(String d, String s, String c) {
    final src = super.subscribe(d, s, c);
    subscribes++;
    _notifyingLink = _link;
    StreamSubscription<Uint8List>? sub;
    late final StreamController<Uint8List> out;
    out = StreamController<Uint8List>(
      onListen: () {
        listeners++;
        sub = src.where((_) => _notifyingLink == _link).listen(out.add);
      },
      onCancel: () async {
        listeners--;
        await sub?.cancel();
        await out.close();
      },
    );
    return out.stream;
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUp(() => SharedPreferences.setMockInitialValues({}));

  test('wifiState pushed after a reconnect reaches the picker, one listener '
      'per link', () async {
    final ble = _LinkScopedBle();
    await ble.connect(_devId);
    await ble.write(_devId, BleUuids.controlService, BleUuids.wifiState,
        _b('{"state":"idle"}'));
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    c.listen(wifiNotifierProvider(_devId), (_, _) {});
    await c.read(wifiNotifierProvider(_devId).future);
    await pumpEventQueue();
    expect(ble.subscribes, 1, reason: 'the link that was up at build');

    await ble.disconnect(_devId);
    await pumpEventQueue();
    await ble.connect(_devId);
    await pumpEventQueue();

    ble.simulateNotify(_devId, BleUuids.controlService, BleUuids.wifiState,
        _b('{"state":"connected"}'));
    await pumpEventQueue();
    expect(c.read(wifiNotifierProvider(_devId)).value!.state, 'connected');
    expect(ble.subscribes, 2);
    expect(ble.listeners, 1,
        reason: 'the old link\'s listener would hear every notify twice');
  });
}
