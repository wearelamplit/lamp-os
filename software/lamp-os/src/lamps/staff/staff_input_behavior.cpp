#include "lamps/staff/staff_input_behavior.hpp"

#include <Arduino.h>

#include "behaviors/configurator.hpp"
#include "core/behavior_context.hpp"
#include "core/hw_config.hpp"
#include "util/color.hpp"
#include "util/gradient.hpp"

namespace staff {

namespace {
constexpr uint16_t kScrubDegPerSec = 90;
constexpr uint32_t kRampPeriodMs = 3000;   // top pad: slow sweep
constexpr uint32_t kPulsePeriodMs = 1200;  // bottom pad: faster pulse
constexpr uint8_t kBrightLo = 20;
constexpr uint8_t kBrightHi = 100;
constexpr uint32_t kShoutCooldownMs = 3000;  // a mash sends one bid, not a flood

uint8_t triangle(uint32_t heldMs, uint32_t periodMs, uint8_t lo, uint8_t hi) {
  const uint32_t half = periodMs / 2;
  const uint32_t phase = heldMs % periodMs;
  const uint32_t up = phase < half ? phase : periodMs - phase;  // 0..half
  return static_cast<uint8_t>(lo + static_cast<uint32_t>(hi - lo) * up / half);
}
}  // namespace

StaffInputBehavior::StaffInputBehavior(lamp::FrameBuffer* fb,
                                       lamp::Config& config,
                                       lamp::Button* stoke,
                                       lamp::Button* shout,
                                       lamp::Touch* topPad,
                                       lamp::Touch* bottomPad)
    : lamp::AnimatedBehavior(fb, 60, false),
      config_(config),
      stoke_(stoke),
      shout_(shout),
      topPad_(topPad),
      bottomPad_(bottomPad),
      configuredBrightness_(config.lamp.brightness) {
  const auto& stops = config_.shade.broadcastColors();
  moodHue_ = stops.empty() ? 0 : lamp::colorToHue(stops.front());

  if (stoke_) {
    stoke_->setCallback([this](lamp::ButtonEvent e) { onStoke(e); });
  }
  if (shout_) {
    shout_->setCallback([this](lamp::ButtonEvent e) { onShout(e); });
  }
  if (topPad_) {
    topPad_->setCallback([this](lamp::TouchEvent e) { onTopPad(e); });
  }
}

void StaffInputBehavior::onShout(lamp::ButtonEvent e) {
  if (e != lamp::ButtonEvent::Click) return;
  auto* ctx = behaviorContext();
  if (!ctx) return;
  const uint32_t now = millis();
  if (lastShoutMs_ != 0 && now - lastShoutMs_ < kShoutCooldownMs) return;
  lastShoutMs_ = now;
  ctx->requestGreeting();
  // ponytail: legacy leg (flip the advertised BLE name so pre-mesh lamps
  // re-greet) deferred pending a bench proof that legacy greeting dedup keys
  // on name, not BLE address. Building the setAdvertisedName setter before
  // that would be dead code if it keys on address.
}

void StaffInputBehavior::onStoke(lamp::ButtonEvent e) {
  auto* ctx = behaviorContext();
  if (!ctx) return;
  switch (e) {
    case lamp::ButtonEvent::Click:
      stoked_ = !stoked_;
      if (stoked_) {
        ctx->setSurfaceBrightness(lamp::Surface::Shade, 100);
        ctx->setSurfaceBrightness(lamp::Surface::Base, 100);
      }
      ctx->setBrightness(stoked_ ? 100 : configuredBrightness_);
      break;
    case lamp::ButtonEvent::DoubleClick:
      moodActive_ = !moodActive_;
      if (moodActive_) ctx->setSolidColor(lamp::colorFromHue(moodHue_));
      else restoreConfiguredColors();
      break;
    case lamp::ButtonEvent::LongPressStart:
      if (unlock_.unlocked()) {
        scrubbing_ = true;
        moodActive_ = true;
        lastScrubMs_ = millis();
      }
      break;
    case lamp::ButtonEvent::LongPressStop:
      if (unlock_.unlocked()) scrubbing_ = false;
      else unlock_.stokeLongReleased(millis());
      break;
  }
}

void StaffInputBehavior::onTopPad(lamp::TouchEvent e) {
  // A top-pad tap is the second step of the unlock combo; it participates
  // only while still locked. Once unlocked, holds drive the brightness ramp
  // (polled in control()); a bare tap does nothing.
  if (e == lamp::TouchEvent::Tap && !unlock_.unlocked()) {
    unlock_.topPadTapped(millis());
  }
}

void StaffInputBehavior::restoreConfiguredColors() {
  auto* ctx = behaviorContext();
  if (!ctx) return;
  if (ctx->shadeConfigurator && ctx->shadeConfigurator->fb) {
    ctx->shadeConfigurator->beginFade(
        lamp::buildGradientWithStops(ctx->shadeConfigurator->fb->pixelCount,
                                     config_.shade.broadcastColors()),
        lamp::kDefaultFadeMs);
    ctx->shadeConfigurator->lastWebSocketUpdateTimeMs = millis();
  }
  if (ctx->baseConfigurator && ctx->baseConfigurator->fb) {
    ctx->baseConfigurator->beginFade(
        lamp::buildGradientWithStops(ctx->baseConfigurator->fb->pixelCount,
                                     config_.base.broadcastColors()),
        lamp::kDefaultFadeMs);
    ctx->baseConfigurator->lastWebSocketUpdateTimeMs = millis();
  }
}

void StaffInputBehavior::control() {
  const uint32_t now = millis();
  unlock_.tick(now);
  if (!unlock_.unlocked()) return;  // touch gestures gated until unlocked

  auto* ctx = behaviorContext();
  if (!ctx) return;

  if (scrubbing_) {
    const uint32_t dt = now - lastScrubMs_;
    lastScrubMs_ = now;
    moodHue_ = static_cast<uint16_t>(
        (moodHue_ + dt * kScrubDegPerSec / 1000) % 360);
    ctx->setSolidColor(lamp::colorFromHue(moodHue_));
  }

  if (topPad_ && topPad_->isHeld()) {
    ctx->setSurfaceBrightness(
        lamp::Surface::Shade,
        triangle(topPad_->heldMs(now), kRampPeriodMs, kBrightLo, kBrightHi));
  }
  if (bottomPad_ && bottomPad_->isHeld()) {
    ctx->setSurfaceBrightness(
        lamp::Surface::Base,
        triangle(bottomPad_->heldMs(now), kPulsePeriodMs, kBrightLo, kBrightHi));
  }
}

}  // namespace staff
