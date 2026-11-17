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
/// listener left over from the old link included. A subscription enables
/// the notify a turn after a read issued alongside it, then emits a first
/// frame, empty as flutter_blue_plus replays it once its cache is cleared.
class _LinkScopedBle extends InMemoryBleClient {
  int _link = 0;
  int? _notifyingLink;

  /// Live listeners on the notify.
  int listeners = 0;

  /// Subscribe calls, each one a notify enable on the lamp.
  int subscribes = 0;

  /// Value a read returns once the link has dropped; null throws.
  Uint8List? readAfterDrop;

  /// Lamp change pushed just before the next notify enable, so only a read
  /// made after the enable sees it.
  Uint8List? changeBeforeEnable;

  /// Holds dropped-link reads until completed.
  Completer<Uint8List>? readGate;

  @override
  Future<void> disconnect(String deviceId) {
    _link++;
    return super.disconnect(deviceId);
  }

  @override
  Future<Uint8List> read(String d, String s, String c) {
    if (_link > 0) {
      final gate = readGate;
      if (gate != null) return gate.future;
      final v = readAfterDrop;
      if (v == null) throw StateError('read unavailable');
      return Future.value(v);
    }
    return super.read(d, s, c);
  }

  @override
  Stream<Uint8List> subscribe(String d, String s, String c) {
    final src = super.subscribe(d, s, c);
    subscribes++;
    final link = _link;
    StreamSubscription<Uint8List>? sub;
    late final StreamController<Uint8List> out;
    out = StreamController<Uint8List>(
      onListen: () {
        listeners++;
        sub = src.where((_) => _notifyingLink == _link).listen(out.add);
        Future(() async {
          if (out.isClosed) return;
          final change = changeBeforeEnable;
          if (change != null) {
            changeBeforeEnable = null;
            if (readAfterDrop != null) readAfterDrop = change;
            await super.write(d, s, c, change);
            simulateNotify(d, s, c, change);
          }
          _notifyingLink = link;
          out.add(Uint8List(0));
        });
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
    final ble = _LinkScopedBle()..readAfterDrop = _b('{"state":"idle"}');
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

  Future<(_LinkScopedBle, ProviderContainer)> openPicker(String json) async {
    final ble = _LinkScopedBle();
    await ble.connect(_devId);
    await ble.write(
        _devId, BleUuids.controlService, BleUuids.wifiState, _b(json));
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    c.listen(wifiNotifierProvider(_devId), (_, _) {});
    await c.read(wifiNotifierProvider(_devId).future);
    await pumpEventQueue();
    return (ble, c);
  }

  Future<void> bounce(_LinkScopedBle ble) async {
    await ble.disconnect(_devId);
    await pumpEventQueue();
    await ble.connect(_devId);
    await pumpEventQueue();
  }

  test('the empty value replayed on re-subscribe does not blank the state',
      () async {
    final (ble, c) = await openPicker('{"state":"scanning"}');
    await bounce(ble);
    expect(c.read(wifiNotifierProvider(_devId)).value!.state, 'scanning');
  });

  test('a change made while the link was down is read back on reconnect',
      () async {
    final (ble, c) = await openPicker('{"state":"scanning"}');
    ble.readAfterDrop = _b(
        '{"state":"idle","scanResults":[{"ssid":"home","rssi":-50,"encrypted":true}]}');
    await bounce(ble);
    final w = c.read(wifiNotifierProvider(_devId)).value!;
    expect(w.state, 'idle');
    expect(w.scanResults.single.ssid, 'home');
  });

  test('a change landing between the reconnect read and the notify enable '
      'is read back', () async {
    final (ble, c) = await openPicker('{"state":"scanning"}');
    ble
      ..readAfterDrop = _b('{"state":"idle"}')
      ..changeBeforeEnable = _b('{"state":"connecting"}');
    await bounce(ble);
    expect(c.read(wifiNotifierProvider(_devId)).value!.state, 'connecting');
  });

  test('a change landing between the first read and the notify enable is '
      'read back', () async {
    final ble = _LinkScopedBle()..changeBeforeEnable = _b('{"state":"connecting"}');
    await ble.connect(_devId);
    await ble.write(_devId, BleUuids.controlService, BleUuids.wifiState,
        _b('{"state":"scanning"}'));
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    c.listen(wifiNotifierProvider(_devId), (_, _) {});
    await c.read(wifiNotifierProvider(_devId).future);
    await pumpEventQueue();
    expect(c.read(wifiNotifierProvider(_devId)).value!.state, 'connecting');
  });

  test('a re-read that fails to parse leaves the state', () async {
    final (ble, c) = await openPicker('{"state":"scanning"}');
    ble.readAfterDrop = _b('not json');
    await bounce(ble);
    expect(c.read(wifiNotifierProvider(_devId)).value!.state, 'scanning');
  });

  test('a re-read returning after the picker is disposed is dropped',
      () async {
    final (ble, c) = await openPicker('{"state":"scanning"}');
    final gate = Completer<Uint8List>();
    ble.readGate = gate;
    await bounce(ble);
    c.dispose();
    gate.complete(_b('{"state":"idle"}'));
    await pumpEventQueue();
  });

  test('disposing the picker stops the renewal on a later reconnect',
      () async {
    final (ble, c) = await openPicker('{"state":"idle"}');
    c.dispose();
    await pumpEventQueue();
    final before = ble.subscribes;
    await bounce(ble);
    expect(ble.subscribes, before);
    expect(ble.listeners, 0);
  });
}
