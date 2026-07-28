#include "color_override.hpp"

#include <cstring>

#include "behaviors/configurator.hpp"
#include "core/behavior_context.hpp"
#include "core/frame_buffer.hpp"
#include "util/gradient.hpp"

namespace lamp {

void ColorOverride::bind(BehaviorContext& ctx, lamp_protocol::OverrideSurface surface) {
  surface_ = surface;
  switch (surface) {
    case lamp_protocol::OverrideSurface::Base:
      configurator_ = ctx.baseConfigurator;
      break;
    case lamp_protocol::OverrideSurface::Shade:
      configurator_ = ctx.shadeConfigurator;
      break;
    case lamp_protocol::OverrideSurface::BaseAndShade:
      // BaseAndShade is a wire-format value only, emitted by the wisp
      // PaintDistributor to halve frame count; the receive path in
      // lamp.cpp routes the colors into the per-surface ColorOverride
      // instances. ColorOverride itself is always bound per-surface.
      configurator_ = nullptr;
      break;
  }
  if (configurator_ && configurator_->fb) {
    pixelCount_ = configurator_->fb->pixelCount;
  }
}

void ColorOverride::apply(const uint8_t sourceMac[6],
                          lamp_protocol::OverrideSource source,
                          const Color* colors, uint8_t numColors,
                          uint32_t fadeDurationMs) {
  if (!configurator_ || numColors == 0 || !colors) return;
  // Operator-priority lockout: while the app has the colour picker /
  // brightness slider for this surface open, wisp paints lose.
  if (operatorEditing_ &&
      source == lamp_protocol::OverrideSource::Wisp) {
#ifdef LAMP_DEBUG
    // This line with no app connected means operatorEditing stranded true.
    Serial.printf("[override] DROP wisp surface=0x%02X (operatorEditing=true)\n",
                  (unsigned)surface_);
#endif
    return;
  }
  // Source-ownership: a Holding override from a different source can be
  // overtaken, newer apply() wins. savedColors_ is NOT re-snapshotted
  // in that case because it already holds the true pre-any-override
  // baseline. Snapshot only when transitioning from Idle into FadingIn.
  if (state_ == FadeState::Idle) {
    savedColors_ = configurator_->colors;
  }

  // Refresh pixel count from the configurator's frame buffer in case
  // configuration changed (rare; only at boot or factory reset).
  if (configurator_->fb) pixelCount_ = configurator_->fb->pixelCount;

  // Expand the stops into a per-pixel gradient via the existing helper.
  std::vector<Color> stops(colors, colors + numColors);
  std::vector<Color> target =
      lamp::buildGradientWithStops(pixelCount_, stops);

  // Cache the full per-pixel gradient so reassertHold() can re-install
  // it after test_expression_complete's payload momentarily overwrites
  // `configurator.colors` with the lamp's saved baseline.
  targetGradient_ = target;

  // Cache the wisp paint's first stop for the app's indicator. Only
  // record on wisp-sourced applies so a hypothetical future producer
  // can't pollute the "last wisp color" view.
  bool wispColorChanged = false;
  if (source == lamp_protocol::OverrideSource::Wisp) {
    wispColorChanged = !hasLastWispColor_ || !(lastWispColor_ == stops[0]);
    lastWispColor_ = stops[0];
    hasLastWispColor_ = true;
  }

#ifdef LAMP_DEBUG
  // Diagnostic for the 2026-06-13 "jacko renders pink/orange instead of
  // wisp/picker colors" report. Print the actual stops[0] bytes being
  // passed into the configurator so we can compare against the LED output.
  // If this prints the correct RGB but LEDs render wrong, the bug is
  // downstream of beginFade (configurator fade math, compositor overlays,
  // NeoPixel write). If this prints swapped/wrong bytes, the bug is
  // upstream in the wire decode.
  Serial.printf("[override] beginFade surface=0x%02X src=%d target0=(R=%u G=%u B=%u W=%u)\n",
                (unsigned)surface_, (int)source,
                (unsigned)stops[0].r, (unsigned)stops[0].g,
                (unsigned)stops[0].b, (unsigned)stops[0].w);
#endif
  // Wisp pixels are rendered by the compositor's LayerStack wisp layer, fed
  // from the drain adapter. This override only tracks state (isWispActive,
  // lastWispColor, watchdog) that the app + presence provider read.
  state_ = FadeState::FadingIn;
  activeSource_ = source;
  (void)sourceMac;  // not used post-apply; ownership check is by source kind, not MAC
  lastApplyMs_ = millis();
  lastWispSeenMs_ = lastApplyMs_;
  currentFadeDurationMs_ = fadeDurationMs;

  Serial.printf("[override] apply surface=0x%02X src=%d fade=%ums numColors=%u\n",
                (unsigned)surface_, (int)source,
                (unsigned)fadeDurationMs, (unsigned)numColors);

  // Notify the app's wispStatus subscriber on the active-state edge
  // (indicator on/off) and, while already active, on a new wisp paint
  // colour so drift updates reach the app instead of the indicator
  // freezing on the colour control began with.
  if (!maybeNotifyWispStateChange() && wispColorChanged && isWispActive()) {
    if (wispStateCb_) wispStateCb_();
  }
}

void ColorOverride::restore(const uint8_t sourceMac[6],
                            lamp_protocol::OverrideSource source,
                            uint32_t fadeDurationMs) {
  (void)sourceMac;  // not used for the dedup decision; see source
                    // ownership rules below.
  if (!configurator_ || state_ == FadeState::Idle) return;

  // Source-ownership check: only the active source (or Any) can restore.
  // A non-matching source restore is dropped so future producers can't
  // accidentally cancel each other's overrides.
  if (source != lamp_protocol::OverrideSource::Any &&
      source != activeSource_) {
    return;
  }

  // The compositor's wisp presence eases home once this surface leaves
  // FadingIn/Holding; Restoring just holds the indicator on through the ramp.
  state_ = FadeState::Restoring;
  restoreStartMs_ = millis();
  restoreDurationMs_ = fadeDurationMs;

  // Restoring is still isWispActive==true (state != Idle), so the
  // indicator stays on through the fade-out. The Restoring → Idle
  // transition in tick() fires the callback when the override fully
  // releases.
  maybeNotifyWispStateChange();
}

void ColorOverride::tick(uint32_t nowMs) {
  // Auto-restore watchdog. In any active fade state, if the wisp hasn't been
  // seen (an apply, or a paint-mode HELLO keepalive) within the window, revert
  // so a silent wisp can't keep the surface painted forever. Any-source so restore()'s
  // ownership check doesn't bounce it. Keyed off lastWispSeenMs_, not the fade
  // clock, so a long drift fade holds while the wisp is present.
  if ((state_ == FadeState::FadingIn || state_ == FadeState::Holding) &&
      nowMs - lastWispSeenMs_ >= lamp_protocol::kPaintWatchdogMs) {
    Serial.printf("[override] watchdog FIRE surface=0x%02X age=%ums src=%d\n",
                  (unsigned)surface_, (unsigned)(nowMs - lastWispSeenMs_),
                  (int)activeSource_);
    static const uint8_t kZeroMac[6] = {0};
    restore(kZeroMac, lamp_protocol::OverrideSource::Any,
            /*fadeDurationMs=*/kWatchdogRestoreFadeMs);
    return;
  }

  switch (state_) {
    case FadeState::Idle:
      return;
    case FadeState::FadingIn:
      // currentFadeDurationMs_ == 0 (instant snap) also lands here next tick.
      if (nowMs - lastApplyMs_ >= currentFadeDurationMs_) {
        state_ = FadeState::Holding;
      }
      return;
    case FadeState::Holding:
      return;  // watchdog handled above
    case FadeState::Restoring:
      if (nowMs - restoreStartMs_ >= restoreDurationMs_) {
        state_ = FadeState::Idle;
        activeSource_ = lamp_protocol::OverrideSource::None;
        hasLastWispColor_ = false;
        maybeNotifyWispStateChange();
      }
      return;
  }
}

void ColorOverride::touchApply(uint32_t nowMs) {
  if (state_ == FadeState::FadingIn || state_ == FadeState::Holding) {
    lastWispSeenMs_ = nowMs;
  }
}

void ColorOverride::reassertHold() {
  // ponytail: no-op. Wisp pixels live in the compositor's LayerStack, not the
  // configurator, so a saved-colors payload overwriting configurator.colors no
  // longer drops the wisp paint. Callers + this method go in the cleanup slice.
}

void ColorOverride::rebaseline(const std::vector<Color>& currentSavedColors) {
  // BLE write landed mid-Holding (user changed the underlying config
  // colors). Replace the baseline so the next restore lands on the new
  // colors. No-op when Idle; the BLE write went straight to the
  // configurator and there's no override to fix up.
  if (state_ == FadeState::Idle) return;
  savedColors_ = currentSavedColors;
}

}  // namespace lamp
