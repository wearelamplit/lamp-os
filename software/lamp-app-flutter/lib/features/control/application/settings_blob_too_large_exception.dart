import '../../../core/ble/ble_client.dart';

/// A settings blob that would not fit one GATT write once encrypted, from
/// [ControlNotifier.writeSettingsBlob]. [message] is written for the user and
/// [toString] carries the sizes.
class SettingsBlobTooLargeException implements Exception {
  const SettingsBlobTooLargeException(this.encryptedLength);

  final int encryptedLength;

  String get message => "Couldn't save: too much to send to the lamp at once.";

  @override
  String toString() =>
      'SettingsBlobTooLargeException: $encryptedLength bytes '
      'encrypted (cap $kBleMaxWriteBytes)';
}
