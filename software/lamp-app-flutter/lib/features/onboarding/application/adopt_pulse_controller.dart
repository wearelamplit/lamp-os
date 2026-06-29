import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import '../../../core/ble/ble_client.dart';
import '../../../core/ble/uuids.dart';
import '../../control/domain/lamp_color.dart';
import 'identify_color.dart';

/// Connects to an unclaimed lamp, fires a washed-out `pulse` expression on a
/// repeat timer so the physical lamp pulses for identification, and cleans up
/// on [stop].
///
/// All BLE writes are best-effort — a dropped write does not crash the
/// controller. [stop] is idempotent.
class AdoptPulseController {
  AdoptPulseController(this._ble);

  final BleClient _ble;

  String? _deviceId;
  Timer? _timer;
  bool _stopped = false;

  Future<void> start(String deviceId, LampColor baseColor) async {
    _timer?.cancel();
    _deviceId = deviceId;
    _stopped = false;
    final washed = washedOutBright(baseColor);
    await _ble.connect(deviceId);
    await _writePulse(deviceId, washed);
    _timer = Timer.periodic(const Duration(milliseconds: 1500), (_) {
      unawaited(_writePulse(deviceId, washed));
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
      await _ble.write(
        deviceId,
        BleUuids.controlService,
        BleUuids.expressionTest,
        Uint8List.fromList(utf8.encode('{"a":"test_expression_complete"}')),
      );
    } catch (_) {}
    try {
      await _ble.disconnect(deviceId);
    } catch (_) {}
  }

  Future<void> _writePulse(String deviceId, LampColor color) async {
    try {
      final payload = jsonEncode({
        'a': 'test_expression',
        'type': 'pulse',
        'target': 3,
        // ponytail: #RRGGBBWW hex strings — matches ExpressionConfig/baseColors encoding
        'colors': [color.toHex()],
      });
      await _ble.write(
        deviceId,
        BleUuids.controlService,
        BleUuids.expressionTest,
        Uint8List.fromList(utf8.encode(payload)),
        withoutResponse: true,
      );
    } catch (_) {
      // best-effort — dropped writes are expected if the lamp is slow to connect
    }
  }
}
