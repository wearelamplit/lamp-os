// core/lamp_brightness.cpp: brightness computation and application.
// User-write micro-fade, crowd-dim factor, effective brightness, home-mode
// gate, and strip-level application. Owned state: s_userBrightness*,
// s_crowd*. s_hwMaxBrightness is owned by lamp.cpp (set in setup()).
//
// Sibling TU of lamp.cpp; shares file-scope state via core/lamp_internal.hpp.

#include "core/lamp.hpp"
#include "core/lamp_internal.hpp"

#include <Arduino.h>
#include <cstdint>

#include "components/firmware/ota_quiet_mode.hpp"
#include "components/network/ble/ble_control.hpp"
#include "components/network/transport/wifi.hpp"
#include "core/compositor.hpp"
#include "core/frame_buffer.hpp"
#include "core/override_aggregate.hpp"
#include "core/personality_engine.hpp"
#include "util/levels.hpp"

// User brightness micro-fade. A sustained slider drag delivers a write every
// ~60-100 ms (WriteCoalescer window); applying each directly reads as visibly
// stepped brightness. This state interpolates between successive writes over
// kUserBrightnessFadeMs (~80 ms, slightly longer than the coalescer floor so
// each step bleeds into the next without overshooting). Drain stamps a new
// (source, target, start) triple per write; the loop tick interpolates and
// applies. Gated to brightnessOverride.isActive() == false so an active wisp
// override owns the strip uncontested via the applyEffectiveBrightness
// change-callback path.
//
// Source init: drain reads the current visual level by lerping the in-flight
// fade, so an immediate re-write picks up where the strip actually is.
// Cold-start (no prior write) seeds source = target so the first write snaps
// without a black-up ramp.
static constexpr uint16_t kUserBrightnessFadeMs = 80;
static uint8_t  s_userBrightnessSource = 0;
static uint8_t  s_userBrightnessTarget = 0;
static uint32_t s_userBrightnessFadeStartMs = 0;
static bool     s_userBrightnessSeeded = false;

namespace lamp {
uint8_t computeUserBrightnessNow(uint32_t nowMs) {
  if (s_userBrightnessSource == s_userBrightnessTarget) {
    return s_userBrightnessTarget;
  }
  const uint32_t elapsed = nowMs - s_userBrightnessFadeStartMs;
  if (elapsed >= kUserBrightnessFadeMs) {
    return s_userBrightnessTarget;
  }
  const int32_t span =
      static_cast<int32_t>(s_userBrightnessTarget) - s_userBrightnessSource;
  return static_cast<uint8_t>(
      s_userBrightnessSource +
      (span * static_cast<int32_t>(elapsed)) /
          static_cast<int32_t>(kUserBrightnessFadeMs));
}

uint8_t  brightnessFadeSource()    { return s_userBrightnessSource; }
uint8_t  brightnessFadeTarget()    { return s_userBrightnessTarget; }
uint32_t brightnessFadeStartMs()   { return s_userBrightnessFadeStartMs; }
bool     brightnessFadeSeeded()    { return s_userBrightnessSeeded; }

void setBrightnessFade(uint8_t source, uint8_t target, uint32_t startMs) {
  s_userBrightnessSource      = source;
  s_userBrightnessTarget      = target;
  s_userBrightnessFadeStartMs = startMs;
  s_userBrightnessSeeded      = true;
}

void clearBrightnessFadeSeed() {
  s_userBrightnessSeeded = false;
}

// Marks the fade as settled: source==target, seeded stays true so the next
// slider write sources from the settled level and micro-fades rather than
// snapping from the new target.
void settleBrightnessFade() {
  s_userBrightnessSource = s_userBrightnessTarget;
}

}  // namespace lamp

// Crowd-dim micro-fade. PersonalityEngine commits a new crowdDimFactor()
// when its smoothed weighted-peer count crosses the deadband (>= 2/100
// change). The factor is a hard step: without interpolation a deadband
// crossing snaps brightness by >= 2 levels in one frame, visible at low
// overall brightness. This triple interpolates between successive engine
// targets over kCrowdDimFadeMs so the transition reads smooth.
//
// Distinct from the user-write triple (s_userBrightness*): that path only
// runs while no transient override is active and is driven by BLE writes.
// Crowd-dim fades run every frame inside effectiveBrightness() and stay out
// of the OTA pulse multiplier's way by only re-arming when the engine's
// TARGET factor changes (not when raw brightness changes).
static constexpr uint16_t kCrowdDimFadeMs = 80;
static float    s_crowdAppliedFactor = 1.0f;
static float    s_crowdTargetFactor  = 1.0f;
static uint32_t s_crowdFadeStartMs   = 0;
static bool     s_crowdFadeActive    = false;

// Current interpolated crowd-dim factor. Detects target changes against the
// engine and (re)starts a fade from the current value, so a mid-fade
// retrigger glides instead of snapping.
static float currentCrowdDimFactor(uint32_t nowMs) {
  const float engineTarget = lamp::personalityEngine.crowdDimFactor();
  if (engineTarget != s_crowdTargetFactor) {
    float startFrom = s_crowdAppliedFactor;
    if (s_crowdFadeActive) {
      const uint32_t elapsed = nowMs - s_crowdFadeStartMs;
      if (elapsed >= kCrowdDimFadeMs) {
        startFrom = s_crowdTargetFactor;
      } else {
        const float span = s_crowdTargetFactor - s_crowdAppliedFactor;
        startFrom = s_crowdAppliedFactor +
                    span * (static_cast<float>(elapsed) /
                            static_cast<float>(kCrowdDimFadeMs));
      }
    }
    s_crowdAppliedFactor = startFrom;
    s_crowdTargetFactor  = engineTarget;
    s_crowdFadeStartMs   = nowMs;
    s_crowdFadeActive    = true;
  }
  if (!s_crowdFadeActive) return s_crowdTargetFactor;
  const uint32_t elapsed = nowMs - s_crowdFadeStartMs;
  if (elapsed >= kCrowdDimFadeMs) {
    s_crowdAppliedFactor = s_crowdTargetFactor;
    s_crowdFadeActive = false;
    return s_crowdTargetFactor;
  }
  const float span = s_crowdTargetFactor - s_crowdAppliedFactor;
  return s_crowdAppliedFactor +
         span * (static_cast<float>(elapsed) /
                 static_cast<float>(kCrowdDimFadeMs));
}

uint8_t effectiveBrightness() {
  const uint8_t raw = calculateEffectiveHomeMode() ? config.homeMode.brightness
                                                   : config.lamp.brightness;
  // PersonalityEngine crowd-dim (Introvert / Ambivert + smoothed peer weight).
  // Applied BEFORE the transient brightnessOverride.effective() so paint /
  // wisp overrides keep working over the dimmed baseline. The engine reports
  // a target factor, interpolated between successive targets over
  // kCrowdDimFadeMs so deadband-crossing commits don't snap.
  const float factor = currentCrowdDimFactor(millis());
  uint8_t afterCrowd;
  if (factor >= 0.999f) {
    afterCrowd = raw;
  } else {
    const float scaled = static_cast<float>(raw) * factor;
    afterCrowd = static_cast<uint8_t>(scaled + 0.5f);
    if (afterCrowd < 1) afterCrowd = 1;  // never blank from personality alone
  }
  return afterCrowd;
}

namespace lamp {
void setAllStripsBrightness(uint8_t scaledLevel) {
  for (const auto& seg : shade.segments) if (seg.driver) seg.driver->setBrightness(scaledLevel);
  for (const auto& seg : base.segments)  if (seg.driver) seg.driver->setBrightness(scaledLevel);
}

void applyEffectiveBrightness() {
  uint8_t baseline = effectiveBrightness();
  uint8_t level = lamp::overrides.brightness.effective(millis(), baseline);
  setAllStripsBrightness(lamp::calculateBrightnessLevel(s_hwMaxBrightness, level));
}
}  // namespace lamp

// Two regimes:
//   1. BT client connected (the app is the "configurator"): home mode is
//      forced OFF unless the user is on the Home Mode page, in which case
//      it's forced ON so they can preview brightness / behavior changes.
//      The flag is set by the app via CHAR_HOME_MODE_FOCUS and cleared on
//      BT disconnect.
//   2. No BT client connected: presence-based. Home mode iff the user
//      has enabled it AND has a saved SSID AND the most recent wifi scan
//      saw that SSID nearby. The lamp never associates, just sniffs
//      beacons. No password ever leaves the lamp.
bool calculateEffectiveHomeMode() {
  if (ble_control::isClientConnected()) {
    return ble_control::isHomeModePageActive();
  }
  return config.homeMode.enabled
      && !config.homeMode.ssid.empty()
      && wifi::homeSsidVisible(config.homeMode.ssid);
}

// Single funnel for "home mode state may have changed". Keeps the
// compositor's behavior gate and the strip brightness in lockstep so the
// lamp transitions cleanly when preview flips or WiFi associates /
// disassociates.
void reapplyHomeModeState() {
  compositor.setHomeMode(calculateEffectiveHomeMode());
  lamp::applyEffectiveBrightness();
}
