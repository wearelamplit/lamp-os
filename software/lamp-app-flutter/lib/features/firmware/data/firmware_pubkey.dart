// Embedded Ed25519 public key for verifying signed firmware images.
//
// MUST be kept in lockstep with
// software/lamp-os/scripts/keys/firmware_pubkey.h.
//
// If you regenerate the firmware signing key with
// `python3 scripts/gen_firmware_keys.py --force`, you must:
//   1. USB re-flash every lamp with the new firmware_pubkey.h baked in
//   2. Update the bytes below to match
//   3. Rebuild + redeploy the app
//
// A mismatch here vs the lamp side renders OTA permanently broken for
// the fleet. The app will pass the verify here (signature was made
// against this app's key) but the lamp will reject it (signature
// doesn't match the lamp's key). The 32-byte payload below is a public
// key only; loss does not compromise the fleet.

import 'dart:typed_data';

/// 32-byte Ed25519 public key, raw form (matches libsodium
/// `crypto_sign_ed25519_verify_detached`'s `pk` argument AND the
/// cryptography package's `SimplePublicKey` raw-bytes form).
final Uint8List firmwarePublicKey = Uint8List.fromList(const [
  0x0b, 0x58, 0x84, 0x6d, 0x91, 0xf9, 0x44, 0x7c,
  0x90, 0xcd, 0xa2, 0x26, 0xa4, 0x91, 0xff, 0x33,
  0xd5, 0x5c, 0x0d, 0x14, 0xdb, 0xa9, 0xfe, 0xa4,
  0x89, 0xb0, 0x6e, 0x6c, 0x0d, 0xa6, 0x48, 0xec,
]);
