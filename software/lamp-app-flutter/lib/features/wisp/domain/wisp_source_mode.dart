/// Where the wisp gets its paint palette from. Mirrors the wisp-side
/// `WispSourceMode` enum in `software/wisp/src/config/wisp_config.hpp`:
///   off:    wisp does not broadcast paint; lamps run local behavior.
///   manual: wisp broadcasts paint from an operator-defined palette.
///   osc:    wisp follows a palette streamed to it over OSC (Straylight).
///           While no palette arrives the wisp holds the lamps at Off and
///           resumes painting on its own when the stream returns.
///
/// Wire-format on `wispStatus.source` is the lowercase enum name; the
/// wispOp setter accepts the same strings.
enum WispSourceMode {
  off,
  manual,
  osc,
}

/// Decode a wire-format `source` string into the enum. Anything not
/// recognized (older wisp, future field, missing key) maps to [off], the
/// safe default: falling back to Off can never accidentally override the
/// lamps; the user has to explicitly opt in.
WispSourceMode parseWispSourceMode(String? raw) {
  switch (raw) {
    case 'manual':
      return WispSourceMode.manual;
    case 'osc':
      return WispSourceMode.osc;
    default:
      return WispSourceMode.off;
  }
}

/// Wire-format string for a source mode. Used in the `wispOp setSource`
/// JSON payload so the dispatcher on the wisp accepts it directly without
/// numeric coercion.
String wispSourceModeWire(WispSourceMode m) {
  switch (m) {
    case WispSourceMode.off:
      return 'off';
    case WispSourceMode.manual:
      return 'manual';
    case WispSourceMode.osc:
      return 'osc';
  }
}

/// Which Straylight palette role the wisp listens to in [WispSourceMode.osc].
/// Wire format on `wispStatus.oscRole` and the `setOscRole` wispOp is the
/// lowercase enum name; the wisp omits the key at [primary].
enum OscRole {
  primary,
  secondary,
  tertiary,
  fill,
}

/// Decode a wire-format `oscRole`; absent or unknown maps to [OscRole.primary],
/// the wisp's default.
OscRole parseOscRole(String? raw) {
  for (final r in OscRole.values) {
    if (r.name == raw) return r;
  }
  return OscRole.primary;
}

/// Wire-format string for an [OscRole].
String oscRoleWire(OscRole r) => r.name;

/// Which OSC sender the wisp listens to in [WispSourceMode.osc]. Wire format
/// on `wispConfig.oscMode` and the `setOsc` wispOp is the lowercase enum
/// name. [custom] follows one address the owner sets ([oscCustomAddressValid]).
/// [unknown] is a value this app cannot name, from a newer wisp; it is never
/// sent.
enum OscMode {
  straylight,
  maestro,
  custom,
  unknown,
}

/// Decode a wire-format `oscMode`: null when absent (a wisp that predates
/// it), [OscMode.unknown] for a value this app does not know.
OscMode? parseOscMode(Object? raw) {
  if (raw == null) return null;
  for (final m in OscMode.values) {
    if (m != OscMode.unknown && m.name == raw) return m;
  }
  return OscMode.unknown;
}

/// The Maestro palette slots the wisp accepts in `setOsc`.
const int kMaestroSlotMin = 1;
const int kMaestroSlotMax = 10;

/// Decode a wire-format `oscSlot`: null when absent or outside
/// [kMaestroSlotMin]..[kMaestroSlotMax].
int? parseMaestroSlot(Object? raw) =>
    raw is int && raw >= kMaestroSlotMin && raw <= kMaestroSlotMax ? raw : null;

/// The longest Custom OSC address the wisp stores, in bytes.
const int kOscCustomAddressMaxLen = 15;

/// Why a Custom OSC address breaks the wisp's rule.
enum OscAddressProblem { empty, noLeadingSlash, tooLong, badCharacter }

const String _kOscAddressForbidden = '"\\#*,?[]{}';

/// What is wrong with [address] as a Custom OSC address, or null when the
/// wisp takes it. The rule is the wisp's `setOsc` address rule in
/// `docs/dev/networking.md`.
OscAddressProblem? oscCustomAddressProblem(String address) {
  if (address.isEmpty) return OscAddressProblem.empty;
  if (!address.startsWith('/')) return OscAddressProblem.noLeadingSlash;
  for (final c in address.codeUnits) {
    if (c < 0x21 || c > 0x7E || _kOscAddressForbidden.codeUnits.contains(c)) {
      return OscAddressProblem.badCharacter;
    }
  }
  if (address.length > kOscCustomAddressMaxLen) {
    return OscAddressProblem.tooLong;
  }
  return null;
}

bool oscCustomAddressValid(String address) =>
    oscCustomAddressProblem(address) == null;
