/// Sixteen critter SVGs at `assets/critters/critter-{1..16}.svg`, one per
/// `critterIndex` 1..16. Each carries a `critterNShade` + `critterNBody`
/// gradient pair so LampPreview can runtime-recolor head + body
/// independently.
library;

/// Total number of critter variants on disk. Must stay in sync with the
/// random pick range in `add_lamp_notifier._pickCritterIndex` and with
/// the actual file count under `assets/critters/`.
const critterCount = 16;

/// Smaller, simpler critter for lamps with no assigned identity yet
/// (discover / pre-adopt surfaces).
const strayCritterAsset = 'assets/critters/critter-stray.svg';

/// Look up the SVG asset path for a given critter index (1..[critterCount]).
/// Falls back to a stable hash of [deviceId] when [critterIndex] is null
/// (legacy lamps).
String critterAssetFor({
  required int? critterIndex,
  required String deviceId,
}) {
  final n = critterIndex ??
      (deviceId.codeUnits.fold<int>(0, (a, b) => a + b).abs() % critterCount) +
          1;
  final clamped = ((n - 1) % critterCount).abs() + 1;
  return 'assets/critters/critter-$clamped.svg';
}
