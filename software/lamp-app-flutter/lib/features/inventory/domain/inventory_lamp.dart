import 'package:freezed_annotation/freezed_annotation.dart';

part 'inventory_lamp.freezed.dart';
part 'inventory_lamp.g.dart';

@freezed
abstract class InventoryLamp with _$InventoryLamp {
  const factory InventoryLamp({
    required String id,
    required String name,
    /// Lamp's raw mesh MAC (uppercase colon-hex), mirrored from
    /// CHAR_LAMP_SECTION's `lampId`. `id` is the platform BLE remoteId (a MAC
    /// on Android, a CoreBluetooth UUID on iOS); this is the stable address
    /// that matches peer-observed `lampId`s on both platforms (case-
    /// insensitively). Null on legacy entries and firmware predating the field.
    String? lampId,
    String? controlPassword,
    int? lastSeenEpochMs,
    /// Cached last-seen colors, written by `controlNotifier._updateSeen` on
    /// each connect-and-read and settled slider drag, persisted via
    /// `inventory.v1`. Read by `resolveLampColors` for tiles. Shape
    /// `[R,G,B,W]`; legacy length-3 entries are treated as W=0.
    List<int>? lastShadeColor,
    List<int>? lastBaseColor,
    /// Last observed `isMesh` (adv capability bit 1): true for v0x03 mesh
    /// firmware, false for legacy BT-only. Read by `lampRouteResolver` when
    /// the live roster is empty (lamp offline) so a BT-only lamp routes to
    /// BtOnlyLampScreen. Null legacy entries default to "assume mesh-capable".
    bool? lastKnownIsMesh,
    /// Lamp variant (`'standard'`, `'snafu'`). Populated post-connect from
    /// CHAR_LAMP_SECTION's `lampType`, persisted so OTA can fetch matching
    /// firmware offline. Null legacy entries surface a "reconnect once" error.
    String? lampType,
    /// Packed semver (`major<<16 | minor<<8 | patch`) of the last-seen
    /// firmware, mirrored from CHAR_LAMP_SECTION's `fwVersion` so My Lamps can
    /// show firmware identity offline.
    int? fwVersion,
    /// Firmware channel string the lamp was last seen running (e.g.
    /// `standard-stable`). Carries the v0x04 `{lampType}-{channel}` form.
    String? fwChannel,
  }) = _InventoryLamp;

  factory InventoryLamp.fromJson(Map<String, dynamic> json) =>
      _$InventoryLampFromJson(json);
}
