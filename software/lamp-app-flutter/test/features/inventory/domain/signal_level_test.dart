import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/inventory/domain/signal_level.dart';

void main() {
  group('signalLevelFor', () {
    test('null RSSI is unknown, not full bars', () {
      expect(signalLevelFor(null), SignalLevel.unknown);
    });

    test('buckets by dBm threshold', () {
      expect(signalLevelFor(-40), SignalLevel.strong);
      expect(signalLevelFor(-60), SignalLevel.strong);
      expect(signalLevelFor(-61), SignalLevel.medium);
      expect(signalLevelFor(-75), SignalLevel.medium);
      expect(signalLevelFor(-76), SignalLevel.weak);
      expect(signalLevelFor(-110), SignalLevel.weak);
    });
  });

  group('signalDotBrightnessFor', () {
    test('strong is full bright, weaker dims, unknown is medium', () {
      expect(signalDotBrightnessFor(-40), 1.0);
      expect(signalDotBrightnessFor(-70), 0.65);
      expect(signalDotBrightnessFor(-90), 0.4);
      expect(signalDotBrightnessFor(null), 0.65);
    });
  });
}
