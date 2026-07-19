import 'ble_permissions.dart';

// Simulator/emulator has no Bluetooth to authorize; the host bridge owns the
// radio, so short-circuit the OS prompt that would otherwise block boot.
class AlwaysGrantedPermissions implements BlePermissions {
  @override
  Future<bool> isGranted() async => true;
  @override
  Future<bool> request() async => true;
  @override
  Future<bool> isPermanentlyDenied() async => false;
  @override
  Future<void> openSettings() async {}
}
