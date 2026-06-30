/// Where the wisp's current zone selection came from. Mirrors the
/// `ZoneSelector::Source` enum on the wisp firmware side; the wire
/// format on `wispStatus.zoneSource` is the lowercase enum name.
///
/// - [none]      — no zone selected, wisp is idle.
/// - [firstSeen] — wisp adopted the first zone it observed on the mesh.
/// - [nvs]       — operator persisted a choice from a previous session.
/// - [appOp]     — operator set it this session via a `setZone` wispOp.
/// - [unknown]   — forward-compat sentinel for any value the firmware
///                 introduces before the app catches up.
enum ZoneSource {
  none,
  firstSeen,
  nvs,
  appOp,
  unknown,
}

/// Decode the wire-format string into the enum, so call sites switch on the
/// enum value rather than scattering `s.zoneSource == 'appOp'` literals.
ZoneSource parseZoneSource(String? raw) {
  switch (raw) {
    case 'none':
    case '':
    case null:
      return ZoneSource.none;
    case 'firstSeen':
      return ZoneSource.firstSeen;
    case 'nvs':
      return ZoneSource.nvs;
    case 'appOp':
      return ZoneSource.appOp;
    default:
      return ZoneSource.unknown;
  }
}

/// Wire-format string for round-tripping back to the wisp. The wisp
/// itself never accepts a `zoneSource` write (it's an output-only
/// status field), but the optimistic local update in
/// `WispNotifier.setZone` constructs a copyWith with the expected
/// post-write value for UI responsiveness.
String zoneSourceWire(ZoneSource s) {
  switch (s) {
    case ZoneSource.none:
      return 'none';
    case ZoneSource.firstSeen:
      return 'firstSeen';
    case ZoneSource.nvs:
      return 'nvs';
    case ZoneSource.appOp:
      return 'appOp';
    case ZoneSource.unknown:
      // Wire format reserved — should never be emitted from the app
      // since `unknown` only arises on parse. Round-tripping a string
      // we don't recognise back to the wisp would be a bug.
      return 'none';
  }
}
