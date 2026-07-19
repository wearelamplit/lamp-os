#pragma once

#include <cstdint>
#include <vector>

#include "util/color.hpp"

namespace lamp {

// Per-pixel gamma-summed full-duty mA for one surface at driver level 255.
// channels is 3 or 4; W counts only on 4-channel strips.
float fullDutyMa(const std::vector<Color>& buffer, uint8_t channels);

// Demand at the requested (pre-clamp) driver level. (requested+1)/256 mirrors
// the NeoPixel setPixelColor brightness scaler; idle strip draw rides on top.
float demandMa(float fullDutySumMa, uint8_t requestedScaled,
               uint16_t totalPixelCount);

// Supply-budget brightness governor. Dormant it holds the ceiling at 255 so
// the min() at the strip funnel is identity; any frame over budget at the
// level about to be written snaps the ceiling to the level that fits before
// that frame reaches the strip. While clamped the ceiling only moves down
// per frame; recovery goes through the paced release in tick. Pure over
// plain values so native tests drive it directly.
class PowerGovernor {
 public:
  enum class State : uint8_t { BootRamp, Dormant, Clamped };

  // Seeds the boot ramp at half ceiling and arms one pendingApply so the
  // hold level reaches the drivers.
  void begin(uint16_t supplyBudgetMa, uint32_t nowMs);

  // Per flushed frame, before any pixel write. fullDutySumMa covers both
  // surfaces; radioBusy covers OTA sessions and BLE clients (the boot window
  // is internal, timer-based). Returns true when the ceiling snapped down on
  // this frame: the caller must re-apply brightness before pushing pixels.
  bool senseFrame(uint32_t nowMs, float fullDutySumMa, uint8_t requestedLevel,
                  uint16_t totalPixelCount, bool radioBusy);

  // Loop cadence. Boot-ramp progression plus the 1 s-paced 0.88-of-budget
  // release, the only path that raises a clamped ceiling.
  void tick(uint32_t nowMs);

  // EMA-glided ceiling; advances the glide, so call with a fresh nowMs.
  uint8_t ceiling(uint32_t nowMs);

  // Pump flag; stays armed while the glide is off target.
  bool consumePendingApply();

  State state() const { return state_; }
  float lastDemandMa() const { return lastDemandMa_; }
  uint32_t lastSenseMs() const { return lastSenseMs_; }
  uint16_t pixelBudgetMa() const { return pixelBudgetMa_; }

 private:
  void glide(uint32_t nowMs);
  float rampCap(uint32_t nowMs) const;
  bool inBootWindow(uint32_t nowMs) const;

  State state_ = State::Dormant;
  uint16_t supplyBudgetMa_ = 0;
  uint16_t pixelBudgetMa_ = 0;
  float target_ = 255.0f;
  float current_ = 255.0f;
  float lastDemandMa_ = 0.0f;
  uint32_t lastSenseMs_ = 0;
  uint32_t beginMs_ = 0;
  uint32_t lastGlideMs_ = 0;
  uint32_t lastPaceMs_ = 0;
  bool pendingApply_ = false;
};

}  // namespace lamp
