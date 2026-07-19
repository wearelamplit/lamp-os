import 'package:freezed_annotation/freezed_annotation.dart';

part 'nearby_lamp.freezed.dart';
part 'nearby_lamp.g.dart';

@freezed
abstract class NearbyLamp with _$NearbyLamp {
  const NearbyLamp._();

  const factory NearbyLamp({
    required String id,
    required String name,
    required int rssi,
    required List<String> serviceUuids,
    required int baseRgb,
    required int shadeRgb,
    required int lastSeenEpochMs,
    /// True iff this lamp's firmware advertises the version byte,
    /// i.e. speaks the app's mesh protocol and is fully app-
    /// controllable. Drives the BT-only routing decision in
    /// MyLampsScreen and the `mesh` vs `bluetooth` status dot. v1
    /// firmware (legacy BT-only) and transitional pre-shade-restore
    /// v2 builds both get `false`.
    @Default(false) bool isMesh,
    /// True once the lamp has been claimed/set up (capability bit 0x04 in
    /// the advertisement). Drives the adopt-wizard vs one-tap-add routing.
    @Default(false) bool configured,
  }) = _NearbyLamp;

  factory NearbyLamp.fromJson(Map<String, dynamic> json) =>
      _$NearbyLampFromJson(json);

  /// True when the lamp hasn't been set up yet: the advertised `configured`
  /// bit is clear. Fresh AND custom lamps report this until claimed, so the
  /// AddLamp flow routes them through the adopt wizard; a configured lamp is
  /// added in one tap.
  bool get isFactoryDefault => !configured;
}
