import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_permissions.dart';

class _AlwaysGranted implements BlePermissions {
  int requestCalls = 0;
  @override
  Future<bool> request() async {
    requestCalls += 1;
    return true;
  }

  @override
  Future<bool> isGranted() async => true;

  @override
  Future<bool> isPermanentlyDenied() async => false;

  @override
  Future<void> openSettings() async {}
}

void main() {
  test('test double records request calls', () async {
    final p = _AlwaysGranted();
    expect(await p.isGranted(), isTrue);
    expect(await p.request(), isTrue);
    expect(p.requestCalls, 1);
  });
}
