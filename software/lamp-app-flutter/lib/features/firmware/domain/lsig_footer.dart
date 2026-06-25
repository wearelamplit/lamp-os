// Dart mirror of the LSIG footer layout from
// software/lamp-os/src/components/firmware/firmware_signature.hpp and
// scripts/sign_firmware.py.
//
// The lamp's signed firmware = firmware.bin || LSIG_FOOTER(96).
// The footer is appended at sign time and stripped + verified at OTA
// receive time. The app verifies the footer LOCALLY before pushing the
// binary to a lamp — if our copy is corrupt we drop it without burning
// the lamp's flash bandwidth.

import 'dart:typed_data';

/// LSIG footer fixed size — used both as a memcpy sentinel and as the
/// "footerLen" field in MSG_FW_OFFER / MSG_FW_DONE.
const int lsigFooterLen = 96;

const int lsigMagicLen     = 4;
const int lsigChannelLen   = 16;
const int lsigSignatureLen = 64;

/// Footer offsets — must mirror sign_firmware.py's struct layout exactly.
/// v0x04: channel widens 8 → 16 (carries `{type}-{channel}`); reserved
/// shrinks 12 → 4; signature offset 32 pinned.
///
///   offset  size  field
///   0       4     magic "LSIG"
///   4       16    channel — zero-padded ASCII, `{lampType}-{channel}`
///   20      4     firmware version — packed (major<<16)|(minor<<8)|patch, LE
///   24      4     signedRegionLen — imageLen - 96, LE
///   28      4     reserved (zeros)
///   32      64    ed25519 signature over SHA256(image[0..signedRegionLen))
const int lsigMagicOffset       = 0;
const int lsigChannelOffset     = 4;
const int lsigVersionOffset     = 20;
const int lsigSignedRegLenOffset = 24;
const int lsigReservedOffset    = 28;
const int lsigSignatureOffset   = 32;

/// Parsed LSIG footer.
class LsigFooter {
  LsigFooter({
    required this.channel,
    required this.version,
    required this.signedRegionLen,
    required this.signature,
  });

  /// Channel string (trailing nulls stripped) — `"stable"`, `"beta"`, etc.
  final String channel;

  /// Packed semver — (major << 16) | (minor << 8) | patch.
  final int version;

  /// Bytes of the signed region (i.e. imageLen - 96). The ed25519
  /// signature verifies against SHA-256 of these bytes.
  final int signedRegionLen;

  /// Raw ed25519 signature (64 bytes).
  final Uint8List signature;

  /// Convenience: major version component.
  int get major => (version >> 16) & 0xFF;
  int get minor => (version >> 8)  & 0xFF;
  int get patch =>  version        & 0xFF;

  /// Convenience: render version as `M.m.p`.
  String get versionString => '$major.$minor.$patch';
}

/// Errors surfaced during footer parsing — the caller (signature
/// verifier) maps them to user-facing OTA error states.
class LsigFooterParseException implements Exception {
  const LsigFooterParseException(this.reason);
  final String reason;
  @override
  String toString() => 'LsigFooterParseException: $reason';
}

/// Parse the last [lsigFooterLen] bytes of a signed firmware image.
/// Throws [LsigFooterParseException] on any structural problem.
LsigFooter parseLsigFooter(Uint8List signedImage) {
  if (signedImage.length < lsigFooterLen) {
    throw LsigFooterParseException(
        'image too small (${signedImage.length} < $lsigFooterLen)');
  }
  final footerStart = signedImage.length - lsigFooterLen;
  final view = ByteData.view(
      signedImage.buffer, signedImage.offsetInBytes + footerStart, lsigFooterLen);

  // Magic check.
  if (view.getUint8(0) != 0x4C ||  // 'L'
      view.getUint8(1) != 0x53 ||  // 'S'
      view.getUint8(2) != 0x49 ||  // 'I'
      view.getUint8(3) != 0x47) {  // 'G'
    throw const LsigFooterParseException('LSIG magic mismatch');
  }

  // Channel — read up to 8 bytes, strip trailing zeros for display.
  final channelBuf = StringBuffer();
  for (var i = 0; i < lsigChannelLen; ++i) {
    final c = view.getUint8(lsigChannelOffset + i);
    if (c == 0) break;
    channelBuf.writeCharCode(c);
  }

  final version = view.getUint32(lsigVersionOffset, Endian.little);
  final signedRegionLen = view.getUint32(lsigSignedRegLenOffset, Endian.little);

  // The signedRegionLen MUST match image - footer or the LSIG was built
  // against a different blob. The receiver would also reject this; we
  // catch it earlier so the user sees "corrupt download" not "lamp
  // refused the offer".
  final expected = signedImage.length - lsigFooterLen;
  if (signedRegionLen != expected) {
    throw LsigFooterParseException(
        'signedRegionLen ($signedRegionLen) != expected ($expected) — '
        'truncated download?');
  }

  final signature = Uint8List.fromList(signedImage.sublist(
      footerStart + lsigSignatureOffset,
      footerStart + lsigSignatureOffset + lsigSignatureLen));

  return LsigFooter(
    channel: channelBuf.toString(),
    version: version,
    signedRegionLen: signedRegionLen,
    signature: signature,
  );
}
