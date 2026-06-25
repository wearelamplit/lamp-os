import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/lamp_crypto.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/application/auth_client.dart';

void main() {
  test('writes CHAR_AUTH with a ciphertext-framed payload', () async {
    final ble = InMemoryBleClient();
    await ble.connect('dev1');
    final auth = AuthClient(ble: ble);

    await auth.authenticate(deviceId: 'dev1', password: 'open sesame');

    final written = await ble.read(
        'dev1', BleUuids.controlService, BleUuids.auth);
    // Should be magic byte + 12B nonce + 16B tag + ciphertext
    expect(written[0], LampCrypto.magicCiphertext);
    // {"auth":true} is 13 bytes; total = 1 + 12 + 16 + 13 = 42
    expect(written.length,
        1 + LampCrypto.nonceLen + LampCrypto.tagLen +
            utf8.encode('{"auth":true}').length);
  });

  test('no-op when password is null or empty', () async {
    final ble = InMemoryBleClient();
    await ble.connect('dev1');
    final auth = AuthClient(ble: ble);

    await auth.authenticate(deviceId: 'dev1', password: null);
    await auth.authenticate(deviceId: 'dev1', password: '');

    expect(
      () => ble.read('dev1', BleUuids.controlService, BleUuids.auth),
      throwsA(isA<BleNotFound>()),
    );
  });
}
