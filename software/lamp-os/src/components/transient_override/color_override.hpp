#pragma once

// ColorOverride — transient color override owned per-surface
// (base / shade). The wisp module drives a lamp's surface through
// this primitive:
//
//   apply()   — paint an override color set, fade-in over fadeDurationMs.
//               Marks state FadingIn → Holding.
//   restore() — fade back to the saved baseline. Marks state Restoring →
//               Idle.
//   tick()    — drives the state machine. Watchdog auto-restores after
//               kPaintWatchdogMs (60s) of no apply() refresh, so a wisp
//               that goes silent doesn't leave a lamp painted forever.
//
// Per-pixel fade lives in ConfiguratorBehavior — apply() builds the
// gradient + calls configurator->beginFade(targetColors, fadeDurationMs).
// The configurator's own draw loop runs the interpolation. Single source
// of truth for fade math; ColorOverride never touches the buffer.
//
// Source ownership: each (apply, restore) carries an OverrideSource
// (currently only Wisp produces overrides). A restore() with a non-
// matching sourceKind is a no-op so future producers can't accidentally
// cancel each other's overrides. sourceKind == Any always succeeds
// (admin / shutdown path).

#include <cstdint>
#include <functional>
#include <vector>

#include "components/network/lamp_protocol.hpp"
#include "util/color.hpp"

namespace lamp {

// Forward declarations to keep this header light.
class ConfiguratorBehavior;
struct BehaviorContext;

enum class FadeState : uint8_t {
  Idle = 0,       // No override active. apply() transitions to FadingIn.
  FadingIn = 1,   // Configurator is fading current → override target.
                  // Transitions to Holding when the fade window elapses.
  Holding = 2,    // Override fully applied; tick() watches the watchdog.
                  // Transitions to Restoring when restore() is called OR
                  // kPaintWatchdogMs (60s) elapses since the last apply().
  Restoring = 3,  // Configurator is fading override → savedColors_.
                  // Transitions to Idle when the restore fade completes.
};

class ColorOverride {
 public:
  // Wire the configurator pointer via the shared BehaviorContext. The
  // surface arg picks base vs shade. pixelCount is read once from the
  // bound configurator's frame buffer at apply() time and cached so we
  // don't re-resolve on every fade.
  void bind(BehaviorContext& ctx, lamp_protocol::OverrideSurface surface);

  // Apply an override. `colors` is the unexpanded list (1..N stops); we
  // expand to pixelCount via buildGradientWithStops before pushing into
  // the configurator. `fadeDurationMs == 0` means snap-in (configurator
  // writes target directly on the next frame). On entry: snapshots the
  // CURRENT buffer state (the live baseline) into savedColors_ so a
  // later restore lands on whatever the user had configured, even if it
  // was a prior BLE write that hasn't been persisted yet.
  void apply(const uint8_t sourceMac[6],
             lamp_protocol::OverrideSource source,
             const Color* colors, uint8_t numColors,
             uint32_t fadeDurationMs);

  // Restore to the saved baseline. Drops silently if the call doesn't
  // own this override (sourceKind mismatch and not Any). When state is
  // Idle this is a no-op — restore-without-prior-apply is benign.
  void restore(const uint8_t sourceMac[6],
               lamp_protocol::OverrideSource source,
               uint32_t fadeDurationMs);

  // Drives the state machine: FadingIn→Holding, Holding→Restoring
  // (watchdog), Restoring→Idle. Cheap when state == Idle. Call from the
  // loop task each iteration.
  void tick(uint32_t nowMs);

  // Update the "what to restore to" baseline mid-Holding. Called from the
  // BLE color drain after a user-driven write so a subsequent restore
  // lands on the new color set instead of the pre-override values. No-op
  // when state == Idle (the BLE write went straight into the configurator,
  // no override to update).
  void rebaseline(const std::vector<Color>& currentSavedColors);

  // Cross-touch the watchdog without running a fade. Wisp paint is sent as
  // a Base+Shade pair 10 ms apart but lands in a single-slot mailbox
  // (PendingTypedSlot, newest writer wins). If Core 1 doesn't drain
  // between the two posts the Shade frame silently drops the Base frame
  // and Base's lastApplyMs_ never advances — after 60 s the watchdog
  // auto-restores Base, expressions un-pause, and the next surviving
  // wisp Base frame snapshots the expression-painted buffer as the new
  // savedColors_, leaving the lamp visibly "stopped listening" to wisp.
  // Cross-touch from the Shade-side drain (and vice versa) is proof of
  // a healthy mesh and keeps both surfaces' watchdogs honest.
  void touchApply(uint32_t nowMs) {
    if (state_ == FadeState::FadingIn || state_ == FadeState::Holding) {
      lastApplyMs_ = nowMs;
    }
  }

  bool isActive() const { return state_ != FadeState::Idle; }
  lamp_protocol::OverrideSource activeSource() const { return activeSource_; }

  // True iff a wisp paint frame is currently shaping this surface — i.e.
  // the override is animating (FadingIn/Holding/Restoring) AND the most
  // recent apply that took effect carried sourceKind=Wisp. The Flutter
  // app reads this through wispStatus to render the will-o'-wisp icon
  // and to grey out expressions that opt into disabledDuringWispOverride.
  bool isWispActive() const {
    return state_ != FadeState::Idle &&
           activeSource_ == lamp_protocol::OverrideSource::Wisp;
  }

  // First stop of the most-recent wisp paint. Cached on apply() when
  // source==Wisp so the app can render it inside the indicator even
  // during a Restoring fade-out. hasLastWispColor() is false until the
  // first wisp paint has landed.
  Color lastWispColor() const { return lastWispColor_; }
  bool hasLastWispColor() const { return hasLastWispColor_; }

  // Edge-triggered callback fired when isWispActive() changes value.
  // Wired in lamp.cpp to ble_control::notifyWispStatus so the
  // app's wispStatus subscription receives a push the moment a surface
  // becomes wisp-controlled or releases.
  using OnWispStateChangeCallback = std::function<void()>;
  void setOnWispStateChangeCallback(OnWispStateChangeCallback cb) {
    wispStateCb_ = std::move(cb);
  }

  // Snap-re-install the most recent override target gradient back into
  // the configurator. Acts on FadingIn + Holding (skips Restoring + Idle).
  // Used by test_expression_complete after the app's payload overwrites
  // `configurator.colors` with the lamp's saved colors — without this
  // call, the wisp paint wouldn't visually return until the wisp's next
  // backstop paint cycle (~10s gap). Snap-in (0ms fade) because the
  // surface is already supposed to be at these colors; we're just
  // restoring what was momentarily stomped.
  void reassertHold();

  // Operator-priority lockout. While set, apply() drops wisp-sourced
  // overrides on the floor. Set on by the app when the colour-picker /
  // brightness-slider for this surface opens; cleared when the picker
  // closes. Bounded entirely by picker lifecycle, no timer.
  void setOperatorEditing(bool editing) { operatorEditing_ = editing; }
  bool operatorEditing() const { return operatorEditing_; }

  // Auto-restore watchdog. If the wisp goes silent the override
  // transitions out so the lamp can't be painted forever. 60s matches
  // the wisp's expected refresh cadence with margin.
  static constexpr uint32_t kPaintWatchdogMs = 60000;

 private:
  ConfiguratorBehavior* configurator_ = nullptr;
  lamp_protocol::OverrideSurface surface_ = lamp_protocol::OverrideSurface::Base;
  uint8_t pixelCount_ = 0;

  FadeState state_ = FadeState::Idle;
  lamp_protocol::OverrideSource activeSource_ = lamp_protocol::OverrideSource::None;

  // Timestamp of the last apply() — drives both the FadingIn→Holding
  // transition (when elapsed >= currentFadeDurationMs_) and the
  // Holding→Restoring watchdog (when elapsed >= kPaintWatchdogMs).
  uint32_t lastApplyMs_ = 0;
  uint32_t currentFadeDurationMs_ = 0;

  // Timestamp of the restore() — drives the Restoring→Idle transition.
  uint32_t restoreStartMs_ = 0;
  uint32_t restoreDurationMs_ = 0;

  // The baseline we'll restore to. Snapshotted from the configurator's
  // `colors` at apply() time, and replaced by rebaseline() during Holding.
  std::vector<Color> savedColors_;

  // Most recent override target gradient (per-pixel). Captured in
  // apply() after expanding stops. Used by reassertHold() to put the
  // wisp paint back into `configurator.colors` after a test_expression
  // round-trip overwrote it.
  std::vector<Color> targetGradient_;

  // Operator-editing lock — see setOperatorEditing() above.
  bool operatorEditing_ = false;

  // Wisp paint state — set in apply() when source==Wisp.
  Color lastWispColor_;
  bool  hasLastWispColor_ = false;

  // isWispActive() value at the last callback fire. Used for edge-
  // triggering so we don't spam BLE notifies on every paint.
  bool wasWispActive_ = false;

  OnWispStateChangeCallback wispStateCb_;

  // Edge-detect helper: call after any mutation to state_ / activeSource_.
  void maybeNotifyWispStateChange() {
    const bool nowActive = isWispActive();
    if (nowActive != wasWispActive_) {
      wasWispActive_ = nowActive;
      if (wispStateCb_) wispStateCb_();
    }
  }
};

}  // namespace lamp
