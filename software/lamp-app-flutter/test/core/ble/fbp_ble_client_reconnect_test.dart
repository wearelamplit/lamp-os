import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_blue_plus_platform_interface/flutter_blue_plus_platform_interface.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_timing.dart';
import 'package:lamp_app/core/ble/fbp_ble_client.dart';
import 'package:lamp_app/core/ble/uuids.dart';

const _id = 'AA:BB:CC:DD:EE:01';

/// A lamp with one characteristic that answers connect, discovery and reads,
/// and can drop the link on its own.
final class _FakeLamp extends FlutterBluePlusPlatform {
  final _conn = StreamController<BmConnectionStateResponse>.broadcast();
  final _services = StreamController<BmDiscoverServicesResult>.broadcast();
  final _values = StreamController<BmCharacteristicData>.broadcast();
  int discoveries = 0;

  @override
  Stream<BmConnectionStateResponse> get onConnectionStateChanged =>
      _conn.stream;
  @override
  Stream<BmDiscoverServicesResult> get onDiscoveredServices =>
      _services.stream;
  @override
  Stream<BmCharacteristicData> get onCharacteristicReceived => _values.stream;

  void _state(BmConnectionStateEnum s) => _conn.add(BmConnectionStateResponse(
        remoteId: const DeviceIdentifier(_id),
        connectionState: s,
        disconnectReasonCode: null,
        disconnectReasonString: null,
      ));

  void drop() => _state(BmConnectionStateEnum.disconnected);

  @override
  Future<bool> connect(BmConnectRequest request) {
    scheduleMicrotask(() => _state(BmConnectionStateEnum.connected));
    return Future.value(true);
  }

  @override
  Future<bool> discoverServices(BmDiscoverServicesRequest request) {
    discoveries++;
    final service = Guid(BleUuids.controlService);
    scheduleMicrotask(() => _services.add(BmDiscoverServicesResult(
          remoteId: const DeviceIdentifier(_id),
          services: [
            BmBluetoothService(
              remoteId: const DeviceIdentifier(_id),
              primaryServiceUuid: null,
              serviceUuid: service,
              characteristics: [
                BmBluetoothCharacteristic(
                  remoteId: const DeviceIdentifier(_id),
                  primaryServiceUuid: null,
                  serviceUuid: service,
                  characteristicUuid: Guid(BleUuids.wispStatus),
                  instanceId: 0,
                  descriptors: const [],
                  properties: BmCharacteristicProperties(
                    broadcast: false,
                    read: true,
                    writeWithoutResponse: false,
                    write: false,
                    notify: true,
                    indicate: false,
                    authenticatedSignedWrites: false,
                    extendedProperties: false,
                    notifyEncryptionRequired: false,
                    indicateEncryptionRequired: false,
                  ),
                ),
              ],
            ),
          ],
          success: true,
          errorCode: 0,
          errorString: '',
        )));
    return Future.value(true);
  }

  @override
  Future<bool> readCharacteristic(BmReadCharacteristicRequest request) {
    scheduleMicrotask(() => _values.add(BmCharacteristicData(
          remoteId: request.remoteId,
          primaryServiceUuid: request.primaryServiceUuid,
          serviceUuid: request.serviceUuid,
          characteristicUuid: request.characteristicUuid,
          instanceId: request.instanceId,
          value: const [7],
          success: true,
          errorCode: 0,
          errorString: '',
        )));
    return Future.value(true);
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  // A notifier that renews its notify subscription on the connected edge
  // resolves the characteristic before connect() has returned.
  test('a link that drops on its own is rediscovered at the next connected '
      'edge', () async {
    final lamp = _FakeLamp();
    FlutterBluePlusPlatform.instance = lamp;
    final discovers = <BleTimingEvent>[];
    final savedSink = bleTimings.sink;
    bleTimings.sink = (e) {
      if (e.phase == BleTimingPhase.discover) discovers.add(e);
    };
    addTearDown(() {
      bleTimings
        ..sink = savedSink
        ..linkDown(_id);
    });
    final client = FbpBleClient();
    await client.connect(_id);
    await client.read(_id, BleUuids.controlService, BleUuids.wispStatus);
    expect(lamp.discoveries, 1);

    lamp.drop();
    await pumpEventQueue();
    Future<Uint8List>? edgeRead;
    final sub = client.watchConnected(_id).listen((up) {
      if (up) {
        edgeRead ??=
            client.read(_id, BleUuids.controlService, BleUuids.wispStatus);
      }
    });
    addTearDown(sub.cancel);
    await client.connect(_id);
    expect(edgeRead, isNotNull);
    expect(await edgeRead, [7]);

    expect(lamp.discoveries, 2,
        reason: 'the old link\'s services do not exist on the new one');
    expect(discovers.length, 2,
        reason: 'the rediscovery after a drop is timed as a cold discover');
  });

  test('a read on the connected edge and one after connect share one '
      'discovery', () async {
    final lamp = _FakeLamp();
    FlutterBluePlusPlatform.instance = lamp;
    final discovers = <BleTimingEvent>[];
    final savedSink = bleTimings.sink;
    bleTimings.sink = (e) {
      if (e.phase == BleTimingPhase.discover) discovers.add(e);
    };
    addTearDown(() {
      bleTimings
        ..sink = savedSink
        ..linkDown(_id);
    });
    final client = FbpBleClient();
    Future<Uint8List>? edgeRead;
    final sub = client.watchConnected(_id).listen((up) {
      if (up) {
        edgeRead ??=
            client.read(_id, BleUuids.controlService, BleUuids.wispStatus);
      }
    });
    addTearDown(sub.cancel);

    await client.connect(_id);
    expect(edgeRead, isNotNull, reason: 'the edge read began inside connect');
    expect(
      await client.read(_id, BleUuids.controlService, BleUuids.wispStatus),
      [7],
    );
    expect(await edgeRead, [7]);
    expect(lamp.discoveries, 1);
    expect(discovers, hasLength(1));
  });
}
