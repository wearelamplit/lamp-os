import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import '../../../core/ble/ble_client.dart';
import '../../../core/ble/uuids.dart';
import '../../control/domain/lamp_color.dart';
import 'identify_color.dart';

/// Connects to an unclaimed lamp and pulses its BASE strip in a washed-out
/// version of the lamp's SHADE colour, to identify it during adopt-confirm.
/// Drives the base-colours characteristic directly (a factory lamp accepts it
/// unauthenticated) and claims the base edit-session so a wisp's paint doesn't
/// override the pulse. Restores the original base colour + releases the
/// session on [stop]. All writes best-effort; [stop] idempotent.
class AdoptPulseController {
  AdoptPulseController(this._ble);
  final BleClient _ble;

  static const int _editBaseBit = 0x01;

  String? _deviceId;
  LampColor? _restoreBase;
  Timer? _timer;
  bool _stopped = false;

  Future<void> start(
      String deviceId, LampColor shadeColor, LampColor baseColor) async {
    _timer?.cancel();
    _deviceId = deviceId;
    _restoreBase = baseColor;
    _stopped = false;
    final bright = washedOutBright(shadeColor);
    final dim = _dim(bright);
    await _connectWithRetry(deviceId);
    if (_stopped) return;
    // Claim the base surface so a wisp's paint doesn't override the pulse.
    await _writeEditSession(deviceId, open: true);
    if (_stopped) return;
    var on = true;
    await _writeBase(deviceId, bright);
    _timer = Timer.periodic(const Duration(milliseconds: 600), (_) {
      on = !on;
      unawaited(_writeBase(deviceId, on ? bright : dim));
    });
  }

  Future<void> stop() async {
    if (_stopped) return;
    _stopped = true;
    _timer?.cancel();
    _timer = null;
    final deviceId = _deviceId;
    if (deviceId == null) return;
    final restore = _restoreBase;
    if (restore != null) {
      try { await _writeBase(deviceId, restore); } catch (_) {}
    }
    try { await _writeEditSession(deviceId, open: false); } catch (_) {}
    try { await _ble.disconnect(deviceId); } catch (_) {}
  }

  /// Android throws GATT_ERROR(133) on connect intermittently in a crowded
  /// BLE environment; retry a few times with backoff, bailing if stop() ran.
  Future<void> _connectWithRetry(String deviceId) async {
    const attempts = 4;
    for (var i = 0; i < attempts; i++) {
      if (_stopped) return;
      try {
        await _ble.connect(deviceId);
        return;
      } catch (_) {
        if (i == attempts - 1) rethrow;
        await Future<void>.delayed(const Duration(milliseconds: 1200));
      }
    }
  }

  Future<void> _writeBase(String deviceId, LampColor c) async {
    try {
      final payload = utf8.encode(jsonEncode([c.toHex()]));
      await _ble.write(deviceId, BleUuids.controlService, BleUuids.baseColors,
          Uint8List.fromList(payload), withoutResponse: true);
    } catch (_) {}
  }

  Future<void> _writeEditSession(String deviceId, {required bool open}) async {
    try {
      await _ble.write(deviceId, BleUuids.controlService, BleUuids.editSession,
          Uint8List.fromList([_editBaseBit, open ? 1 : 0]),
          withoutResponse: true);
    } catch (_) {}
  }

  LampColor _dim(LampColor c) => LampColor(
      r: (c.r * 0.15).round(),
      g: (c.g * 0.15).round(),
      b: (c.b * 0.15).round(),
      w: (c.w * 0.15).round());
}
