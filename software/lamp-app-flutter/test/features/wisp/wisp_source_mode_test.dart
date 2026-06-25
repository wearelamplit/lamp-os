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
      // The pre-Phase-E wispStatus payload didn't carry a `source` field.
      // Defaulting to off is the safe choice: the wisp only emits paint
      // frames when source is Manual or Aurora, so falling back to Off
      // can never accidentally override the lamps. The operator has to
      // explicitly opt in.
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
