import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/bridge_protocol.dart';

void main() {
  test('maps wire codes to the typed exceptions consumers catch', () {
    expect(bridgeErrorToException(BridgeErrorCode.disconnected, 'x'),
        isA<BleDisconnectedException>());
    expect(bridgeErrorToException(BridgeErrorCode.encryptionRequired, 'x'),
        isA<BleEncryptionRequired>());
    expect(bridgeErrorToException(BridgeErrorCode.notFound, 'x'),
        isA<BleNotFound>());
  });

  test('timeout/discovery carry the string signals the reconnect ladder greps', () {
    expect(bridgeErrorToException(BridgeErrorCode.timeout, 'x').toString().toLowerCase(),
        contains('timeout'));
    expect(bridgeErrorToException(BridgeErrorCode.discoveryFailed, 'x').toString().toLowerCase(),
        contains('discoverservices'));
  });

  test('isBleDisconnectError recognises the disconnected mapping', () {
    expect(isBleDisconnectError(bridgeErrorToException(BridgeErrorCode.disconnected, 'x')),
        isTrue);
  });

  test('parses wire strings, unknown falls through', () {
    expect(bridgeErrorCodeFromWire('READ_TOO_LARGE'), BridgeErrorCode.readTooLarge);
    expect(bridgeErrorCodeFromWire('NONSENSE'), BridgeErrorCode.unknown);
  });
}
