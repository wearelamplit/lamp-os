import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/utils/duration_format.dart';

void main() {
  group('formatDuration', () {
    test('zero', () {
      expect(formatDuration(Duration.zero), '0ms');
    });

    test('sub-second stays in ms', () {
      expect(formatDuration(const Duration(milliseconds: 999)), '999ms');
    });

    test('exact second drops trailing zero ms', () {
      expect(formatDuration(const Duration(seconds: 1)), '1s');
    });

    test('seconds below the minute rollover', () {
      expect(formatDuration(const Duration(seconds: 59)), '59s');
    });

    test('exact minute drops trailing zero s', () {
      expect(formatDuration(const Duration(seconds: 60)), '1m');
    });

    test('minute plus seconds', () {
      expect(formatDuration(const Duration(seconds: 61)), '1m 1s');
    });

    test('exact hour drops trailing zero m', () {
      expect(formatDuration(const Duration(minutes: 60)), '1h');
    });

    test('hour plus minutes', () {
      expect(formatDuration(const Duration(minutes: 61)), '1h 1m');
    });

    test('caps at two units, dropping the third', () {
      expect(formatDuration(const Duration(seconds: 3661)), '1h 1m');
    });

    test('rolls minutes into hours mid-range', () {
      expect(formatDuration(const Duration(minutes: 124)), '2h 4m');
    });
  });
}
