// LSIG footer parser tests. The footer is the canonical byte sequence
// emitted by scripts/sign_firmware.py — these tests build that exact
// sequence and verify our parser pulls the same fields back out.

import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/firmware/domain/lsig_footer.dart';

Uint8List _buildFooter({
  required String channel,
  required int version,
  required int signedRegionLen,
  required Uint8List signature,
}) {
  final out = Uint8List(lsigFooterLen);
  final view = ByteData.view(out.buffer);
  // Magic "LSIG"
  view.setUint8(0, 0x4C);
  view.setUint8(1, 0x53);
  view.setUint8(2, 0x49);
  view.setUint8(3, 0x47);
  // Channel
  final channelBytes = channel.codeUnits;
  for (var i = 0; i < lsigChannelLen; ++i) {
    view.setUint8(lsigChannelOffset + i,
        i < channelBytes.length ? channelBytes[i] : 0);
  }
  // Version + signedRegionLen
  view.setUint32(lsigVersionOffset, version, Endian.little);
  view.setUint32(lsigSignedRegLenOffset, signedRegionLen, Endian.little);
  // Reserved zeros (default).
  // Signature
  for (var i = 0; i < lsigSignatureLen; ++i) {
    view.setUint8(lsigSignatureOffset + i, signature[i]);
  }
  return out;
}

Uint8List _buildSignedImage({
  required int signedRegionLen,
  String channel = 'stable',
  int version = 0x00010205,
  Uint8List? signature,
}) {
  signature ??= Uint8List.fromList(List.generate(64, (i) => i & 0xFF));
  final out = Uint8List(signedRegionLen + lsigFooterLen);
  // Fill the signed region with a recognizable pattern.
  for (var i = 0; i < signedRegionLen; ++i) {
    out[i] = i & 0xFF;
  }
  final footer = _buildFooter(
    channel: channel,
    version: version,
    signedRegionLen: signedRegionLen,
    signature: signature,
  );
  for (var i = 0; i < footer.length; ++i) {
    out[signedRegionLen + i] = footer[i];
  }
  return out;
}

void main() {
  group('parseLsigFooter', () {
    test('reads channel, version, signed region length, signature', () {
      final image = _buildSignedImage(signedRegionLen: 1024);
      final footer = parseLsigFooter(image);
      expect(footer.channel, equals('stable'));
      expect(footer.version, equals(0x00010205));
      expect(footer.major, equals(1));
      expect(footer.minor, equals(2));
      expect(footer.patch, equals(5));
      expect(footer.versionString, equals('1.2.5'));
      expect(footer.signedRegionLen, equals(1024));
      expect(footer.signature.length, equals(64));
    });

    test('strips trailing zeros from the channel string', () {
      final image = _buildSignedImage(signedRegionLen: 64, channel: 'beta');
      final footer = parseLsigFooter(image);
      expect(footer.channel, equals('beta'));
      expect(footer.channel.length, equals(4));
    });

    test('rejects an image too small to hold a footer', () {
      expect(
        () => parseLsigFooter(Uint8List(lsigFooterLen - 1)),
        throwsA(isA<LsigFooterParseException>()),
      );
    });

    test('rejects bad LSIG magic', () {
      final image = _buildSignedImage(signedRegionLen: 64);
      image[64] = 0xFF; // first byte of footer
      expect(
        () => parseLsigFooter(image),
        throwsA(isA<LsigFooterParseException>()),
      );
    });

    test('rejects signedRegionLen mismatch (truncated download)', () {
      final image = _buildSignedImage(signedRegionLen: 1024);
      // Forge: claim 9999 bytes signed when image only has 1024.
      final view = ByteData.view(
          image.buffer, image.offsetInBytes + 1024 + lsigSignedRegLenOffset, 4);
      view.setUint32(0, 9999, Endian.little);
      expect(
        () => parseLsigFooter(image),
        throwsA(isA<LsigFooterParseException>()),
      );
    });
  });
}
