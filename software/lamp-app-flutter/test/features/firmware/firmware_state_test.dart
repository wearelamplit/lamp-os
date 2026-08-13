import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/firmware/domain/firmware_state.dart';
import 'package:lamp_app/features/firmware/domain/lsig_footer.dart';

void main() {
  final footer = LsigFooter(
    version: 0x010203,
    channel: 'standard-beta',
    signedRegionLen: 12345,
    signature: Uint8List(0),
  );

  group('isBusy', () {
    test('idle is neither pushing nor busy', () {
      expect(const FirmwareIdle().isPushing, false);
      expect(const FirmwareIdle().isBusy, false);
    });

    test('verifying is busy but not pushing', () {
      expect(const FirmwareVerifying().isPushing, false);
      expect(const FirmwareVerifying().isBusy, true);
    });

    test('streaming is both', () {
      final s = FirmwareStreaming(footer: footer, chunksSent: 1, totalChunks: 4);
      expect(s.isPushing, true);
      expect(s.isBusy, true);
    });

    test('succeeded is neither', () {
      expect(FirmwareSucceeded(footer: footer).isPushing, false);
      expect(FirmwareSucceeded(footer: footer).isBusy, false);
    });
  });
}
