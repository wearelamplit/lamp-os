import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import '../../../core/ble/ble_client.dart';
import '../../../core/ble/uuids.dart';
import '../../control/domain/lamp_color.dart';
import 'connect_with_retry.dart';

/// Connects to an unclaimed lamp and re-fires a `pulse` test_expression in the
/// lamp's own shade colour on the BASE strip, so the physical lamp pulses for
/// identification. Pulse is used (not breathing) because it doesn't yield to
/// wisp paint: the identify has to win over an active show. The firmware runs
/// a transient colored pulse from the payload. All writes best-effort; [stop]
/// idempotent.
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
    await connectWithRetry(_ble, deviceId, shouldAbort: () => _stopped);
    if (_stopped) return;
    await _writePulse(deviceId, shadeColor);
    if (_stopped) return;
    _timer = Timer.periodic(const Duration(milliseconds: 1500), (_) {
      unawaited(_writePulse(deviceId, shadeColor));
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

  Future<void> _writePulse(String deviceId, LampColor color) async {
    try {
      final payload = jsonEncode({
        'a': 'test_expression',
        'type': 'pulse',             // pulse draws over wisp paint; breathing yields to it
        'target': 2,                 // base strip
        'colors': [color.toHex()],   // firmware seeds a transient pulse with these
      });
      await _ble.write(deviceId, BleUuids.controlService, BleUuids.expressionTest,
          Uint8List.fromList(utf8.encode(payload)), withoutResponse: true);
    } catch (_) {}
  }

}
