import '../../../core/widgets/status_dot.dart';
import '../../nearby/domain/nearby_lamp.dart';

/// Derives a [StatusKind] for a lamp at the current moment.
///
/// - `mesh`: the active connection right now. Pulses; the app is actually
///   reading + writing this lamp.
/// - `bluetooth`: heard via BLE adv within the nearby staleness window but
///   not the one connected (or connection is currently dropped).
/// - `searching`: not heard yet but the BLE scanner just started; the
///   caller passes `inScanGrace=true` while the grace window is open.
/// - `offline`: not heard, scan grace has expired.
///
/// Pure function so both `LampChip` in the AppBar and the rows in
/// `MyLampsScreen` share the same logic and stay in unit-test reach.
StatusKind statusFor({
  required String lampId,
  required List<NearbyLamp> nearby,
  required bool connected,
  bool inScanGrace = false,
}) {
  if (connected) return StatusKind.mesh;
  NearbyLamp? hit;
  for (final l in nearby) {
    if (l.id == lampId) {
      hit = l;
      break;
    }
  }
  return _kindFromHit(hit, inScanGrace);
}

/// Map-keyed overload for a caller that already has an id->NearbyLamp
/// index materialised at a higher scope (e.g. screen-level for a list of
/// inventory tiles). Saves the linear scan in [statusFor].
StatusKind statusForById({
  required String lampId,
  required Map<String, NearbyLamp> nearbyById,
  required bool connected,
  bool inScanGrace = false,
}) {
  if (connected) return StatusKind.mesh;
  return _kindFromHit(nearbyById[lampId], inScanGrace);
}

StatusKind _kindFromHit(NearbyLamp? hit, bool inScanGrace) {
  if (hit == null) {
    return inScanGrace ? StatusKind.searching : StatusKind.offline;
  }
  // `isMesh` = "firmware speaks the app's mesh protocol". Lamps with
  // the current build show as `mesh` whenever in range; legacy v1
  // firmware (BT-only) shows as `bluetooth`.
  return hit.isMesh ? StatusKind.mesh : StatusKind.bluetooth;
}
