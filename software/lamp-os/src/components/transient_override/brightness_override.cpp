#include "brightness_override.hpp"

#include <Arduino.h>

#include <cstring>

#include "util/fade.hpp"

namespace lamp {

// Compute the interpolated brightness at nowMs, assuming the override is
// in its forward (FadingIn → Holding) phase. Separate helper because
// effective() needs to handle FadingIn (interpolating from fromBrightness_
// to toBrightness_) AND Restoring (interpolating back) AND Holding (snap
// to toBrightness_). The math is identical — just different endpoints —
// so we keep it inline at the call sites.
//
// Uses lamp::ease (quadEaseInOut LUT) for the curve so brightness fades
// match the color fade aesthetic. The LUT operates on uint8_t channel
// values; brightness is also uint8_t (0..100 conceptually, but the type
// is 8-bit), so the call is direct.

void BrightnessOverride::apply(const uint8_t sourceMac[6],
                               lamp_protocol::OverrideSource source,
                               uint8_t brightness, uint16_t fadeDurationMs) {
  // Operator-priority lockout: while the brightness slider is being
  // dragged the wisp's overrides lose.
  if (operatorEditing_ &&
      source == lamp_protocol::OverrideSource::Wisp) {
    return;
  }
  // Snapshot the CURRENT effective value as the fade start so a mid-fade
  // re-apply (different source overtaking, or same source re-painting)
  // doesn't snap. effective() already reads the current millis() so we
  // can call it before mutating state.
  //
  // Caller passes the absolute brightness; we'll fade from wherever we
  // are right now (computed via effective on the entry state). Use a
  // sentinel baseline of toBrightness_ when Idle so the fade starts at
  // the no-override value (the caller's true baseline) — but we don't
  // have that here. The override fades from whatever we're already
  // showing, which is fine: at Idle, fromBrightness_ is also the value
  // the callback's last reported, so the fade starts smoothly.
  //
  // (The chain: at Idle, effective() = baseline, but fromBrightness_
  // hasn't been seeded yet. We don't need it to be — we'll seed it from
  // toBrightness_ ↔ baseline when the caller supplies baseline in tick.)
  // The simpler invariant: store the absolute target + start time; let
  // effective() compute from baseline when state == Idle and from
  // toBrightness_ otherwise. fromBrightness_ is computed lazily.

  // For correctness on the FadingIn lerp endpoint we DO need to capture
  // the value at fade-start. Without a baseline arg here, snapshot
  // toBrightness_ (the previous target, valid even on re-apply because
  // the previous Holding value IS toBrightness_). On the cold start
  // (state_ == Idle), seed fromBrightness_ = toBrightness_ = brightness
  // so the snap is instant if the caller asked for fadeDurationMs == 0,
  // and to a flat hold otherwise. Caller's tick() will refresh against
  // baseline next frame.
  if (state_ == FadeState::Idle) {
    // Lazy seed — tick() supplies baseline; mark unseeded so we can pick
    // it up. Without this, fromBrightness_ would equal `brightness` and
    // the fade would be a no-op (from == to).
    fromSeeded_ = false;
  } else {
    fromBrightness_ = toBrightness_;
    fromSeeded_ = true;  // re-apply mid-Holding: prior target IS the from.
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

void BrightnessOverride::restore(const uint8_t sourceMac[6],
                                 lamp_protocol::OverrideSource source,
                                 uint16_t fadeDurationMs) {
  (void)sourceMac;
  if (state_ == FadeState::Idle) return;
  if (source != lamp_protocol::OverrideSource::Any &&
      source != activeSource_) {
    return;
  }
  // Fade from the current override value back to the baseline (which we
  // don't know absolutely here — the caller's tick will pass it in).
  // Snapshot toBrightness_ as the FROM endpoint; the TO endpoint will be
  // injected by the caller via the baseline arg to effective()/tick().
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
        return toBrightness_;
      }
      // If the from endpoint hasn't been seeded yet (cold apply), use
      // the caller's baseline as the from value. tick() is the writer
      // that latches it for change-detection; here we just compute the
      // interpolated value as if it were already seeded.
      const uint8_t from = fromSeeded_ ? fromBrightness_ : baseline;
      return ease(from, toBrightness_, fadeDurationMs_, elapsed);
    }
    case FadeState::Holding:
      return toBrightness_;
    case FadeState::Restoring: {
      const uint32_t elapsed = nowMs - restoreStartMs_;
      if (restoreDurationMs_ == 0 || elapsed >= restoreDurationMs_) {
        return baseline;
      }
      return ease(fromBrightness_, baseline, restoreDurationMs_, elapsed);
    }
  }
  return baseline;  // unreachable; silences strict compilers.
}

void BrightnessOverride::tick(uint32_t nowMs, uint8_t baseline) {
  switch (state_) {
    case FadeState::Idle:
      // Even when idle we still need to fire the callback if the baseline
      // shifted under us (e.g. home-mode toggle). The lastReportedBrightness_
      // tracks the most recently committed value — if it differs from the
      // current baseline AND nothing else (the BLE brightness drain
      // notably) has already committed, the strip is stale. We DON'T
      // proactively fire here though; the BLE drain owns the baseline
      // change and calls applyEffectiveBrightness directly. This branch
      // is intentionally cheap (single load + branch).
      return;
    case FadeState::FadingIn: {
      // Lazy from-seed: first tick after cold apply latches the baseline.
      if (!fromSeeded_) {
        fromBrightness_ = baseline;
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
  // when the integer-rounded value differs from what we last reported.
  const uint8_t current = effective(nowMs, baseline);
  if (current != lastReportedBrightness_) {
    lastReportedBrightness_ = current;
    if (onChange_) onChange_();
  }
}

}  // namespace lamp
