import 'dart:async';

import 'package:fake_async/fake_async.dart';
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
  int failNextStarts = 0;

  @override
  Future<bool> startScan(BmScanSettings request) async {
    if (failNextStarts > 0) {
      failNextStarts--;
      throw StateError('radio refused the scan');
    }
    return true;
  }

  @override
  Future<bool> stopScan(BmStopScanRequest request) async => true;

  Future<void> hear(String id) async {
    send(id);
    await Future<void>.delayed(Duration.zero);
  }

  void send(String id) {
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
      expect(heard, ['E'], reason: 'the replayed E is not a new sighting');
      await radio.hear('F');
      await radio.hear('E');
      expect(heard, [
        'E',
        'F',
        'E',
      ], reason: 'every advertisement heard after the restart is forwarded');
      await s.stop();
      await sub.cancel();
    },
  );

  test('a lamp heard again after the scan restarts itself is forwarded', () {
    fakeAsync((async) {
      final s = FbpBleScanner();
      final heard = <String>[];
      s.results().listen((ad) => heard.add(ad.id));
      s.start();
      async.flushMicrotasks();
      radio.send('G');
      async.flushMicrotasks();
      // The radio stopping on its own, as fbp's scan timeout does.
      fbp.FlutterBluePlus.stopScan();
      async.elapse(const Duration(seconds: 4));
      expect(fbp.FlutterBluePlus.isScanningNow, isTrue);
      radio.send('G');
      async.flushMicrotasks();
      expect(heard, ['G', 'G']);
      s.stop();
      async.flushMicrotasks();
    });
  });

  test('a lamp heard after a failed start is retried and forwarded', () {
    fakeAsync((async) {
      final first = FbpBleScanner();
      first.start();
      async.flushMicrotasks();
      radio.send('H');
      async.flushMicrotasks();
      first.stop();
      async.flushMicrotasks();

      radio.failNextStarts = 1;
      final s = FbpBleScanner();
      final heard = <String>[];
      s.results().listen((ad) => heard.add(ad.id));
      s.start();
      async.flushMicrotasks();
      expect(fbp.FlutterBluePlus.isScanningNow, isFalse);
      async.elapse(const Duration(seconds: 4));
      expect(fbp.FlutterBluePlus.isScanningNow, isTrue);
      radio.send('H');
      async.flushMicrotasks();
      expect(heard, ['H'], reason: 'only the H heard after the retry counts');
      s.stop();
      async.flushMicrotasks();
    });
  });
}
