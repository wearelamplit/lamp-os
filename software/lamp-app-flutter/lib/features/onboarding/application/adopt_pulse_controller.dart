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
    await _connectWithRetry(deviceId);
    if (_stopped) return;
    await _writePulse(deviceId, washed);
    if (_stopped) return;
    _timer = Timer.periodic(const Duration(milliseconds: 1500), (_) {
      unawaited(_writePulse(deviceId, washed));
    });
  }

  /// Android intermittently fails a GATT connect with GATT_ERROR(133),
  /// especially right after scanning in a crowded BLE environment (many
  /// lamps advertising + mesh). The same race [AddLampNotifier.submit]
  /// handles with a single retry — the pulse is best-effort identification,
  /// so retry a few times with a short backoff, bailing if [stop] ran.
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
        // 2 = base strip (the more visible surface), pulsed with the lamp's
        // shade colour for a clearer "this is the one" identification.
        'target': 2,
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
