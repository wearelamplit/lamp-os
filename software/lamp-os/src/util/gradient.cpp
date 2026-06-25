#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "color.hpp"
#include "fade.hpp"

namespace lamp {
namespace {
// Maximum number of color stops we'll honor in buildGradientWithStops. The UI
// caps user-selectable stops at 5; we pick a slightly generous ceiling so the
// `breaks` array can live on the stack. Anything beyond this is silently
// truncated to the first kMaxStops colors.
constexpr uint8_t kMaxStops = 8;

// In-place gradient writer: writes `steps` interpolated colors into `dst`.
// No allocation; the caller owns the buffer.
void calculateGradientInto(Color inColorStart, Color inColorEnd, Color* dst,
                           uint8_t steps) {
  for (uint8_t i = 0; i < steps; i++) {
    dst[i] = fadeLinear(inColorStart, inColorEnd, steps, i);
  }
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
  uint8_t numberColors = inColorStops.size();

  // input color stops are empty
  if (numberColors < 1) {
    return std::vector<Color>{inNumberPixels, Color()};
  }

  // single color - return a uniform pixel buffer
  if (numberColors == 1) {
    return std::vector<Color>{inNumberPixels, inColorStops[0]};
  }

  // Clamp to our compile-time ceiling so `breaks` can live on the stack.
  if (numberColors > kMaxStops) {
    numberColors = kMaxStops;
  }

  // two colors - return a single gradient (allocate exactly once via resize)
  if (numberColors == 2) {
    std::vector<Color> buf(inNumberPixels);
    calculateGradientInto(inColorStops[0], inColorStops[1], buf.data(),
                          inNumberPixels);
    return buf;
  }

  // multiple colors - use integer math to calculate an even fit for all the
  // stops
  uint8_t steps = floor(inNumberPixels / (numberColors - 1));
  uint8_t remainder = inNumberPixels % (numberColors - 1);

  // Stack-allocated breakpoints: bounded by kMaxStops - 1.
  std::array<uint8_t, kMaxStops - 1> breaks{};
  const uint8_t numBreaks = numberColors - 1;
  for (uint8_t i = 0; i < numBreaks; i++) {
    breaks[i] = steps;
  }

  if (remainder != 0) {
    for (uint8_t i = 0; i < numBreaks; i++) {
      breaks[i] = breaks[i] + 1;

      remainder--;

      if (remainder == 0) {
        break;
      }
    }
  }

  // with all the breakpoints identified, build the gradients in place — no
  // per-stop allocation, one resize on `buf`.
  std::vector<Color> buf;
  buf.resize(inNumberPixels);
  size_t offset = 0;
  for (uint8_t i = 0; i < numBreaks; i++) {
    calculateGradientInto(inColorStops[i], inColorStops[i + 1],
                          buf.data() + offset, breaks[i]);
    offset += breaks[i];
  }

  return buf;
};
}  // namespace lamp
