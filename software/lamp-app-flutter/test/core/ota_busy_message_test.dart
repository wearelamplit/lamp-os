import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ota_busy_message.dart';

void main() {
  group('otaBusyMessage', () {
    test('names the pupil when the target resolves', () {
      expect(
        otaBusyMessage(lampName: 'Jacko', targetName: 'lily'),
        'Jacko is busy teaching lily new tricks 🎓',
      );
    });

    test('falls back to a generic pupil when the target is unknown', () {
      expect(
        otaBusyMessage(lampName: 'Jacko'),
        'Jacko is busy teaching a lamp new tricks 🎓',
      );
    });

    test('treats an empty target as unknown', () {
      expect(
        otaBusyMessage(lampName: 'Jacko', targetName: ''),
        'Jacko is busy teaching a lamp new tricks 🎓',
      );
    });
  });
}
