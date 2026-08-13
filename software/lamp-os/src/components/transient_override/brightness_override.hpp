#pragma once

// BrightnessOverride: transient brightness override. Single global
// instance that controls the lamp's master brightness. Mesh peers may
// emit a surface byte in OVERRIDE_BRIGHTNESS frames but the receiver
// ignores it; brightness is a master-level value with no per-strip
// concept.
//
// Change-driven callback model: tick() interpolates from→to over
// fadeDurationMs and only calls the registered onChange callback when
// the integer-rounded value actually changes. This preserves the
// existing event-driven brightness path (lamp.cpp's
// applyEffectiveBrightness): no setBrightness() gets pushed into the
// strip every frame, only when the user-visible level moves.
//
// Source ownership identical to ColorOverride: a restore from a
// different sourceKind (and not Any) is a no-op.

#include <cstdint>
#include <functional>

#include "color_override.hpp"  // FadeState enum
#include "components/network/protocol/lamp_protocol.hpp"

namespace lamp {

class BrightnessOverride {
 public:
  // Apply a brightness override. `brightness` is a 0..100 relative factor
  // (range-validated by the parser); effective() applies it as a floored
  // multiplier over the caller's baseline. The change-driven callback fires
  // only when the integer-rounded effective value changes so the strip sees
  // a clean transition.
  void apply(const uint8_t sourceMac[6],
             lamp_protocol::OverrideSource source,
             uint8_t brightness, uint16_t fadeDurationMs);

  // Restore: fade back to the baseline supplied at restore time
  // (effective() takes the current baseline as an arg so the caller
  // can pass whatever home-mode-aware value applies right now). Drops
  // silently on sourceKind mismatch.
  void restore(const uint8_t sourceMac[6],
               lamp_protocol::OverrideSource source,
               uint16_t fadeDurationMs);

  // Drives the state machine + the change-driven callback. Cheap when
  // Idle. nowMs is millis(); baseline is the caller's current baseline
  // (calling site reads effectiveBrightness() each frame so home mode
  // / lamp mode transitions are honored even during a fade).
  void tick(uint32_t nowMs, uint8_t baseline);

  // The floored-multiplier output at nowMs (when active) or `baseline`
  // (when idle). Read-only; doesn't mutate state. Used by the caller's
  // effective-brightness path so a single source-of-truth value flows
  // into the NeoPixel setBrightness call. The factor eases toward its
  // target; the multiplier stacks over the caller's live crowd-dim baseline.
  uint8_t effective(uint32_t nowMs, uint8_t baseline) const;

  // Wire the on-change handler. Called by tick() ONLY when the
  // integer-rounded effective value differs from the last reported
  // value, so a long-duration fade only generates ~N callbacks where
  // N is the perceived brightness step count, not one per frame.
  void setOnChangeCallback(std::function<void()> cb) { onChange_ = std::move(cb); }

  bool isActive() const { return state_ != FadeState::Idle; }
  lamp_protocol::OverrideSource activeSource() const { return activeSource_; }

  bool isWispActive() const {
    return isActive() && activeSource_ == lamp_protocol::OverrideSource::Wisp;
  }

  // Refresh the watchdog on a wisp paint-mode HELLO so a held dim survives
  // between the wisp's sparse re-asserts. No-op unless holding a wisp override.
  void touchApply(uint32_t nowMs);

  // Operator-priority lockout, same semantics as ColorOverride. Set
  // on by the app when the brightness slider is being dragged; off
  // when the user releases it.
  void setOperatorEditing(bool editing) { operatorEditing_ = editing; }
  bool operatorEditing() const { return operatorEditing_; }

  // Silence window before the override auto-restores. A wisp that stops
  // re-asserting brightness for this long releases its dim.
  static constexpr uint32_t kPaintWatchdogMs = 60000;

 private:
  FadeState state_ = FadeState::Idle;

  // Fade endpoints in factor space (0..100). fromBrightness_ is the factor
  // at fade start; toBrightness_ is the target factor. effective() eases
  // between them and applies the floored multiplier against the live baseline.
  uint8_t fromBrightness_ = 0;
  uint8_t toBrightness_ = 0;
  // First-tick latch. Cold apply() marks this false; the first FadingIn
  // tick() seeds fromBrightness_ = 100 (the untouched factor) so the dim
  // eases in rather than snapping. A re-apply on a held override snapshots
  // the prior target factor as the new from, no lazy seed needed.
  bool fromSeeded_ = false;
  uint8_t lastReportedBrightness_ = 0;  // change-detection latch

  uint32_t fadeStartMs_ = 0;
  uint16_t fadeDurationMs_ = 0;

  // Tracks the most recent apply(); drives the watchdog auto-restore.
  uint32_t lastApplyMs_ = 0;
  uint16_t currentFadeDurationMs_ = 0;

  // Restoring-state timing, same shape as the color override.
  uint32_t restoreStartMs_ = 0;
  uint16_t restoreDurationMs_ = 0;

  lamp_protocol::OverrideSource activeSource_ = lamp_protocol::OverrideSource::None;

  std::function<void()> onChange_;

  // Operator-editing lock; see setOperatorEditing() above.
  bool operatorEditing_ = false;
};

}  // namespace lamp
