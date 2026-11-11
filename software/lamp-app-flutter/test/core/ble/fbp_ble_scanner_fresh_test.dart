import 'dart:async';

import 'package:flutter_blue_plus/flutter_blue_plus.dart' as fbp;
import 'package:flutter_blue_plus_platform_interface/flutter_blue_plus_platform_interface.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_scanner.dart';

/// A platform whose scan responses the test pushes by hand, so the real fbp
/// scan pipeline (its accumulated `scanResults` list) sits between the radio
/// and [FbpBleScanner].
final class _Radio extends FlutterBluePlusPlatform {
  final _responses = StreamController<BmScanResponse>.broadcast();

  @override
  Stream<BmScanResponse> get onScanResponse => _responses.stream;
  @override
  Future<bool> startScan(BmScanSettings request) async => true;
  @override
  Future<bool> stopScan(BmStopScanRequest request) async => true;

  Future<void> hear(String id) async {
    // Distinct fbp timestamps between sightings of one device.
    await Future<void>.delayed(const Duration(milliseconds: 2));
    _responses.add(
      BmScanResponse(
        advertisements: [
          BmScanAdvertisement(
            remoteId: DeviceIdentifier(id),
            platformName: null,
            advName: 'lamp $id',
            connectable: true,
            txPowerLevel: null,
            appearance: null,
            manufacturerData: const {
              0xA455: [1, 2, 3, 4, 5, 6, 0x06],
            },
            serviceData: const {},
            serviceUuids: const [],
            rssi: -60,
          ),
        ],
        success: true,
        errorCode: 0,
        errorString: '',
      ),
    );
    await Future<void>.delayed(Duration.zero);
  }
}

void main() {
  late _Radio radio;
  setUp(() {
    radio = _Radio();
    FlutterBluePlusPlatform.instance = radio;
  });

  test(
    'a lamp heard once is forwarded once while other lamps keep advertising',
    () async {
      final s = FbpBleScanner();
      final heard = <String>[];
      final sub = s.results().listen((ad) => heard.add(ad.id));
      await s.start();
      await radio.hear('A');
      for (var i = 0; i < 3; i++) {
        await radio.hear('B');
      }
      await radio.hear('A');
      expect(
        heard.where((id) => id == 'A').length,
        2,
        reason: 'A is forwarded for each of its own advertisements only',
      );
      expect(heard.where((id) => id == 'B').length, 3);
      await s.stop();
      await sub.cancel();
    },
  );

  test('a new scanner does not forward the previous scan\'s lamps', () async {
    final first = FbpBleScanner();
    await first.start();
    await radio.hear('C');
    await first.stop();
    expect(fbp.FlutterBluePlus.lastScanResults, isNotEmpty);

    final second = FbpBleScanner();
    final heard = <String>[];
    final sub = second.results().listen((ad) => heard.add(ad.id));
    await second.start();
    await radio.hear('D');
    expect(heard, ['D'], reason: 'C was not heard during this scan');
    await second.stop();
    await sub.cancel();
  });

  test(
    'a restarted scanner does not forward its previous scan\'s lamps',
    () async {
      final s = FbpBleScanner();
      final heard = <String>[];
      final sub = s.results().listen((ad) => heard.add(ad.id));
      await s.start();
      await radio.hear('E');
      await s.stop();
      await s.start();
      await radio.hear('F');
      expect(heard, [
        'E',
        'F',
      ], reason: 'E is not heard again after the restart');
      await s.stop();
      await sub.cancel();
    },
  );
}
