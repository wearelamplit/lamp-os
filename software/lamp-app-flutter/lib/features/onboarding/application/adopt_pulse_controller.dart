import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import '../../../core/ble/ble_client.dart';
import '../../../core/ble/uuids.dart';
import '../../control/domain/lamp_color.dart';
import 'identify_color.dart';

/// Connects to an unclaimed lamp and re-fires a `pulse` test_expression in a
/// washed-out version of the lamp's shade colour on the BASE strip, so the
/// physical lamp pulses for identification. The firmware runs a transient
/// colored pulse expression from the payload (composes on top of wisp paint).
/// All writes best-effort; [stop] idempotent.
class AdoptPulseController {
  AdoptPulseController(this._ble);
  final BleClient _ble;

  String? _deviceId;
  Timer? _timer;
  bool _stopped = false;

  Future<void> start(String deviceId, LampColor shadeColor) async {
    _timer?.cancel();
    _deviceId = deviceId;
    _stopped = false;
    final color = washedOutBright(shadeColor);
    await _connectWithRetry(deviceId);
    if (_stopped) return;
    await _writePulse(deviceId, color);
    if (_stopped) return;
    _timer = Timer.periodic(const Duration(milliseconds: 1500), (_) {
      unawaited(_writePulse(deviceId, color));
    });
  }

  Future<void> stop() async {
    if (_stopped) return;
    _stopped = true;
    _timer?.cancel();
    _timer = null;
    final deviceId = _deviceId;
    if (deviceId == null) return;
    try {
      await _ble.write(deviceId, BleUuids.controlService, BleUuids.expressionTest,
          Uint8List.fromList(utf8.encode('{"a":"test_expression_complete"}')));
    } catch (_) {}
    try { await _ble.disconnect(deviceId); } catch (_) {}
  }

  /// Android throws GATT_ERROR(133) on connect intermittently in a crowded
  /// BLE environment; retry a few times with backoff, bailing if stop() ran.
  Future<void> _connectWithRetry(String deviceId) async {
    const attempts = 4;
    for (var i = 0; i < attempts; i++) {
      if (_stopped) return;
      try { await _ble.connect(deviceId); return; }
      catch (_) {
        if (i == attempts - 1) rethrow;
        await Future<void>.delayed(const Duration(milliseconds: 1200));
      }
    }
  }

  Future<void> _writePulse(String deviceId, LampColor color) async {
    try {
      final payload = jsonEncode({
        'a': 'test_expression',
        'type': 'pulse',
        'target': 2,                 // base strip
        'colors': [color.toHex()],   // firmware seeds a transient pulse with these
      });
      await _ble.write(deviceId, BleUuids.controlService, BleUuids.expressionTest,
          Uint8List.fromList(utf8.encode(payload)), withoutResponse: true);
    } catch (_) {}
  }
}
