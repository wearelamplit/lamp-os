import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import '../../control/domain/lamp_color.dart';
import '../../../core/ble/ble_client.dart';
import '../../../core/ble/uuids.dart';
import '../domain/wisp_claims.dart';
import '../domain/wisp_source_mode.dart';
import '../domain/wisp_status.dart';

/// Thin wrapper around the two wisp BLE characteristics. The notifier layer
/// owns lifetime/state; this class is just IO.
///
/// `CHAR_WISP_OP` accepts plaintext JSON (the lamp's WriteRouter is
/// plaintext-flavored). Auth is still gated by the connection-level
/// `isAuthed` check, which the BLE session already satisfies by the time the
/// user reaches the Wisp tab.
class WispRepository {
  WispRepository(this._ble, this._deviceId);

  final BleClient _ble;
  final String _deviceId;

  /// One-shot read of the merged wispStatus JSON. Empty / `"{}"` /
  /// unparseable payloads map to [WispStatus.empty].
  Future<WispStatus> readStatus() async {
    final bytes = await _ble.read(
      _deviceId,
      BleUuids.controlService,
      BleUuids.wispStatus,
    );
    return WispStatus.fromBytes(bytes);
  }

  /// Best-effort read of CHAR_WISP_CLAIMS. Returns the claimed mesh MACs, or
  /// null when the lamp can't answer (legacy firmware without the
  /// characteristic, timeout, transient error). Null means "unknown, don't
  /// filter"; the caller shows all lamps. Time-bounded so it can't stall the
  /// shared BLE flow.
  Future<Set<String>?> readClaims() async {
    try {
      final bytes = await _ble
          .read(_deviceId, BleUuids.controlService, BleUuids.wispClaims)
          .timeout(const Duration(seconds: 4));
      return parseClaimedMacs(bytes);
    } catch (_) {
      return null;
    }
  }

  /// Pin the wisp to [zoneId]. Persisted in wisp NVS — survives reboot.
  Future<void> setZone(int zoneId) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'setZone',
      'zoneId': zoneId,
    });
  }

  /// Revert the wisp to first-seen-wins. Clears the NVS pin.
  Future<void> clearZone() async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'clearZone',
    });
  }

  /// Source mode. Wisp persists in NVS and applies the
  /// appropriate transition (Off → broadcast RESTORE; Manual → push
  /// stored palette into CurrentPalette; Aurora → resume subscription).
  Future<void> setSource(WispSourceMode mode) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'setSource',
      'mode': wispSourceModeWire(mode),
    });
  }

  /// Manual palette. The wisp persists the (clamped to 50) color
  /// list in NVS and, if currently in Manual mode, pushes it into
  /// CurrentPalette so the lamps repaint without a mode flip. Palette is
  /// emitted as a list of `[r,g,b]` integer triples; W is intentionally
  /// dropped (the lamp's headroom math handles warm tinting locally).
  /// The cap aligns with `lamp_protocol::kMaxWispPaletteColors` on the
  /// firmware side so the MSG_WISP_PALETTE broadcast that follows can
  /// carry the whole palette without truncation.
  Future<void> setManualPalette(List<LampColor> palette) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'setManualPalette',
      'colors': [
        // The wisp dispatcher caps at 50 even if we send more, but we
        // also clamp client-side so a slightly stale UI doesn't waste
        // wire bytes on values that will be silently discarded.
        for (final c in palette.take(50)) [c.r, c.g, c.b],
      ],
    });
  }

  /// Off-mode color the wisp renders on its OWN 30-pixel ring when
  /// sourceMode == Off. Does NOT broadcast paint to the lamp grid —
  /// PaintDistributor stays held off in Off mode, so this color exists
  /// only on the wisp itself, "operating it like a lamp" per the
  /// product UX.
  Future<void> setOffColor(LampColor color) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'setOffColor',
      'color': <int>[color.r, color.g, color.b],
    });
  }

  /// Push new WiFi credentials to the wisp. The wisp persists them in
  /// NVS and immediately kicks WifiLink to reconnect and StageBeacon to
  /// refresh its BLE advert (so pre-mesh lamps follow the new SSID on
  /// their next scan). The wisp's own connection state surfaces back
  /// through `WispStatus.wifiConnected` on the next status notify.
  ///
  /// Accepted threat: the WiFi PSK leaks both here on the BLE write to
  /// `CHAR_WISP_OP` and downstream when the lamp re-broadcasts the wispOp as
  /// plaintext MSG_CONTROL_OP on the mesh (ESP-NOW range ~30 m LoS). Fleet-wide
  /// mesh auth would close it but is rejected; bounded by physical proximity.
  Future<void> setWifi(String ssid, String password) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'setWifi',
      'ssid': ssid,
      'pw': password,
    });
  }

  /// Commit a new drift interval and fade percentage. The wisp persists both
  /// in NVS and restarts the drift engine at the new rate.
  Future<void> setDrift(int intervalMs, int fadePct) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'setDrift',
      'intervalMs': intervalMs,
      'fadePct': fadePct,
    });
  }

  /// Re-roll per-lamp color assignments. The wisp bumps its shuffle seed,
  /// re-paints the fleet with the new assignments, and broadcasts a fresh
  /// wispStatus so the app preview re-rolls in lock-step.
  Future<void> shuffle() async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'shuffle',
    });
  }

  /// Hidden bench-test hook (firmware-gated on LAMP_DEBUG). Tells the
  /// connected lamp to fire a social-greeting waveform for the peer at
  /// [bdAddr] as if that peer had just walked into BLE range. Bypasses
  /// the natural per-mode cooldown / re-greet window / BLE-reachability
  /// gate so a tester can iterate on greeting waveforms without dragging
  /// physical lamps around the bench. The disposition matrix still
  /// applies: the lamp gets whatever waveform its current SocialMode +
  /// the peer's current Disposition would produce naturally. Release
  /// firmware ignores the op silently.
  Future<void> testGreet(String bdAddr) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'testGreet',
      'bdAddr': bdAddr,
    });
  }

  Future<void> _writeOp(Map<String, dynamic> payload) async {
    await _ble.write(
      _deviceId,
      BleUuids.controlService,
      BleUuids.wispOp,
      Uint8List.fromList(utf8.encode(jsonEncode(payload))),
    );
  }
}
