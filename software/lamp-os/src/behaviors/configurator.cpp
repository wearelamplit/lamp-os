#include "configurator.hpp"

#include <Arduino.h>

#include <cstdint>

#include "util/color.hpp"
#include "util/fade.hpp"

namespace lamp {

bool ConfiguratorBehavior::fadeActive(uint32_t nowMs) const {
  if (fadeDurationMs_ == 0) return false;
  return (nowMs - fadeStartMs_) < fadeDurationMs_;
}

void ConfiguratorBehavior::beginFade(const std::vector<Color>& targetColors,
                                     uint16_t fadeDurationMs) {
  // Snapshot the fade-FROM endpoint from the configurator's PREVIOUS
  // intended output (the `colors` field, optionally lerped if we're
  // interrupting a fade in flight) — NOT from fb->buffer. fb->buffer
  // is the post-overlay rendered state, which on Base includes the
  // knockout overlay's per-pixel dimming. Snapshotting from the buffer
  // captured that dimming as the fade baseline and produced a visible
  // flicker on the picker hot path: every new fade interpolated from
  // (dimmed previous color) → (un-dimmed target color), then the
  // knockout overlay re-dimmed the interpolated result, oscillating
  // pixel brightness as fades restarted.
  //
  // Snapshotting from `colors` (or the in-progress lerp value mid-fade)
  // gives us the configurator's INTENDED rendered state, decoupled from
  // any downstream overlays. Mid-fade interrupts still pick up wherever
  // the interpolation has landed — just from the lerp math, not from
  // the post-overlay buffer.
  const size_t n = fb ? static_cast<size_t>(fb->pixelCount) : 0;
  const uint32_t now = millis();
  const bool wasFading = fadeActive(now);
  // Stash the pre-mutation source so the mid-fade branch can read it
  // without aliasing the destination assignment below.
  std::vector<Color> prevFromColors = fadeFromColors_;
  fadeFromColors_.assign(n, Color());
  if (n > 0) {
    if (wasFading && !prevFromColors.empty() && !colors.empty()) {
      // Mid-fade interrupt: compute where the lerp would have landed
      // at `now`, using the previous endpoints. This preserves visual
      // continuity (the new fade starts wherever the old fade was
      // headed) without going through fb->buffer.
      const uint32_t elapsed = now - fadeStartMs_;
      for (size_t i = 0; i < n; ++i) {
        fadeFromColors_[i] = fade(
            prevFromColors[i % prevFromColors.size()],
            colors[i % colors.size()],
            fadeDurationMs_, elapsed);
      }
    } else if (!colors.empty()) {
      // Fresh fade with a known prior target: start from whatever
      // the configurator was painting before this call. Expand if
      // colors.size() differs from pixelCount (the per-stop case).
      for (size_t i = 0; i < n; ++i) {
        fadeFromColors_[i] = colors[i % colors.size()];
      }
    } else if (fb) {
      // Cold-start: no prior `colors` (first beginFade ever). Fall
      // back to fb->buffer because there's no other source. Knockout
      // dimming here doesn't cause flicker because there's nothing
      // for it to be inconsistent against — the next fade onward
      // hits the `colors` branch above.
      for (size_t i = 0; i < n; ++i) {
        fadeFromColors_[i] = fb->buffer[i];
      }
    }
  }
  // Assign the target colors. Callers (ColorOverride::apply, the BLE
  // colors drain) have already done the gradient expansion to pixelCount
  // length, so we just copy.
  colors = targetColors;
  fadeStartMs_ = now;
  fadeDurationMs_ = fadeDurationMs;
}

void ConfiguratorBehavior::draw() {
  // Duration-controlled fade. When fadeDurationMs_ is 0 (set by
  // legacy callers that never went through beginFade — i.e. boot-time
  // initBehaviors which assigns colors directly) or the window has
  // elapsed, write target colors directly. Otherwise per-pixel lerp
  // between fadeFromColors_[i] and colors[i] using the existing quad
  // ease-in-out LUT (via fade()).
  const uint32_t now = millis();
  const bool active = fadeActive(now);
  if (!active || fadeFromColors_.size() != colors.size()) {
    for (int i = 0; i < fb->pixelCount; i++) {
      fb->buffer[i] = colors[i];
    }
  } else {
    // 16-bit math is fine — fadeDurationMs_ ≤ 65535 and elapsed is bounded
    // by that. The fade() helper takes (start, end, steps, currentStep),
    // matches the existing easeFrames-based call site.
    const uint32_t elapsed = now - fadeStartMs_;
    const uint32_t duration = fadeDurationMs_;
    for (int i = 0; i < fb->pixelCount; i++) {
      fb->buffer[i] = fade(fadeFromColors_[i], colors[i], duration, elapsed);
    }
  }

  nextFrame();
};

void ConfiguratorBehavior::control() {
  uint32_t now = millis();
  if (animationState == STOPPED) {
    if (lastWebSocketUpdateTimeMs > 0 &&
        now < lastWebSocketUpdateTimeMs + CONFIGURATOR_WEBSOCKET_TIMEOUT_MS) {
      playOnce();
    }
  }
  if (animationState == PLAYING_ONCE && frame == easeFrames) {
    pause();
  }
  if (animationState == PAUSED && now > lastWebSocketUpdateTimeMs + CONFIGURATOR_WEBSOCKET_TIMEOUT_MS) {
    playOnce();
  }
};
}  // namespace lamp