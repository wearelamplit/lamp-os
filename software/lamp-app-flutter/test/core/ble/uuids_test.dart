import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/uuids.dart';

void main() {
  test('control service UUID matches firmware', () {
    expect(BleUuids.controlService, '5f64f4d0-d6d9-4a44-9b3f-3a8d6f7e6b40');
  });

  test('every characteristic UUID is 36 chars and lowercase', () {
    for (final u in [
      BleUuids.brightness,
      BleUuids.shadeColors,
      BleUuids.baseColors,
      BleUuids.baseKnockout,
      BleUuids.expressionTest,
      BleUuids.expressionOp,
      BleUuids.settingsBlob,
      BleUuids.stateNotify,
      BleUuids.wifiOp,
      BleUuids.wifiState,
      BleUuids.pageCtrl,
      BleUuids.pageData,
      BleUuids.remoteOp,
      BleUuids.homeModeFocus,
      BleUuids.auth,
    ]) {
      expect(u.length, 36, reason: 'bad length: $u');
      expect(u, u.toLowerCase(), reason: 'must be lowercase: $u');
    }
  });
}
