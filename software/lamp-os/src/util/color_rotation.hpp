#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "color.hpp"

namespace lamp {

// Physical direction on the surface; the configurator maps it to buffer order.
enum class RotationDir : uint8_t { Up, Down };

constexpr uint8_t kRotationMinPeriodS = 5;
constexpr uint8_t kRotationMaxPeriodS = 120;
constexpr uint8_t kRotationDefaultPeriodS = 40;

// The most one clock step covers. Smaller turns a busy loop into slow motion;
// larger lets a stall jump the pattern on resume.
constexpr uint32_t kMaxRotationStepMs = 100;

// A role's colours flowing round a closed loop, one loop every periodS seconds.
struct ColorRotation {
  bool on = false;
  uint8_t periodS = kRotationDefaultPeriodS;
  RotationDir dir = RotationDir::Up;
};

constexpr uint8_t clampRotationPeriodS(int32_t s) {
  return s < kRotationMinPeriodS   ? kRotationMinPeriodS
         : s > kRotationMaxPeriodS ? kRotationMaxPeriodS
                                   : static_cast<uint8_t>(s);
}

// A role's colours as the configurator draws them.
struct RoleScene {
  std::vector<Color> colors;
  ColorRotation rotation;
};

// The one choice between an open gradient and a loop: rotation on with a
// gradientStopCount of 2 or more builds the loop and keeps `on`; anything
// else builds the open gradient with `on` cleared.
RoleScene buildRoleScene(uint8_t px, const std::vector<Color>& stops,
                         const ColorRotation& rotation);

// Position round a loop, where 2^32 is one loop.
class RotationClock {
 public:
  // Steps by the time since the last advance, capped at kMaxRotationStepMs.
  // `towardZero` moves the colours toward buffer index 0.
  void advance(uint32_t nowMs, uint8_t periodS, bool towardZero);
  // The settled position.
  void reset() { phase_ = 0; }
  uint32_t phase() const { return phase_; }

 private:
  uint32_t phase_ = 0;
  uint32_t lastMs_ = 0;
};

// How far `phase` has moved a loop of `len` pixels, in 1/256 px.
uint32_t loopOffset(uint32_t phase, size_t len);

// Pixel `i` of `loop` shifted by `offset` (from loopOffset): the two pattern
// pixels it falls between, blended linearly. `loop` must be non-empty.
Color sampleLoop(const std::vector<Color>& loop, uint32_t offset, size_t i);

}  // namespace lamp
