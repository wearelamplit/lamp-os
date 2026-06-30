import 'package:freezed_annotation/freezed_annotation.dart';

part 'lamp_nearby_peer.freezed.dart';
part 'lamp_nearby_peer.g.dart';

/// One peer the connected lamp can hear. Decoded from the lamp's
/// `nearby` page-protocol section JSON, NOT from the phone's direct
/// BLE scan. This is the LAMP'S vantage on its social context — used
/// by the Social tab so dispositions are scoped to peers the connected
/// lamp can actually greet.
///
/// Backward compatibility: legacy firmware doesn't emit `rssi` or
/// `proximity`. Missing rssi defaults to -127 (sentinel); missing proximity
/// defaults to 2 (Far), the safe-display fallback.
@freezed
abstract class LampNearbyPeer with _$LampNearbyPeer {
  const factory LampNearbyPeer({
    required String name,
    /// Canonical-form colon-hex BD_ADDR. Empty if the lamp firmware
    /// predates the bdAddr emit.
    @Default('') String bdAddr,
    /// Raw BLE-scan RSSI in dBm as observed by the connected lamp.
    /// `-127` means "no reading yet" (older firmware that doesn't emit
    /// RSSI, or a fresh peer not yet seen via BLE).
    @Default(-127) int rssi,
    /// Proximity bucket: 0=Near, 1=Around, 2=Far. Default 2 keeps legacy
    /// peers in a safe "Far" bucket rather than mis-classifying them as Near.
    @Default(2) int proximity,
    /// 4-channel RGBW for the lamp's base and shade. Used to render
    /// the lamp icon next to the row.
    @JsonKey(name: 'base') @Default(<int>[0, 0, 0, 0]) List<int> baseRgbw,
    @JsonKey(name: 'shade') @Default(<int>[0, 0, 0, 0]) List<int> shadeRgbw,
    @Default(false) bool viaBle,
    @Default(false) bool viaEspNow,
    @Default(0) int lastSeenMs,
  }) = _LampNearbyPeer;

  factory LampNearbyPeer.fromJson(Map<String, dynamic> json) =>
      _$LampNearbyPeerFromJson(json);
}
