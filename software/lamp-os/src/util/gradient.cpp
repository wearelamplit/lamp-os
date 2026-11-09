#include "gradient.hpp"

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

uint8_t usableStops(const std::vector<Color>& stops) {
  const auto n = static_cast<uint8_t>(stops.size());
  return n > kMaxStops ? kMaxStops : n;
}

// The one segment filler for open gradients and loops. Segment k blends
// stops[k] toward stops[(k + 1) % count] over whole pixels; the remainder
// pixels go one each to the first segments.
std::vector<Color> fillSegments(uint8_t px, const std::vector<Color>& stops,
                                uint8_t count, uint8_t segments) {
  const uint8_t steps = px / segments;
  const uint8_t remainder = px % segments;
  std::vector<Color> buf(px);
  size_t offset = 0;
  for (uint8_t k = 0; k < segments; k++) {
    const uint8_t len = steps + (k < remainder ? 1 : 0);
    calculateGradientInto(stops[k], stops[(k + 1) % count], buf.data() + offset, len);
    offset += len;
  }
  return buf;
}
}  // namespace

std::vector<Color> calculateGradient(Color inColorStart, Color inColorEnd,
                                     uint8_t inSteps) {
  std::vector<Color> output(inSteps);
  calculateGradientInto(inColorStart, inColorEnd, output.data(), inSteps);
  return output;
};

std::vector<Color> buildGradientWithStops(uint8_t inNumberPixels,
                                          std::vector<Color> inColorStops) {
  const uint8_t numberColors = usableStops(inColorStops);
  if (numberColors < 1) {
    return std::vector<Color>(inNumberPixels, Color());
  }
  if (numberColors == 1) {
    return std::vector<Color>(inNumberPixels, inColorStops[0]);
  }
  return fillSegments(inNumberPixels, inColorStops, numberColors, numberColors - 1);
};

std::vector<Color> buildGradientLoop(uint8_t px, const std::vector<Color>& stops) {
  const uint8_t count = usableStops(stops);
  if (count < 2) {
    return buildGradientWithStops(px, stops);
  }
  return fillSegments(px, stops, count, count);
}
}  // namespace lamp
