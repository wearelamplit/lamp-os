import '../../../core/ble/ble_client.dart';
import '../../../core/ble/lamp_crypto.dart';
import '../../../core/ble/uuids.dart';

/// Writes [BleUuids.auth] so the firmware grants subsequent control writes on
/// this connection.  Empty/null password is treated as a factory-default lamp
/// (firmware grants open access; no write needed).  Non-empty password: sends
/// a ciphertext frame — the firmware's GCM auth-tag verification on CHAR_AUTH
/// implicitly authenticates without exposing the password on the wire.
class AuthClient {
  AuthClient({required this.ble});

  final BleClient ble;

  Future<void> authenticate({
    required String deviceId,
    required String? password,
  }) async {
    if (password == null || password.isEmpty) return;
    // SECURITY (audit sec-H2, deferred): the encrypted payload is a
    // CONSTANT plaintext (`{auth: true}`) with a random GCM nonce. A
    // passive BLE sniffer who captures one CHAR_AUTH write can replay
    // the captured frame verbatim from their own phone and the lamp
    // accepts it. The real fix needs a lamp-side per-connection
    // challenge characteristic (read returns a fresh nonce; client
    // mixes it into the payload; lamp rejects reuse) — a firmware-
    // coordinated change. Tracked in
    // docs/accepted-security-threats.md.
    // Ciphertext body is opaque — firmware's CHAR_AUTH dispatcher only
    // cares that the GCM tag verifies. Send a tiny constant JSON.
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
