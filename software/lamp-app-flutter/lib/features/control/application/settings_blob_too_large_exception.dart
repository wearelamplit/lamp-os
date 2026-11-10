import '../../../core/ble/ble_client.dart';

/// Thrown by [ControlNotifier.writeSettingsBlob] before it writes anything,
/// when the blob encrypted would not fit one GATT write. [message] is written
/// for the user; [toString] carries the sizes.
class SettingsBlobTooLargeException implements Exception {
  const SettingsBlobTooLargeException(this.encryptedLength);

  final int encryptedLength;

  String get message => "Couldn't save: too much to send to the lamp at once.";

  @override
  String toString() =>
      'SettingsBlobTooLargeException: $encryptedLength bytes '
      'encrypted (cap $kBleMaxWriteBytes)';
}
