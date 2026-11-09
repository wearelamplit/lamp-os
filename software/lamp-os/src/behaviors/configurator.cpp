#include "configurator.hpp"

#include <Arduino.h>

#include <algorithm>
#include <cstdint>

#include "util/color.hpp"
#include "util/fade.hpp"

namespace lamp {

ConfiguratorBehavior::ConfiguratorBehavior(FrameBuffer* inBuffer, uint32_t inFrames,
                                           Surface surface)
    : AnimatedBehavior(inBuffer, inFrames), surface_(surface) {}

bool ConfiguratorBehavior::fadeActive(uint32_t nowMs) const {
  if (fadeDurationMs_ == 0) return false;
  return (nowMs - fadeStartMs_) < fadeDurationMs_;
}

// Index 0 sits at the shade's bottom and the base's top
// (docs/dev/expressions.md), so Up runs toward index 0 on the base only.
bool ConfiguratorBehavior::towardZero() const {
  return (surface_ == Surface::Base) == (rotation_.dir == RotationDir::Up);
}

Color ConfiguratorBehavior::target(size_t i, uint32_t offset) const {
  return rotating_ ? sampleLoop(colors, offset, i) : colors[i % colors.size()];
}

void ConfiguratorBehavior::beginFade(const std::vector<Color>& targetColors,
                                     uint32_t fadeDurationMs) {
  install(targetColors, fadeDurationMs, ColorRotation{});
}

void ConfiguratorBehavior::showScene(const RoleScene& scene, uint32_t fadeMs) {
  install(scene.colors, fadeMs, scene.rotation);
}

void ConfiguratorBehavior::install(const std::vector<Color>& targetColors,
                                   uint32_t fadeDurationMs,
                                   const ColorRotation& rotation) {
  // Snapshot the fade-FROM endpoint from the configurator's PREVIOUS
  // intended output (the `colors` field, optionally lerped when
  // interrupting a fade in flight), NOT from fb->buffer. fb->buffer
  // is the post-overlay rendered state, which on Base includes the
  // knockout overlay's per-pixel dimming. Snapshotting from the buffer
  // captured that dimming as the fade baseline and produced a visible
  // flicker on the picker hot path: every new fade interpolated from
  // (dimmed previous color) → (un-dimmed target color), then the
  // knockout overlay re-dimmed the interpolated result, oscillating
  // pixel brightness as fades restarted.
  //
  // Snapshotting from `colors` (or the in-progress lerp value mid-fade)
  // gives the configurator's INTENDED rendered state, decoupled from
  // any downstream overlays. Mid-fade interrupts still pick up wherever
  // the interpolation has landed, just from the lerp math, not from
  // the post-overlay buffer.
  const size_t n = fb ? static_cast<size_t>(fb->pixelCount) : 0;
  const uint32_t now = millis();
  const bool wasFading = fadeActive(now);
  // Stash the pre-mutation source so the mid-fade branch can read it
  // without aliasing the destination assignment below.
  std::vector<Color> prevFromColors = fadeFromColors_;
  fadeFromColors_.assign(n, Color());
  const uint32_t offset = rotating_ ? loopOffset(clock_.phase(), n) : 0;
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
            target(i, offset),
            fadeDurationMs_, elapsed);
      }
    } else if (!colors.empty()) {
      // Fresh fade with a known prior target: start from whatever
      // the configurator was painting before this call. Expand if
      // colors.size() differs from pixelCount (the per-stop case).
      for (size_t i = 0; i < n; ++i) {
        fadeFromColors_[i] = target(i, offset);
      }
    } else if (fb) {
      // Cold-start: no prior `colors` (first beginFade ever). Fall
      // back to fb->buffer because there's no other source. Knockout
      // dimming here doesn't cause flicker because there's nothing
      // for it to be inconsistent against; the next fade onward
      // hits the `colors` branch above.
      for (size_t i = 0; i < n; ++i) {
        fadeFromColors_[i] = fb->buffer[i];
      }
    }
  }
  // Assign the target colors. Callers (ColorOverride::apply, the BLE
  // colors drain) have already done the gradient expansion to pixelCount
  // length, so this just copies.
  colors = targetColors;
  fadeStartMs_ = now;
  fadeDurationMs_ = fadeDurationMs;
  rotation_ = rotation;
  rotating_ = rotation.on && n > 0 && colors.size() == n;
}

void ConfiguratorBehavior::setSolid(Color c) {
  const size_t n = fb ? static_cast<size_t>(fb->pixelCount) : 0;
  if (colors.size() != n) {
    colors.assign(n, c);  // one-time size reconcile; steady state fills below
  } else {
    std::fill(colors.begin(), colors.end(), c);
  }
  fadeDurationMs_ = 0;  // instant, no lerp window
  rotating_ = false;
  lastWebSocketUpdateTimeMs = millis();  // keep control() in PLAYING_ONCE
}

void ConfiguratorBehavior::draw() {
  const uint32_t now = millis();
  uint32_t offset = 0;
  if (rotating_) {
    clock_.advance(now, rotation_.periodS, towardZero());
    offset = loopOffset(clock_.phase(), fb->pixelCount);
  }
  const bool active = fadeActive(now);
  if (!active || fadeFromColors_.size() != colors.size()) {
    for (int i = 0; i < fb->pixelCount; i++) {
      fb->buffer[i] = rotating_ ? sampleLoop(colors, offset, i) : colors[i];
    }
  } else {
    const uint32_t elapsed = now - fadeStartMs_;
    const uint32_t duration = fadeDurationMs_;
    for (int i = 0; i < fb->pixelCount; i++) {
      const Color to = rotating_ ? sampleLoop(colors, offset, i) : colors[i];
      fb->buffer[i] = fade(fadeFromColors_[i], to, duration, elapsed);
    }
  }

  nextFrame();
};

void ConfiguratorBehavior::control() {
  uint32_t now = millis();
  if (rotating_ || fb->defaultRotation.on) {
    controlRotating(now);
    return;
  }
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

bool ConfiguratorBehavior::showsRotatingSaved() const {
  return rotating_ && rotation_.periodS == fb->defaultRotation.periodS &&
         rotation_.dir == fb->defaultRotation.dir && colors == fb->defaultColors;
}

// Past the timeout a rotating saved scene plays on and never stops. A static
// one takes the static path's run-out to STOPPED (frames - easeFrames draws),
// which outlasts the crossfade at the compositor's 16 ms frame floor, so idle
// takes over on the saved frame.
void ConfiguratorBehavior::controlRotating(uint32_t now) {
  const bool editing = lastWebSocketUpdateTimeMs > 0 &&
                       now <= lastWebSocketUpdateTimeMs + CONFIGURATOR_WEBSOCKET_TIMEOUT_MS;
  if (editing) {
    if (animationState == STOPPED) {
      playOnce();
    } else if (animationState == PLAYING) {
      pause();
    }
    if (animationState == PLAYING_ONCE && frame == easeFrames) {
      pause();
    }
    return;
  }
  if (fb->defaultRotation.on) {
    if (animationState != PLAYING) {
      if (!showsRotatingSaved()) install(fb->defaultColors, kDefaultFadeMs, fb->defaultRotation);
      play();
    }
    return;
  }
  if (animationState != STOPPED) {
    install(fb->defaultColors, kDefaultFadeMs, fb->defaultRotation);
    frame = easeFrames;
    playOnce();
  }
}
}  // namespace lamp
