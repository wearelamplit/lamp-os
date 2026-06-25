import 'dart:convert';
import 'dart:typed_data';

import 'package:cryptography/cryptography.dart';
import 'package:flutter/foundation.dart';

/// Wire format for sensitive BLE writes when the new firmware is in use:
///
/// ```
///   plaintext (legacy / webapp):  [0x01][ utf-8 json ... ]
///   ciphertext (new app):         [0x02][ 12B nonce ][ 16B tag ][ ciphertext ]
/// ```
///
/// Both sides derive the AES-256 key via HKDF-SHA256:
///   - salt = first 16 bytes of the characteristic UUID, byte-reversed
///   - info = "lamp-v1" + 0x00 + charShortName
///   - IKM  = UTF-8 bytes of the lamp's controlPassword
///
/// See `software/lamp-os/src/components/network/crypto.{hpp,cpp}` for the
/// firmware counterpart.
///
/// SECURITY (audit sec-H3, deferred): two lamps with the same
/// controlPassword derive the IDENTICAL 256-bit AES key for each
/// characteristic because the HKDF info doesn't include a per-device
/// identifier. Captured ciphertext from lamp A's CHAR_X can be
/// replayed against lamp B's CHAR_X iff both share the password.
/// Fix would mix the lamp's WiFi STA MAC into the info, bump prefix
/// to "lamp-v2" — but the lamp firmware must mirror the change AND
/// the app needs the lamp's STA MAC at connect time (Android can
/// derive from BLE MAC, iOS cannot — would need a new BLE
/// characteristic exposing it). Tracked in
/// docs/accepted-security-threats.md.
/// Threat is bounded by operator deliberately reusing passwords.
class LampCrypto {
  /// SECURITY (accepted threats T1+T2): the plaintext byte signals to
  /// the lamp firmware "this payload is unauthenticated/unencrypted, but
  /// accept it because the lamp is factory-fresh (empty password) or
  /// the operation is one of the wisp-relay ops where the wisp can't
  /// decrypt anyway." Plaintext writes leak any embedded secrets — the
  /// Wi-Fi PSK in `setWifi` and the new admin credential in first-
  /// claim are the two cases where this matters. Fleet-wide mesh
  /// authentication would close both, but is deliberately rejected.
  /// See docs/accepted-security-threats.md.
  static const int magicPlaintext = 0x01;
  static const int magicCiphertext = 0x02;
  static const int nonceLen = 12;
  static const int tagLen = 16;

  static final _aes = AesGcm.with256bits();
  static final _hkdf = Hkdf(hmac: Hmac.sha256(), outputLength: 32);

  /// HKDF-SHA256 with the wire-spec salt/info construction. Returns a
  /// 32-byte [SecretKey] suitable for AES-256-GCM.
  static Future<SecretKey> deriveKey({
    required String password,
    required Uint8List saltUuid16,
    required String charShortName,
  }) async {
    // info = b"lamp-v1" + 0x00 + charShortName (NUL is a separator byte, not
    // a C-string terminator — firmware builds it explicitly the same way).
    final info = <int>[
      ...utf8.encode('lamp-v1'),
      0,
      ...utf8.encode(charShortName),
    ];
    return _hkdf.deriveKey(
      secretKey: SecretKey(utf8.encode(password)),
      // package:cryptography names the HKDF salt slot `nonce` — semantically
      // it IS the salt parameter (RFC 5869 §2.2).
      nonce: saltUuid16,
      info: info,
    );
  }

  /// Encrypt a JSON op for a sensitive write. Returns the framed wire
  /// payload: `[0x02 | nonce(12) | tag(16) | ciphertext]`. Caller is
  /// expected to write the result to the characteristic verbatim.
  static Future<Uint8List> encryptOp({
    required Map<String, dynamic> op,
    required String password,
    required Uint8List saltUuid16,
    required String charShortName,
  }) async {
    if (password.isEmpty) {
      throw ArgumentError.value(
        password, 'password',
        'encryptOp requires a non-empty password; use wrapPlaintext() '
        'for factory-default lamps',
      );
    }
    final key = await deriveKey(
        password: password,
        saltUuid16: saltUuid16,
        charShortName: charShortName);
    final plaintext = utf8.encode(jsonEncode(op));
    final box = await _aes.encrypt(plaintext, secretKey: key);
    final out = BytesBuilder()
      ..addByte(magicCiphertext)
      ..add(box.nonce)
      ..add(box.mac.bytes)
      ..add(box.cipherText);
    return out.toBytes();
  }

  /// Wrap a JSON op in the plaintext-prefix format used when no
  /// controlPassword is known (factory-default / pre-adoption flow).
  /// Returns `[0x01 | utf-8 json]`.
  static Uint8List wrapPlaintext(Map<String, dynamic> op) {
    final json = utf8.encode(jsonEncode(op));
    return Uint8List.fromList([magicPlaintext, ...json]);
  }

  /// Test-only inverse of [encryptOp] — mirrors the firmware's
  /// `lamp::crypto::decryptOp` so round-trip tests don't need a device.
  ///
  /// Returns the decrypted plaintext bytes (caller decodes JSON), or
  /// throws on any failure path (wrong password, tampered tag, malformed
  /// payload, etc.).
  @visibleForTesting
  static Future<Uint8List> decryptOpForTesting(
    Uint8List wire, {
    required String password,
    required Uint8List saltUuid16,
    required String charShortName,
  }) async {
    if (wire.length < 1 + nonceLen + tagLen) {
      throw const FormatException('payload too short');
    }
    if (wire[0] != magicCiphertext) {
      throw const FormatException('wrong magic byte');
    }
    if (password.isEmpty) {
      throw const FormatException('empty password');
    }
    final nonce = wire.sublist(1, 1 + nonceLen);
    final tag = wire.sublist(1 + nonceLen, 1 + nonceLen + tagLen);
    final ct = wire.sublist(1 + nonceLen + tagLen);
    final key = await deriveKey(
        password: password,
        saltUuid16: saltUuid16,
        charShortName: charShortName);
    final box = SecretBox(ct, nonce: nonce, mac: Mac(tag));
    final plain = await _aes.decrypt(box, secretKey: key);
    return Uint8List.fromList(plain);
  }
}

/// Convert a UUID string like `5f64f4d1-d6d9-4a44-9b3f-3a8d6f7e6b40` into
/// 16 bytes, then reverse them. Matches the firmware's `uuidSaltLE(...)`.
Uint8List uuidSaltLE16(String uuid) {
  final hex = uuid.replaceAll('-', '');
  assert(hex.length == 32, 'expected 32-char hex UUID, got: $uuid');
  final fwd = Uint8List(16);
  for (var i = 0; i < 16; i++) {
    fwd[i] = int.parse(hex.substring(i * 2, i * 2 + 2), radix: 16);
  }
  // Reverse to LE.
  final out = Uint8List(16);
  for (var i = 0; i < 16; i++) {
    out[i] = fwd[15 - i];
  }
  return out;
}
