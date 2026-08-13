#include "brightness_override.hpp"

#include <Arduino.h>

#include <cstring>

#include "util/fade.hpp"
#include "util/levels.hpp"

namespace lamp {

// Anti-shutoff floor for the space dim, in output-percent. A factor of 0
// still leaves a lamp at 20% so a forged frame can't black out a space.
static constexpr uint8_t kSpaceDimFloor = 20;

// fromBrightness_ / toBrightness_ hold the fade endpoints in FACTOR space
// (0..100). effective() eases the factor over fadeDurationMs and applies
// applyDimFactor against the live baseline each read, so a mid-fade
// baseline shift (crowd-dim, home mode) composes cleanly. lamp::ease
// (quadEaseInOut LUT) drives the curve to match the color fade aesthetic.

void BrightnessOverride::apply(const uint8_t sourceMac[6],
                               lamp_protocol::OverrideSource source,
                               uint8_t brightness, uint16_t fadeDurationMs) {
  // Operator-priority lockout: while the brightness slider is being
  // dragged the wisp's overrides lose.
  if (operatorEditing_ &&
      source == lamp_protocol::OverrideSource::Wisp) {
    return;
  }
  // Cold apply eases from the untouched factor (100), seeded lazily on the
  // first tick. A re-apply mid-Holding eases from the prior target factor so
  // an overtaking source glides instead of snapping. `brightness` is a factor.
  if (state_ == FadeState::Idle) {
    fromSeeded_ = false;
  } else {
    fromBrightness_ = toBrightness_;
    fromSeeded_ = true;
  }
  toBrightness_ = brightness;

  state_ = FadeState::FadingIn;
  activeSource_ = source;
  (void)sourceMac;  // not used post-apply; ownership check is by source kind, not MAC
  fadeStartMs_ = ::millis();
  fadeDurationMs_ = fadeDurationMs;
  lastApplyMs_ = fadeStartMs_;
  currentFadeDurationMs_ = fadeDurationMs;
}

void BrightnessOverride::touchApply(uint32_t nowMs) {
  if (isWispActive() &&
      (state_ == FadeState::FadingIn || state_ == FadeState::Holding)) {
    lastApplyMs_ = nowMs;
  }
}

void BrightnessOverride::restore(const uint8_t sourceMac[6],
                                 lamp_protocol::OverrideSource source,
                                 uint16_t fadeDurationMs) {
  (void)sourceMac;
  if (state_ == FadeState::Idle) return;
  if (source != lamp_protocol::OverrideSource::Any &&
      source != activeSource_) {
    return;
  }
  // Ease the factor from the current target back to 100 (untouched).
  fromBrightness_ = toBrightness_;
  state_ = FadeState::Restoring;
  restoreStartMs_ = ::millis();
  restoreDurationMs_ = fadeDurationMs;
}

uint8_t BrightnessOverride::effective(uint32_t nowMs, uint8_t baseline) const {
  switch (state_) {
    case FadeState::Idle:
      return baseline;
    case FadeState::FadingIn: {
      const uint32_t elapsed = nowMs - fadeStartMs_;
      if (fadeDurationMs_ == 0 || elapsed >= fadeDurationMs_) {
        return applyDimFactor(baseline, toBrightness_ / 100.0f, kSpaceDimFloor);
      }
      // Cold apply eases from the untouched factor (100); tick() latches it.
      const uint8_t fromFactor = fromSeeded_ ? fromBrightness_ : 100;
      const uint8_t factor =
          ease(fromFactor, toBrightness_, fadeDurationMs_, elapsed);
      return applyDimFactor(baseline, factor / 100.0f, kSpaceDimFloor);
    }
    case FadeState::Holding:
      return applyDimFactor(baseline, toBrightness_ / 100.0f, kSpaceDimFloor);
    case FadeState::Restoring: {
      const uint32_t elapsed = nowMs - restoreStartMs_;
      if (restoreDurationMs_ == 0 || elapsed >= restoreDurationMs_) {
        return baseline;
      }
      const uint8_t factor =
          ease(fromBrightness_, 100, restoreDurationMs_, elapsed);
      return applyDimFactor(baseline, factor / 100.0f, kSpaceDimFloor);
    }
  }
  return baseline;  // unreachable; silences strict compilers.
}

void BrightnessOverride::tick(uint32_t nowMs, uint8_t baseline) {
  switch (state_) {
    case FadeState::Idle:
      // The callback must still fire when idle if the baseline shifted
      // underneath (e.g. home-mode toggle). lastReportedBrightness_
      // tracks the most recently committed value; if it differs from the
      // current baseline AND nothing else (the BLE brightness drain
      // notably) has already committed, the strip is stale. This branch
      // does not proactively fire; the BLE drain owns the baseline
      // change and calls applyEffectiveBrightness directly. This branch
      // is intentionally cheap (single load + branch).
      return;
    case FadeState::FadingIn: {
      // Lazy from-seed: cold apply eases up from the untouched factor.
      if (!fromSeeded_) {
        fromBrightness_ = 100;
        fromSeeded_ = true;
      }
      const uint32_t elapsed = nowMs - fadeStartMs_;
      if (elapsed >= fadeDurationMs_) {
        state_ = FadeState::Holding;
      }
      // Fall through: emit a callback if the rounded value moved.
      break;
    }
    case FadeState::Holding: {
      const uint32_t elapsed = nowMs - lastApplyMs_;
      if (elapsed >= kPaintWatchdogMs) {
        // Auto-restore. Pass Any to bypass the source-ownership check.
        // restore() does (void)sourceMac internally; pass a zero MAC.
        static const uint8_t kZeroMac[6] = {0};
        restore(kZeroMac, lamp_protocol::OverrideSource::Any,
                /*fadeDurationMs=*/currentFadeDurationMs_);
      }
      break;
    }
    case FadeState::Restoring: {
      const uint32_t elapsed = nowMs - restoreStartMs_;
      if (elapsed >= restoreDurationMs_) {
        state_ = FadeState::Idle;
        activeSource_ = lamp_protocol::OverrideSource::None;
      }
      break;
    }
  }

  // Change-driven callback. Recompute effective and only fire onChange
  // when the integer-rounded value differs from the last reported value.
  const uint8_t current = effective(nowMs, baseline);
  if (current != lastReportedBrightness_) {
    lastReportedBrightness_ = current;
    if (onChange_) onChange_();
  }
}

}  // namespace lamp
