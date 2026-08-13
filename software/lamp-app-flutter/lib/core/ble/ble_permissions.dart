import 'dart:io' show Platform;

import 'package:permission_handler/permission_handler.dart';

/// Wraps runtime BT permissions on Android AND iOS. The Android impl
/// gates BLUETOOTH_SCAN + BLUETOOTH_CONNECT; the iOS impl gates the
/// CoreBluetooth authorization state. Use [BlePermissions.forPlatform]
/// to get the right instance; both impls no-op `true` on the other
/// platform so the factory is safe to call unconditionally.
abstract class BlePermissions {
  Future<bool> isGranted();
  Future<bool> request();

  /// True if the OS has marked the permission unrecoverable from
  /// in-app prompts (Android "Don't ask again", iOS denied/restricted).
  /// UI should call [openSettings] in that case.
  Future<bool> isPermanentlyDenied();

  /// Open the OS app-settings screen so the user can flip the BT toggle
  /// after they've previously denied permanently.
  Future<void> openSettings();

  /// Returns the right [BlePermissions] implementation for the current
  /// platform. Useful in production wiring; tests inject a fake.
  factory BlePermissions.forPlatform() {
    if (Platform.isIOS) return IosBlePermissions();
    return AndroidBlePermissions();
  }
}

class AndroidBlePermissions implements BlePermissions {
  // Android 12+ (API 31+) uses BLUETOOTH_SCAN with `neverForLocation` and
  // doesn't need location. Older Android isn't a supported target here.
  static const _perms = [
    Permission.bluetoothScan,
    Permission.bluetoothConnect,
  ];

  @override
  Future<bool> isGranted() async {
    if (!Platform.isAndroid) return true;
    for (final p in _perms) {
      if (!await p.isGranted) return false;
    }
    return true;
  }

  @override
  Future<bool> request() async {
    if (!Platform.isAndroid) return true;
    final results = await _perms.request();
    return results.values.every((s) => s.isGranted);
  }

  @override
  Future<bool> isPermanentlyDenied() async {
    if (!Platform.isAndroid) return false;
    for (final p in _perms) {
      if (await p.isPermanentlyDenied) return true;
    }
    return false;
  }

  @override
  Future<void> openSettings() => openAppSettings();
}

/// iOS BT permission wrapper. CoreBluetooth surfaces three terminal
/// states beyond "granted": denied (user said no in the system dialog),
/// restricted (parental controls / MDM), and permanentlyDenied
/// (effectively the same as denied on iOS; there is no second-prompt
/// distinction). All three route the user to Settings since the system
/// dialog only fires once per install.
class IosBlePermissions implements BlePermissions {
  @override
  Future<bool> isGranted() async {
    if (!Platform.isIOS) return true;
    return Permission.bluetooth.isGranted;
  }

  @override
  Future<bool> request() async {
    if (!Platform.isIOS) return true;
    final status = await Permission.bluetooth.request();
    return status.isGranted;
  }

  @override
  Future<bool> isPermanentlyDenied() async {
    if (!Platform.isIOS) return false;
    final status = await Permission.bluetooth.status;
    // On iOS, both `denied` (post-first-deny) and `restricted` (MDM /
    // parental controls) require Settings. permanentlyDenied is also
    // theoretically possible per the plugin enum.
    return status.isDenied ||
        status.isRestricted ||
        status.isPermanentlyDenied;
  }

  @override
  Future<void> openSettings() => openAppSettings();
}
