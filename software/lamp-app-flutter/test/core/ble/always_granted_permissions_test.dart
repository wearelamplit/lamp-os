import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/always_granted_permissions.dart';

void main() {
  test('always grants, never permanently denied', () async {
    final p = AlwaysGrantedPermissions();
    expect(await p.request(), isTrue);
    expect(await p.isGranted(), isTrue);
    expect(await p.isPermanentlyDenied(), isFalse);
  });
}
