import 'package:flutter_test/flutter_test.dart';

import 'package:lamp_app/core/widgets/status_dot.dart';
import 'package:lamp_app/features/lamp_shell/application/lamp_status.dart';
import 'package:lamp_app/features/nearby/domain/nearby_lamp.dart';

NearbyLamp _seenLamp(String id) => NearbyLamp(
      id: id,
      name: 'x',
      rssi: -50,
      serviceUuids: const [],
      baseRgb: 0,
      shadeRgb: 0,
      lastSeenEpochMs: 1,
    );

void main() {
  group('statusFor', () {
    test('connected → mesh, even if not in nearby list', () {
      expect(
        statusFor(lampId: 'a', nearby: const [], connected: true),
        StatusKind.mesh,
      );
    });

    test('in nearby + not connected → bluetooth', () {
      expect(
        statusFor(
          lampId: 'a',
          nearby: [_seenLamp('a')],
          connected: false,
        ),
        StatusKind.bluetooth,
      );
    });

    test('absent from nearby + not connected → offline', () {
      expect(
        statusFor(
          lampId: 'a',
          nearby: [_seenLamp('b')],
          connected: false,
        ),
        StatusKind.offline,
      );
    });
  });
}
