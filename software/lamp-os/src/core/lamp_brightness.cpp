// core/lamp_brightness.cpp: brightness computation and application.
// User-write micro-fade, crowd-dim factor, effective brightness, home-mode
// gate, and strip-level application. Owned state: s_userBrightness*,
// s_crowd*. s_hwMaxBrightness is owned by lamp.cpp (set in setup()).
//
// Sibling TU of lamp.cpp; shares file-scope state via core/lamp_internal.hpp.

#include "core/lamp.hpp"
#include "core/lamp_internal.hpp"
#include "core/home_mode_logic.hpp"

#include <Arduino.h>
#include <algorithm>
#include <cstdint>

#include <lampos/clock.hpp>

#include "components/firmware/ota_quiet_mode.hpp"
#include "components/network/ble/ble_control.hpp"
#include "components/network/transport/wifi.hpp"
#include "core/compositor.hpp"
#include "core/crowd_dim_fade.hpp"
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

void retireBrightnessFade(uint32_t nowMs) {
  s_userBrightnessFadeStartMs =
      lampos::retireStamp(nowMs, s_userBrightnessFadeStartMs);
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
// change). The factor is a hard step; CrowdDim ramps between successive
// targets and re-arms only when the target factor changes, so it stays out
// of the OTA pulse multiplier's way.
// Lamp::tick reapplies brightness while crowdDimFading(). Distinct from the
// user-write micro-fade (s_userBrightness*), which BLE writes drive.
static lamp::CrowdDim s_crowdDim;

static float currentCrowdDimFactor(uint32_t nowMs) {
  return s_crowdDim.at(lamp::personalityEngine.crowdDimFactor(),
                       compositor.blocksRemote(lamp::TransientSource::Remote), nowMs);
}

namespace lamp {
bool crowdDimFading(uint32_t nowMs) { return s_crowdDim.fading(nowMs); }
}  // namespace lamp

// Crowd-dim output floor, in percent. Deliberately above the space-dim floor
// (20) — 30% is already very dim, and crowd-dim is a mood nudge, not a shutoff.
static constexpr uint8_t kCrowdDimFloorPct = 30;

uint8_t effectiveBrightness() {
  const uint8_t raw = calculateEffectiveHomeMode() ? config.homeMode.brightness
                                                   : config.lamp.brightness;
  // PersonalityEngine crowd-dim (Introvert / Ambivert + smoothed peer weight).
  // Applied BEFORE the transient brightnessOverride.effective() so paint /
  // wisp overrides keep working over the dimmed baseline. The engine reports
  // a target factor, interpolated between successive targets over
  // kCrowdDimFadeMs so deadband-crossing commits don't snap.
  const float factor = currentCrowdDimFactor(millis());
  return lamp::applyDimFactor(raw, factor, kCrowdDimFloorPct);
}

static uint8_t s_requestedScaled = 0;

// Per-surface brightness trims. 255 = no trim (master brightness verbatim).
// Only ever scale DOWN from the governed level, so the power ceiling holds.
static uint8_t s_shadeFactor = 255;
static uint8_t s_baseFactor  = 255;

namespace lamp {
uint8_t requestedStripLevel() { return s_requestedScaled; }

void setAllStripsBrightness(uint8_t scaledLevel) {
  s_requestedScaled = scaledLevel;
  const uint8_t applied =
      std::min(scaledLevel, s_powerGovernor.ceiling(millis()));
  const uint8_t shadeApplied = scale8(applied, s_shadeFactor);
  const uint8_t baseApplied  = scale8(applied, s_baseFactor);
  for (const auto& seg : shade.segments) if (seg.driver) seg.driver->setBrightness(shadeApplied);
  for (const auto& seg : base.segments)  if (seg.driver) seg.driver->setBrightness(baseApplied);
}

void applyEffectiveBrightness() {
  uint8_t baseline = effectiveBrightness();
  uint8_t level = lamp::overrides.brightness.effective(millis(), baseline);
  setAllStripsBrightness(lamp::calculateBrightnessLevel(s_hwMaxBrightness, level));
}

void setSurfaceFactor(Surface surface, uint8_t factor) {
  (surface == Surface::Shade ? s_shadeFactor : s_baseFactor) = factor;
  applyEffectiveBrightness();
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
// isClientConnected() flips on Core 0, so it is read once per decision.
static lamp::HomeModeState currentHomeModeState() {
  if (ble_control::isClientConnected()) {
    return lamp::homeModeState(true, ble_control::isHomeModePageActive(), false);
  }
  return lamp::homeModeState(false, false,
                             lamp::effectiveHomeModeFromConfig(
                                 config.homeMode.enabled,
                                 config.homeMode.networkBound,
                                 config.homeMode.ssid.empty(),
                                 wifi::homeSsidVisible(config.homeMode.ssid)));
}

// The snapshot reapplyHomeModeState last took, so brightness and the
// compositor's gate never read two different states. Taken on first use for
// the boot path that runs before any reapply.
static lamp::HomeModeState s_homeState;
static bool s_homeStateTaken = false;

bool calculateEffectiveHomeMode() {
  if (!s_homeStateTaken) {
    s_homeState = currentHomeModeState();
    s_homeStateTaken = true;
  }
  return s_homeState.active;
}

// Single funnel for "home mode state may have changed". Keeps the
// compositor's behavior gate and the strip brightness in lockstep so the
// lamp transitions cleanly when preview flips or WiFi associates /
// disassociates.
void reapplyHomeModeState() {
  compositor.setHomeModePolicy(config.homeMode.socialDisabled,
                               config.homeMode.disabledExpressionTypes);
  s_homeState = currentHomeModeState();
  s_homeStateTaken = true;
  compositor.setHomeMode(s_homeState.active);
  compositor.homePreview = s_homeState.preview;
  lamp::applyEffectiveBrightness();
}
