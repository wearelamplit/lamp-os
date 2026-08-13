#include "core/power_governor.hpp"

#include <cmath>

#include <Adafruit_NeoPixel.h>
#include <lampos/led_power.hpp>

namespace lamp {

float fullDutyMa(const std::vector<Color>& buffer, uint8_t channels) {
  float sum = 0.0f;
  for (const auto& c : buffer) {
    sum += Adafruit_NeoPixel::gamma8(c.r) + Adafruit_NeoPixel::gamma8(c.g) +
           Adafruit_NeoPixel::gamma8(c.b);
    if (channels == 4) sum += Adafruit_NeoPixel::gamma8(c.w);
  }
  return sum * (lampos::led::kMaPerChannelFullDuty / 255.0f);
}

float demandMa(float fullDutySumMa, uint8_t requestedScaled,
               uint16_t totalPixelCount) {
  return fullDutySumMa * (static_cast<float>(requestedScaled) + 1.0f) / 256.0f +
         lampos::led::kIdleMaPerPixel * static_cast<float>(totalPixelCount);
}

namespace {

// Reserve covers board + radio draw only; the strips' idle draw is demand.
constexpr uint16_t kReserveQuietMa = 200;
constexpr uint16_t kReserveRadioHotMa = 400;
constexpr uint16_t kPaceMs = 1000;
constexpr float kReleaseFraction = 0.88f;
constexpr uint16_t kGlideMs = 400;
constexpr uint16_t kBootHoldMs = 5000;
constexpr uint16_t kBootGlideMs = 5000;
constexpr float kFullCeiling = 255.0f;
constexpr float kBootCeiling = 128.0f;

}  // namespace

void PowerGovernor::begin(uint16_t supplyBudgetMa, uint32_t nowMs) {
  supplyBudgetMa_ = supplyBudgetMa;
  pixelBudgetMa_ = supplyBudgetMa > kReserveRadioHotMa
                       ? supplyBudgetMa - kReserveRadioHotMa
                       : 0;
  state_ = State::BootRamp;
  beginMs_ = nowMs;
  lastGlideMs_ = nowMs;
  lastPaceMs_ = nowMs;
  current_ = kBootCeiling;
  target_ = kBootCeiling;
  pendingApply_ = true;
}

bool PowerGovernor::inBootWindow(uint32_t nowMs) const {
  return nowMs - beginMs_ < static_cast<uint32_t>(kBootHoldMs) + kBootGlideMs;
}

float PowerGovernor::rampCap(uint32_t nowMs) const {
  const uint32_t t = nowMs - beginMs_;
  if (t < kBootHoldMs) return kBootCeiling;
  if (t >= static_cast<uint32_t>(kBootHoldMs) + kBootGlideMs) return kFullCeiling;
  return kBootCeiling + (kFullCeiling - kBootCeiling) *
                            (static_cast<float>(t - kBootHoldMs) / kBootGlideMs);
}

bool PowerGovernor::senseFrame(uint32_t nowMs, float fullDutySumMa,
                               uint8_t requestedLevel,
                               uint16_t totalPixelCount, bool radioBusy) {
  glide(nowMs);
  const bool bootWindow = inBootWindow(nowMs);
  const uint16_t reserve =
      (radioBusy || bootWindow) ? kReserveRadioHotMa : kReserveQuietMa;
  pixelBudgetMa_ = supplyBudgetMa_ > reserve ? supplyBudgetMa_ - reserve : 0;
  lastDemandMa_ = demandMa(fullDutySumMa, requestedLevel, totalPixelCount);
  lastSenseMs_ = nowMs;
  const float budget = static_cast<float>(pixelBudgetMa_);

  // Judge the level this frame will actually be written at, so an
  // already-clamped ceiling doesn't re-fire every frame.
  const uint8_t ceilingLevel = static_cast<uint8_t>(current_ + 0.5f);
  const uint8_t appliedLevel =
      requestedLevel < ceilingLevel ? requestedLevel : ceilingLevel;
  if (demandMa(fullDutySumMa, appliedLevel, totalPixelCount) <= budget) {
    return false;
  }

  // Solve demandMa(sum, fit, px) == budget for the level; floor it so the
  // rounded ceiling can't tip demand back over budget and churn.
  const float idle =
      lampos::led::kIdleMaPerPixel * static_cast<float>(totalPixelCount);
  float fit = fullDutySumMa > 0.0f
                  ? std::floor(256.0f * (budget - idle) / fullDutySumMa - 1.0f)
                  : 1.0f;
  if (fit < 1.0f) fit = 1.0f;
  const float cap = bootWindow ? rampCap(nowMs) : kFullCeiling;
  if (fit > cap) fit = cap;
  state_ = State::Clamped;
  if (fit >= current_) return false;
  current_ = fit;
  target_ = fit;
  pendingApply_ = true;
  return true;
}

void PowerGovernor::tick(uint32_t nowMs) {
  glide(nowMs);
  if (nowMs - lastPaceMs_ < kPaceMs) return;
  lastPaceMs_ = nowMs;
  const bool bootWindow = inBootWindow(nowMs);
  if (state_ == State::Clamped) {
    if (lastDemandMa_ >
        kReleaseFraction * static_cast<float>(pixelBudgetMa_)) {
      return;
    }
    state_ = bootWindow ? State::BootRamp : State::Dormant;
  } else if (state_ == State::BootRamp && !bootWindow) {
    state_ = State::Dormant;
  }
  const float newTarget =
      state_ == State::BootRamp ? rampCap(nowMs) : kFullCeiling;
  if (newTarget != target_) {
    target_ = newTarget;
    pendingApply_ = true;
  }
}

void PowerGovernor::glide(uint32_t nowMs) {
  const uint32_t dt = nowMs - lastGlideMs_;
  lastGlideMs_ = nowMs;
  if (current_ == target_ || dt == 0) return;
  const float step =
      dt >= kGlideMs ? 1.0f : static_cast<float>(dt) / kGlideMs;
  current_ += (target_ - current_) * step;
  if (current_ - target_ < 0.5f && target_ - current_ < 0.5f) {
    current_ = target_;
  }
  pendingApply_ = true;
}

uint8_t PowerGovernor::ceiling(uint32_t nowMs) {
  glide(nowMs);
  return static_cast<uint8_t>(current_ + 0.5f);
}

bool PowerGovernor::consumePendingApply() {
  const bool was = pendingApply_;
  pendingApply_ = false;
  return was;
}

}  // namespace lamp
