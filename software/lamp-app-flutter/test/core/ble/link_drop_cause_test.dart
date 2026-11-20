import 'package:flutter_blue_plus/flutter_blue_plus.dart' show ErrorPlatform;
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/fbp_ble_client.dart';

void main() {
  test('Android remote-user-terminated is the lamp hanging up', () {
    expect(linkDropCauseOf(ErrorPlatform.android, 0x13),
        LinkDropCause.lampHungUp);
  });

  test('every other reason is some other drop', () {
    for (final code in [null, 0, 0x08, 0x16, 0x3e, 133, 23789258]) {
      expect(linkDropCauseOf(ErrorPlatform.android, code), LinkDropCause.other,
          reason: 'android code $code');
    }
    for (final platform in [ErrorPlatform.apple, ErrorPlatform.fbp]) {
      expect(linkDropCauseOf(platform, 0x13), LinkDropCause.other,
          reason: '$platform shares no code space with Android HCI');
    }
  });
}
