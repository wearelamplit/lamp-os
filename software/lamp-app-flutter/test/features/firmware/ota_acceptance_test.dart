import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/firmware/domain/ota_acceptance.dart';

// Mirrors software/lamp-os/test/test_ota_channel/ota_channel.cpp: the app must
// only offer what the lamp firmware would accept.
void main() {
  group('otaAcceptable', () {
    test('intra-channel beta upgrade accepted', () {
      expect(otaAcceptable('standard-beta', 100, 'standard-beta', 101), isTrue);
    });

    test('intra-channel stable upgrade accepted', () {
      expect(
          otaAcceptable('standard-stable', 100, 'standard-stable', 101), isTrue);
    });

    test('intra-channel beta equal version rejected', () {
      expect(
          otaAcceptable('standard-beta', 100, 'standard-beta', 100), isFalse);
    });

    test('intra-channel stable equal version rejected', () {
      expect(otaAcceptable('standard-stable', 100, 'standard-stable', 100),
          isFalse);
    });

    test('intra-channel beta downgrade rejected', () {
      expect(
          otaAcceptable('standard-beta', 101, 'standard-beta', 100), isFalse);
    });

    test('promotion at equal version accepted', () {
      expect(
          otaAcceptable('standard-beta', 100, 'standard-stable', 100), isTrue);
    });

    test('promotion to newer stable accepted', () {
      expect(otaAcceptable('standard-beta', 99, 'standard-stable', 100), isTrue);
    });

    test('snafu intra-channel beta upgrade accepted', () {
      expect(otaAcceptable('snafu-beta', 100, 'snafu-beta', 101), isTrue);
    });

    test('snafu promotion at equal version accepted', () {
      expect(otaAcceptable('snafu-beta', 100, 'snafu-stable', 100), isTrue);
    });

    test('promotion to older stable rejected', () {
      expect(
          otaAcceptable('standard-beta', 101, 'standard-stable', 100), isFalse);
    });

    test('stable receiving beta rejected', () {
      expect(
          otaAcceptable('standard-stable', 100, 'standard-beta', 101), isFalse);
    });

    test('cross-variant same channel rejected', () {
      expect(otaAcceptable('standard-beta', 100, 'snafu-beta', 101), isFalse);
    });

    test('cross-variant promotion rejected', () {
      expect(otaAcceptable('standard-beta', 100, 'snafu-stable', 100), isFalse);
    });

    test('dev rejects beta offer', () {
      expect(
          otaAcceptable('standard-dev', 100, 'standard-beta', 101), isFalse);
    });

    test('dev rejects stable offer', () {
      expect(otaAcceptable('standard-dev', 100, 'standard-stable', 101),
          isFalse);
    });

    test('beta rejects dev offer', () {
      expect(
          otaAcceptable('standard-beta', 100, 'standard-dev', 101), isFalse);
    });

    test('stable rejects dev offer', () {
      expect(otaAcceptable('standard-stable', 100, 'standard-dev', 101),
          isFalse);
    });

    test('empty offer channel rejected', () {
      expect(otaAcceptable('standard-beta', 100, '', 101), isFalse);
    });

    test('empty our channel rejected', () {
      expect(otaAcceptable('', 100, 'standard-stable', 100), isFalse);
    });

    test('null channels rejected', () {
      expect(otaAcceptable(null, 100, 'standard-stable', 100), isFalse);
      expect(otaAcceptable('standard-beta', 100, null, 100), isFalse);
    });

    test('channel with no dash rejected', () {
      expect(otaAcceptable('standardbeta', 100, 'standard-beta', 101), isFalse);
      expect(otaAcceptable('standard-beta', 100, 'standardbeta', 101), isFalse);
    });
  });
}
