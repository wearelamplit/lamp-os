import 'package:freezed_annotation/freezed_annotation.dart';

part 'lamp_nearby_peer.freezed.dart';
part 'lamp_nearby_peer.g.dart';

/// One peer the connected lamp can hear. Decoded from the lamp's
/// `nearby` page-protocol section JSON, NOT from the phone's direct
/// BLE scan. This is the LAMP'S vantage on its social context, used
/// by the Social tab so dispositions are scoped to peers the connected
/// lamp can actually greet.
///
/// Backward compatibility: legacy firmware doesn't emit `rssi` (missing
/// defaults to -127, the sentinel) and emits colors as 4-int RGBW lists;
/// current firmware emits 8-hex-char "rrggbbww" strings. `_rgbwFromJson`
/// accepts both. A legacy `proximity` key is ignored; proximity is the
/// firmware-computed `near` flag.
@freezed
abstract class LampNearbyPeer with _$LampNearbyPeer {
  const factory LampNearbyPeer({
    required String name,
    /// Raw mesh MAC, uppercase colon-hex. Empty if the lamp firmware
    /// predates the lampId emit.
    @Default('') String lampId,
    /// Raw BLE-scan RSSI in dBm as observed by the connected lamp.
    /// `-127` means "no reading yet" (older firmware that doesn't emit
    /// RSSI, or a fresh peer not yet seen via BLE).
    @Default(-127) int rssi,
    /// 4-channel RGBW for the lamp's base and shade. Used to render
    /// the lamp icon next to the row.
    @JsonKey(name: 'base', fromJson: _rgbwFromJson)
    @Default(<int>[0, 0, 0, 0])
    List<int> baseRgbw,
    @JsonKey(name: 'shade', fromJson: _rgbwFromJson)
    @Default(<int>[0, 0, 0, 0])
    List<int> shadeRgbw,
    @Default(false) bool viaBle,
    @Default(false) bool viaEspNow,
    @Default(0) int lastSeenMs,
    /// Packed semver (major<<16 | minor<<8 | patch). 0 = unknown/legacy
    /// peer (the lamp omits it when zero).
    @Default(0) int fwVersion,
    /// `{type}-{channel}` slot, e.g. "standard-beta". Empty on legacy
    /// firmware that predates the emit.
    @Default('') String fwChannel,
    /// OTA state: 0=idle, 1=sending, 2=receiving. Omitted by the lamp
    /// when idle.
    @Default(0) int otaState,
    /// Mesh MAC (uppercase colon-hex) of the peer this lamp is OTA-sending
    /// to. Null unless the lamp is Sending. Resolve against inventory by
    /// `lampId` to name the receiver, which is HELLO-silent during its OTA.
    String? otaSendingTo,
    /// Firmware-computed proximity: true = near, false = far. The lamp
    /// derives this from its own RSSI tiers; the app just renders it.
    @Default(false) bool near,
  }) = _LampNearbyPeer;

  factory LampNearbyPeer.fromJson(Map<String, dynamic> json) =>
      _$LampNearbyPeerFromJson(json);
}

/// Accepts both firmware color shapes: legacy 4-int RGBW list and the
/// current 8-hex-char "rrggbbww" string. Anything else (missing key,
/// malformed value) falls back to black.
List<int> _rgbwFromJson(dynamic value) {
  if (value is List) {
    return [
      for (var i = 0; i < 4; i++)
        if (i < value.length && value[i] is num)
          (value[i] as num).toInt().clamp(0, 255)
        else
          0,
    ];
  }
  if (value is String && value.length == 8) {
    final packed = int.tryParse(value, radix: 16);
    if (packed != null) {
      return [
        (packed >> 24) & 0xFF,
        (packed >> 16) & 0xFF,
        (packed >> 8) & 0xFF,
        packed & 0xFF,
      ];
    }
  }
  return const [0, 0, 0, 0];
}

/// Display substitution for legacy BLE-only peers. Such a lamp omits the
/// W channel from its advertisement, so a stored W-white shade/base
/// `(0,0,0,255)` arrives all-zero and would render black. White is the one
/// channel legacy omits, so an all-zero legacy color means white: return
/// `(0,0,0,255)`. A real mesh peer's all-zero color is genuinely off and
/// stays black, so the swap is gated to legacy (BLE-only) peers.
List<int> displayRgbw(List<int> rgbw, {required bool legacyOnlyBle}) {
  if (legacyOnlyBle &&
      rgbw.length >= 4 &&
      rgbw[0] == 0 &&
      rgbw[1] == 0 &&
      rgbw[2] == 0 &&
      rgbw[3] == 0) {
    return const [0, 0, 0, 255];
  }
  return rgbw;
}
