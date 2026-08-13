import 'ble_client.dart';

/// Wire error vocabulary the bridge emits. Beyond the four typed exceptions,
/// `timeout` and `discoveryFailed` exist because control_notifier's reconnect
/// ladder ALSO branches on `e.toString()` substrings ('timeout',
/// 'discoverservices'); a bridge failure surfaced as anything else would be
/// treated as fatal and break the retry.
enum BridgeErrorCode {
  disconnected,
  encryptionRequired,
  readTooLarge,
  notFound,
  timeout,
  discoveryFailed,
  unknown,
}

BridgeErrorCode bridgeErrorCodeFromWire(String code) {
  switch (code) {
    case 'DISCONNECTED':
      return BridgeErrorCode.disconnected;
    case 'ENCRYPTION_REQUIRED':
      return BridgeErrorCode.encryptionRequired;
    case 'READ_TOO_LARGE':
      return BridgeErrorCode.readTooLarge;
    case 'NOT_FOUND':
      return BridgeErrorCode.notFound;
    case 'TIMEOUT':
      return BridgeErrorCode.timeout;
    case 'DISCOVERY_FAILED':
      return BridgeErrorCode.discoveryFailed;
    default:
      return BridgeErrorCode.unknown;
  }
}

/// Exception carrying a string the ladder greps for transient signals.
class BridgeTransientException implements Exception {
  const BridgeTransientException(this.message);
  final String message;
  @override
  String toString() => message;
}

Object bridgeErrorToException(
  BridgeErrorCode code,
  String deviceId, {
  String? message,
}) {
  switch (code) {
    case BridgeErrorCode.disconnected:
      return BleDisconnectedException(deviceId, message);
    case BridgeErrorCode.encryptionRequired:
      return BleEncryptionRequired(deviceId);
    case BridgeErrorCode.readTooLarge:
      return BleReadTooLarge(deviceId, 0, kBleMaxReadBytes);
    case BridgeErrorCode.notFound:
      return BleNotFound(message ?? deviceId);
    case BridgeErrorCode.timeout:
      return BridgeTransientException('bridge timeout: $deviceId');
    case BridgeErrorCode.discoveryFailed:
      return BridgeTransientException('bridge discoverServices failed: $deviceId');
    case BridgeErrorCode.unknown:
      return BridgeTransientException('bridge error: ${message ?? deviceId}');
  }
}
