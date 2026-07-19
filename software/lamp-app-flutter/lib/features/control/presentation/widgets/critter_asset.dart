/// Sixteen critter SVGs at `assets/critters/critter-{1..16}.svg`. Each carries
/// a `critterNShade` + `critterNBody` gradient pair so LampPreview can
/// runtime-recolor head + body independently.
library;

/// Total number of critter variants on disk. Must stay in sync with
/// [critterIndexFor]'s range and the actual file count under
/// `assets/critters/`.
const critterCount = 16;

/// Smaller, simpler critter for lamps with no assigned identity yet
/// (discover / pre-adopt surfaces).
const strayCritterAsset = 'assets/critters/critter-stray.svg';

/// Deterministic critter index (1..[critterCount]) for [identity]. Same
/// identity always yields the same index, so every user and platform derives
/// the same critter for a given lamp (`lampId`) without syncing anything.
/// Case-folded so a lampId is robust against any source that skips the
/// firmware's uppercase formatting.
int critterIndexFor(String identity) =>
    (identity.toUpperCase().codeUnits.fold<int>(0, (a, b) => a + b) %
        critterCount) +
    1;

/// SVG asset path for the critter deterministically derived from [identity]
/// (`lampId`, falling back to the platform `deviceId` for lamps with neither).
String critterAssetFor(String identity) =>
    'assets/critters/critter-${critterIndexFor(identity)}.svg';
