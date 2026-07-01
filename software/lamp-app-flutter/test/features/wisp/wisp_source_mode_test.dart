import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';

void main() {
  group('parseWispSourceMode', () {
    test('"off" → off', () {
      expect(parseWispSourceMode('off'), WispSourceMode.off);
    });

    test('"manual" → manual', () {
      expect(parseWispSourceMode('manual'), WispSourceMode.manual);
    });

    test('"aurora" → aurora', () {
      expect(parseWispSourceMode('aurora'), WispSourceMode.aurora);
    });

    test('null defaults to off (legacy / missing key)', () {
      // Off is safe: the wisp only emits paint frames in Manual or Aurora
      // mode, so Off can never accidentally override the lamps.
      expect(parseWispSourceMode(null), WispSourceMode.off);
    });

    test('unknown string defaults to off', () {
      expect(parseWispSourceMode('rainbow'), WispSourceMode.off);
      expect(parseWispSourceMode(''), WispSourceMode.off);
    });
  });

  group('wispSourceModeWire', () {
    test('round-trips each mode through the wire encoding', () {
      for (final m in WispSourceMode.values) {
        expect(parseWispSourceMode(wispSourceModeWire(m)), m,
            reason: 'mode $m should round-trip');
      }
    });
  });
}
