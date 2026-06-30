import 'dart:convert';
import 'dart:typed_data';

import 'package:cryptography/cryptography.dart' show SecretBoxAuthenticationError;
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/lamp_crypto.dart';
import 'package:lamp_app/core/ble/uuids.dart';

void main() {
  group('uuidSaltLE16', () {
    test('reverses a known UUID hex string', () {
      // CHAR_AUTH = 5f64f4d1-d6d9-4a44-9b3f-3a8d6f7e6b40
      // Forward bytes: 5f 64 f4 d1 d6 d9 4a 44 9b 3f 3a 8d 6f 7e 6b 40
      // Reversed:      40 6b 7e 6f 8d 3a 3f 9b 44 4a d9 d6 d1 f4 64 5f
      final salt = uuidSaltLE16(BleUuids.auth);
      expect(salt.length, 16);
      expect(salt[0], 0x40);
      expect(salt[1], 0x6b);
      expect(salt[15], 0x5f);
    });

    test('produces different salts for different UUIDs', () {
      final a = uuidSaltLE16(BleUuids.wifiOp);
      final b = uuidSaltLE16(BleUuids.remoteOp);
      expect(a, isNot(equals(b)));
    });
  });

  group('LampCrypto.wrapPlaintext', () {
    test('prefixes with 0x01 and contains the JSON body', () {
      final p = LampCrypto.wrapPlaintext({'op': 'forget'});
      expect(p[0], LampCrypto.magicPlaintext);
      expect(utf8.decode(p.sublist(1)), '{"op":"forget"}');
    });
  });

  group('LampCrypto.encryptOp', () {
    test('produces a 0x02-prefixed buffer of expected length', () async {
      final ct = await LampCrypto.encryptOp(
        op: {'op': 'scan'},
        password: 'sekret',
        saltUuid16: uuidSaltLE16(BleUuids.wifiOp),
        charShortName: 'wifiOp',
      );
      expect(ct[0], LampCrypto.magicCiphertext);
      expect(ct.length, 1 + 12 + 16 + utf8.encode('{"op":"scan"}').length);
    });

    test('same op + same password produces different ciphertexts (random nonce)', () async {
      final salt = uuidSaltLE16(BleUuids.wifiOp);
      final a = await LampCrypto.encryptOp(
          op: {'op': 'scan'}, password: 'sekret', saltUuid16: salt, charShortName: 'wifiOp');
      final b = await LampCrypto.encryptOp(
          op: {'op': 'scan'}, password: 'sekret', saltUuid16: salt, charShortName: 'wifiOp');
      expect(a, isNot(equals(b)));
    });
  });

  group('LampCrypto round-trip', () {
    test('encryptOp → decryptOpForTesting yields the original JSON', () async {
      final salt = uuidSaltLE16(BleUuids.wifiOp);
      final ct = await LampCrypto.encryptOp(
          op: {'op': 'scan', 'ssid': 'home'},
          password: 'sekret',
          saltUuid16: salt,
          charShortName: 'wifiOp');
      final plain = await LampCrypto.decryptOpForTesting(
          ct,
          password: 'sekret',
          saltUuid16: salt,
          charShortName: 'wifiOp');
      expect(jsonDecode(utf8.decode(plain)),
          {'op': 'scan', 'ssid': 'home'});
    });

    test('wrong password fails to decrypt', () async {
      final salt = uuidSaltLE16(BleUuids.wifiOp);
      final ct = await LampCrypto.encryptOp(
          op: {'op': 'scan'},
          password: 'sekret',
          saltUuid16: salt,
          charShortName: 'wifiOp');
      await expectLater(
        LampCrypto.decryptOpForTesting(ct,
            password: 'wrong', saltUuid16: salt, charShortName: 'wifiOp'),
        throwsA(isA<SecretBoxAuthenticationError>()),
      );
    });

    test('wrong charShortName fails to decrypt', () async {
      final salt = uuidSaltLE16(BleUuids.wifiOp);
      final ct = await LampCrypto.encryptOp(
          op: {'op': 'scan'},
          password: 'sekret',
          saltUuid16: salt,
          charShortName: 'wifiOp');
      await expectLater(
        LampCrypto.decryptOpForTesting(ct,
            password: 'sekret',
            saltUuid16: salt,
            charShortName: 'mqttOp'),
        throwsA(isA<SecretBoxAuthenticationError>()),
      );
    });

    test('tampered tag fails to decrypt', () async {
      final salt = uuidSaltLE16(BleUuids.wifiOp);
      final ct = await LampCrypto.encryptOp(
          op: {'op': 'scan'},
          password: 'sekret',
          saltUuid16: salt,
          charShortName: 'wifiOp');
      // Flip a bit in the tag region (bytes 13..28).
      final tampered = Uint8List.fromList(ct);
      tampered[20] ^= 0x01;
      await expectLater(
        LampCrypto.decryptOpForTesting(tampered,
            password: 'sekret', saltUuid16: salt, charShortName: 'wifiOp'),
        throwsA(isA<SecretBoxAuthenticationError>()),
      );
    });
  });

  group('LampCrypto.decryptOpForTesting error paths', () {
    test('rejects short payload', () {
      expect(
        () => LampCrypto.decryptOpForTesting(Uint8List.fromList([0x02]),
            password: 'x',
            saltUuid16: uuidSaltLE16(BleUuids.wifiOp),
            charShortName: 'wifiOp'),
        throwsA(isA<FormatException>()),
      );
    });

    test('rejects wrong magic byte', () {
      // Need at least 1+12+16 bytes to get past the length check.
      final wire = Uint8List(1 + 12 + 16 + 1);
      wire[0] = 0x99;
      expect(
        () => LampCrypto.decryptOpForTesting(wire,
            password: 'x',
            saltUuid16: uuidSaltLE16(BleUuids.wifiOp),
            charShortName: 'wifiOp'),
        throwsA(isA<FormatException>()),
      );
    });

    test('rejects empty password', () async {
      final ct = await LampCrypto.encryptOp(
          op: {'op': 'scan'},
          password: 'sekret',
          saltUuid16: uuidSaltLE16(BleUuids.wifiOp),
          charShortName: 'wifiOp');
      expect(
        () => LampCrypto.decryptOpForTesting(ct,
            password: '',
            saltUuid16: uuidSaltLE16(BleUuids.wifiOp),
            charShortName: 'wifiOp'),
        throwsA(isA<FormatException>()),
      );
    });
  });
}
