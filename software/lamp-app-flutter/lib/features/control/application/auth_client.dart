import '../../../core/ble/ble_client.dart';
import '../../../core/ble/lamp_crypto.dart';
import '../../../core/ble/uuids.dart';

/// Writes [BleUuids.auth] so the firmware grants subsequent control writes on
/// this connection.  Empty/null password is treated as a factory-default lamp
/// (firmware grants open access; no write needed).  Non-empty password: sends
/// a ciphertext frame. The firmware's GCM auth-tag verification on CHAR_AUTH
/// implicitly authenticates without exposing the password on the wire.
class AuthClient {
  AuthClient({required this.ble});

  final BleClient ble;

  Future<void> authenticate({
    required String deviceId,
    required String? password,
  }) async {
    if (password == null || password.isEmpty) return;
    // Accepted threat: the encrypted payload is a CONSTANT plaintext
    // (`{auth: true}`) with a random GCM nonce, so a passive sniffer can
    // replay a captured CHAR_AUTH write verbatim and the lamp accepts it. The
    // fix needs a lamp-side per-connection challenge characteristic.
    // Ciphertext body is opaque; firmware's CHAR_AUTH dispatcher only cares
    // that the GCM tag verifies. Send a tiny constant JSON.
    final bytes = await LampCrypto.encryptOp(
      op: const {'auth': true},
      password: password,
      saltUuid16: uuidSaltLE16(BleUuids.auth),
      charShortName: 'auth',
    );
    await ble.write(
      deviceId,
      BleUuids.controlService,
      BleUuids.auth,
      bytes,
    );
  }
}
