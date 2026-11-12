#include "color_rotation.hpp"

#include <algorithm>

#include <lampos/clock.hpp>

#include "fade.hpp"
#include "gradient.hpp"

namespace lamp {

RoleScene buildRoleScene(uint8_t px, const std::vector<Color>& stops,
                         const ColorRotation& rotation) {
  RoleScene scene{{}, rotation};
  if (rotation.on && gradientStopCount(stops) >= 2) {
    scene.colors = buildGradientLoop(px, stops);
  } else {
    scene.colors = buildGradientWithStops(px, stops);
    scene.rotation.on = false;
  }
  return scene;
}

void RotationClock::advance(uint32_t nowMs, uint8_t periodS, bool towardZero) {
  const uint32_t elapsed = lampos::frameDeltaMs(nowMs, lastMs_);
  lastMs_ = nowMs;
  // Whole phase units per ms keep the position a function of summed time
  // alone, whatever the frame spacing.
  const uint32_t perMs = static_cast<uint32_t>(
      (uint64_t{1} << 32) / (uint32_t{clampRotationPeriodS(periodS)} * 1000u));
  const uint32_t step = elapsed * perMs;
  phase_ = towardZero ? phase_ + step : phase_ - step;
}

uint32_t loopOffset(uint32_t phase, size_t len) {
  return static_cast<uint32_t>((static_cast<uint64_t>(phase) * len) >> 24);
}

Color sampleLoop(const std::vector<Color>& loop, uint32_t offset, size_t i) {
  const size_t len = loop.size();
  const size_t a = (i + (offset >> 8)) % len;
  const size_t b = a + 1 == len ? 0 : a + 1;
  // mixColorLinear's full scale is 256 << 10.
  return mixColorLinear(loop[a], loop[b], (offset & 0xFFu) << 10);
}

}  // namespace lamp
