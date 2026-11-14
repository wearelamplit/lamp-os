#include "gradient.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "color.hpp"
#include "fade.hpp"

namespace lamp {
namespace {
// Stops past this are cut, from open gradients and loops alike.
constexpr uint8_t kMaxStops = 8;

// In-place gradient writer: writes `steps` interpolated colors into `dst`.
// No allocation; the caller owns the buffer.
void calculateGradientInto(Color inColorStart, Color inColorEnd, Color* dst,
                           uint8_t steps) {
  for (uint8_t i = 0; i < steps; i++) {
    dst[i] = fadeLinear(inColorStart, inColorEnd, steps, i);
  }
}

// The one segment filler for open gradients and loops. Segment k blends
// stops[k] toward stops[(k + 1) % count] over whole pixels. The remainder
// pixels go one each to the first segments.
void fillSegments(Color* dst, uint8_t px, const std::vector<Color>& stops,
                  uint8_t count, uint8_t segments) {
  const uint8_t steps = px / segments;
  const uint8_t remainder = px % segments;
  size_t offset = 0;
  for (uint8_t k = 0; k < segments; k++) {
    const uint8_t len = steps + (k < remainder ? 1 : 0);
    calculateGradientInto(stops[k], stops[(k + 1) % count], dst + offset, len);
    offset += len;
  }
}
}  // namespace

uint8_t gradientStopCount(const std::vector<Color>& stops) {
  const auto n = static_cast<uint8_t>(stops.size());
  return n > kMaxStops ? kMaxStops : n;
}

std::vector<Color> calculateGradient(Color inColorStart, Color inColorEnd,
                                     uint8_t inSteps) {
  std::vector<Color> output(inSteps);
  calculateGradientInto(inColorStart, inColorEnd, output.data(), inSteps);
  return output;
};

void buildGradientWithStopsInto(uint8_t px, const std::vector<Color>& stops,
                                Color* dst) {
  const uint8_t count = gradientStopCount(stops);
  if (count < 2) {
    std::fill(dst, dst + px, count == 1 ? stops[0] : Color());
    return;
  }
  fillSegments(dst, px, stops, count, count - 1);
}

std::vector<Color> buildGradientWithStops(uint8_t inNumberPixels,
                                          const std::vector<Color>& inColorStops) {
  std::vector<Color> output(inNumberPixels);
  buildGradientWithStopsInto(inNumberPixels, inColorStops, output.data());
  return output;
}

std::vector<Color> buildGradientLoop(uint8_t px, const std::vector<Color>& stops) {
  const uint8_t count = gradientStopCount(stops);
  std::vector<Color> output(px);
  if (count < 2) {
    buildGradientWithStopsInto(px, stops, output.data());
  } else {
    fillSegments(output.data(), px, stops, count, count);
  }
  return output;
}
}  // namespace lamp
