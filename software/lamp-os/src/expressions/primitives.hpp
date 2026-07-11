#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>

#include "expressions/param_utils.hpp"

namespace lamp {

constexpr uint32_t kFrameRateHz = 30;
constexpr uint32_t kMsPerSecond = 1000;

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

// Per-pixel phase spread, 0..100. 0 = synchronized.
struct Scatter {
  uint8_t percent = 0;

  static Scatter fromParameters(const std::map<std::string, uint32_t>& p) {
    uint32_t s = getParam(p, "scatter", 0);
    if (s > 100) s = 100;
    Scatter sc;
    sc.percent = static_cast<uint8_t>(s);
    return sc;
  }
};

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
