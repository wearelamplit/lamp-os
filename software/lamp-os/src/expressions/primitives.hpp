#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>

#include "expressions/param_utils.hpp"

namespace lamp {

constexpr uint32_t kMsPerSecond = 1000;

// Frame budget per trigger for expressions whose end is decided by wall-clock
// or wave-position state (~27 min at the compositor's 16 ms flush cadence).
// Steady-state continuous instances rewind via rewindBeforeExhaust();
// one-shots conclude by setting frames = frame + 1.
constexpr uint32_t kContinuousMaxFrames = 100000;

// Wrap-safe "now has reached deadline" for millis()-domain timestamps.
inline bool timeReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

enum class TaperCurve : uint8_t { Linear, Quadratic };

// Edge-taper weight (0..100) for pixel `idx` of a `size`-wide point: flat 100
// across the interior, tapering only within `taperWidth` pixels of each end.
// `Quadratic` is ease-out (dimming concentrated at the very edge, smooth
// recovery). Symmetric by construction. Integer math; header-only native-test
// seam.
inline uint32_t edgeTaper(uint16_t idx, uint16_t size, uint16_t taperWidth,
                          TaperCurve curve) {
  if (size <= 1) return 100;
  const uint16_t distFromEnd = std::min<uint16_t>(idx, size - 1 - idx);
  if (distFromEnd >= taperWidth) return 100;
  const uint32_t ramp = (distFromEnd + 1u) * 100u / (taperWidth + 1u);
  if (curve == TaperCurve::Linear) return ramp;
  return 100u - (100u - ramp) * (100u - ramp) / 100u;
}

// Keeps a frame counter from exhausting while wall-clock state still owns the
// animation's end: nextFrame() at frame == frames - 1 flips PLAYING_ONCE to
// STOPPED, which retriggers a continuous instance (visible snap + spurious
// mesh event) or ends a timed cycle early. Call on `frame` right before
// nextFrame().
inline uint32_t rewindBeforeExhaust(uint32_t frame, uint32_t frames) {
  return (frame + 1 >= frames) ? 0 : frame;
}

// Pixel sub-range an effect paints within. Absent params span the full
// window; reversed bounds swap; out-of-range clamps in.
struct Zone {
  uint16_t posMin = 0;
  uint16_t posMax = 0;

  static Zone fromParameters(const std::map<std::string, uint32_t>& p,
                             uint16_t windowSize) {
    Zone r;
    if (windowSize == 0) { r.posMin = 1; r.posMax = 0; return r; }
    const uint16_t last = windowSize - 1;
    uint32_t lo = getParam(p, "posMin", 0);
    uint32_t hi = getParam(p, "posMax", last);
    if (lo > last) lo = last;
    if (hi > last) hi = last;
    if (lo > hi) std::swap(lo, hi);
    r.posMin = static_cast<uint16_t>(lo);
    r.posMax = static_cast<uint16_t>(hi);
    return r;
  }

  // Pixel count in the zone. Zero only when windowSize was zero.
  uint16_t size() const {
    if (posMax < posMin) return 0;
    return static_cast<uint16_t>(posMax - posMin + 1);
  }
};

// Zone from the whole-strip/region toggle: fullStrip=1 (default) spans the
// window, ignoring any stale posMin/posMax the Region mode left behind.
inline Zone resolveZone(const std::map<std::string, uint32_t>& p, uint16_t window) {
  return getParam(p, "fullStrip", 1) != 0 ? Zone::fromParameters({}, window)
                                          : Zone::fromParameters(p, window);
}

// Count of independent active points. Clamped to [1, windowSize].
struct Points {
  uint16_t count = 1;

  static Points fromParameters(const std::map<std::string, uint32_t>& p,
                               uint16_t windowSize, uint16_t defaultCount) {
    uint32_t c = getParam(p, "count", defaultCount);
    const uint16_t hi = windowSize == 0 ? 1 : windowSize;
    if (c < 1) c = 1;
    if (c > hi) c = hi;
    Points pts;
    pts.count = static_cast<uint16_t>(c);
    return pts;
  }
};

// Pixels each effect-point occupies. Clamped to [1, windowSize]; returns
// defaultValue when absent.
inline uint16_t parseSize(const std::map<std::string, uint32_t>& p,
                          uint16_t windowSize, uint16_t defaultValue) {
  uint32_t s = getParam(p, "size", defaultValue);
  const uint16_t hi = windowSize == 0 ? 1 : windowSize;
  if (s < 1) s = 1;
  if (s > hi) s = hi;
  return static_cast<uint16_t>(s);
}

// Fade radius at 100% size, as a fraction of zone size. At 0.5 the lit span
// reads at roughly the full zone at max size. Bench-tunable.
constexpr float kPulseMaxWidthFrac = 0.5f;

// Pulse fade radius (pixels) from a size percent of `zoneSize`. 3px floor
// kills the blocky 1px look on small zones.
inline uint16_t pulseWidthFromPercent(uint16_t sizePercent, uint16_t zoneSize) {
  const long r = std::lround(sizePercent / 100.0 * zoneSize * kPulseMaxWidthFrac);
  return static_cast<uint16_t>(std::max<long>(3, r));
}

// Each phase-offset breath band must span at least this many pixels.
constexpr uint16_t kMinSectionPx = 5;

// Breath bands that fit `zoneSize`: the requested `sections`, floored to 1 and
// capped so every band holds >= kMinSectionPx.
inline uint16_t usableSections(uint16_t sections, uint16_t zoneSize) {
  const uint16_t fit = std::max<uint16_t>(1u, zoneSize / kMinSectionPx);
  uint16_t s = sections < 1 ? 1 : sections;
  if (s > fit) s = fit;
  return s;
}

// Fill order[0..n-1] with a random permutation of 0..n-1 (Fisher-Yates).
// `rng` needs a `range(lo, hi)` returning an inclusive uniform draw.
template <typename Order, typename Rng>
inline void randomPermutation(Order& order, uint16_t n, Rng& rng) {
  for (uint16_t i = 0; i < n; ++i) order[i] = static_cast<uint8_t>(i);
  for (uint16_t i = n; i > 1; --i) {
    const uint16_t j = static_cast<uint16_t>(rng.range(0, i - 1));
    std::swap(order[i - 1], order[j]);
  }
}

// Glitchy scatter: level 0..5 -> {density percent, grain pixels}. Level 0 is
// the solid-fill sentinel (grainPx 0). Bench-tunable.
struct ScatterSpec {
  uint16_t densityPct;
  uint16_t grainPx;
};

inline constexpr ScatterSpec kGlitchScatter[] = {
  {100, 0},
  {80, 6},
  {65, 4},
  {50, 3},
  {40, 2},
  {30, 1},
};
inline constexpr uint16_t kGlitchScatterMax = 5;

// Grain-block plan for a scatter level within `region`. Level 0 fills the
// region solid (grainPx 0, caller special-cases). For 1..5: slotCount =
// region / grainPx, blocksWanted = round(density% of slots) clamped to
// [1, slotCount]. Blocks occupy DISTINCT slots, so realized density is exact
// rather than collision-capped.
struct GlitchPlan {
  uint16_t grainPx;
  uint16_t slotCount;
  uint16_t blocksWanted;
};

inline GlitchPlan glitchBlockPlan(uint16_t scatterLevel, uint16_t region) {
  if (scatterLevel > kGlitchScatterMax) scatterLevel = kGlitchScatterMax;
  const ScatterSpec s = kGlitchScatter[scatterLevel];
  if (s.grainPx == 0 || region == 0) return {s.grainPx, 0, 0};
  const uint16_t slotCount = region / s.grainPx;
  if (slotCount == 0) return {s.grainPx, 0, 0};
  uint16_t blocksWanted = static_cast<uint16_t>(
      (static_cast<uint32_t>(s.densityPct) * slotCount + 50) / 100);
  if (blocksWanted < 1) blocksWanted = 1;
  if (blocksWanted > slotCount) blocksWanted = slotCount;
  return {s.grainPx, slotCount, blocksWanted};
}

// Start-position window for placing a point of `size` within zone `z`.
// clampedSize == 0 only when the zone is empty.
struct ZoneSpan {
  uint16_t clampedSize;
  uint16_t maxStart;
};

inline ZoneSpan zoneSpan(const Zone& z, uint16_t size) {
  const uint16_t regionSize = z.size();
  if (regionSize == 0) return {0, z.posMin};
  const uint16_t clampedSize = size < regionSize ? size : regionSize;
  const uint16_t maxStart =
      static_cast<uint16_t>(z.posMin + (regionSize - clampedSize));
  return {clampedSize, maxStart};
}

// Uniform random start in [posMin, maxStart]; posMin when the span is a single
// slot, guarding rng.range against a zero-width interval. Templated on the rng
// so primitives.hpp stays free of the ESP-only fast_rng.hpp (native tests).
template <class Rng>
inline uint16_t randomStartInZone(const Zone& z, uint16_t size, Rng& rng) {
  const ZoneSpan span = zoneSpan(z, size);
  return (span.maxStart > z.posMin)
      ? static_cast<uint16_t>(rng.range(z.posMin, span.maxStart))
      : z.posMin;
}

}  // namespace lamp
