import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/utils/string_case.dart';

void main() {
  group('toTitleCase', () {
    test('single lowercase word', () {
      expect('jacko'.toTitleCase(), 'Jacko');
    });

    test('multi word', () {
      expect('living room'.toTitleCase(), 'Living Room');
    });

    test('already capitalized normalizes', () {
      expect('LIVING ROOM'.toTitleCase(), 'Living Room');
    });

    test('empty stays empty', () {
      expect(''.toTitleCase(), '');
    });

    test('blank whitespace preserved', () {
      expect('   '.toTitleCase(), '   ');
    });
  });
}
