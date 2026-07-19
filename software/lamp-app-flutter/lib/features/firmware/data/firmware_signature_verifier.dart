// Local Ed25519 verification of signed firmware images.
//
// The lamp's verify path streams SHA-256 over the signed region in
// 4 KB blocks (firmware_signature.hpp:verifySignedFirmware), then
// ed25519-verifies the 32-byte digest. Same scheme here: canonical,
// auditable against the C++ side byte-for-byte, and it rejects a
// corrupt download before burning the lamp's BLE OTA bandwidth on an
// image that would just fail at the end.

import 'dart:typed_data';

import 'package:cryptography/cryptography.dart';

import '../domain/lsig_footer.dart';
import 'firmware_pubkey.dart';

class FirmwareVerifyException implements Exception {
  const FirmwareVerifyException(this.reason);
  final String reason;
  @override
  String toString() => 'FirmwareVerifyException: $reason';
}

/// Parsed footer + the SHA-256 digest of the signed region. The OTA
/// pusher derives the OFFER/DONE prefix from `fullSha256` itself.
class VerifiedFirmware {
  VerifiedFirmware({
    required this.footer,
    required this.fullSha256,
  });

  final LsigFooter footer;

  /// The 32-byte SHA-256 digest of the signed region.
  final Uint8List fullSha256;
}

/// Verify a signed firmware image. Returns the parsed footer + computed
/// digest on success; throws [FirmwareVerifyException] with a
/// user-actionable reason on any failure.
///
/// The verify is offline: no network, no lamp involvement. Computes
/// SHA-256 over `signedImage[0..signedRegionLen)` (i.e. everything
/// except the 96-byte LSIG footer) and ed25519-verifies the digest
/// against the footer's signature with the baked-in [firmwarePublicKey].
Future<VerifiedFirmware> verifyFirmwareImage(Uint8List signedImage) async {
  // 1. Parse footer (also validates length + magic + signedRegionLen).
  final footer = parseLsigFooter(signedImage);

  // 2. SHA-256 over the signed region. Cryptography's Sha256 accepts a
  //    one-shot bytes input; for a ~1.5 MB image that's fine in memory.
  //    (The lamp side streams to keep its 4 KB scratch buffer; the app
  //    is on a phone with gigabytes of RAM, so one-shot is cleaner.)
  final signedRegion = Uint8List.sublistView(signedImage, 0, footer.signedRegionLen);
  final digest = await Sha256().hash(signedRegion);
  final fullSha256 = Uint8List.fromList(digest.bytes);

  // 3. Ed25519 verify the digest against the footer's signature.
  final algorithm = Ed25519();
  final publicKey = SimplePublicKey(
    firmwarePublicKey,
    type: KeyPairType.ed25519,
  );
  final signature = Signature(footer.signature, publicKey: publicKey);
  final ok = await algorithm.verify(fullSha256, signature: signature);
  if (!ok) {
    throw const FirmwareVerifyException(
        'signature verify failed — image not signed by trusted key');
  }

  return VerifiedFirmware(
    footer: footer,
    fullSha256: fullSha256,
  );
}
