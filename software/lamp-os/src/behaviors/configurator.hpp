#pragma once

#include <cstdint>
#include <vector>

#include "config/config_types.hpp"
#include "core/animated_behavior.hpp"
#include "util/color.hpp"

#define CONFIGURATOR_WEBSOCKET_TIMEOUT_MS 60000

/**
 * a layer to preview realtime changes from the web
 *        configuration tool
 */
namespace lamp {

// Per-pixel fade window for BLE color writes (no explicit fadeDurationMs
// supplied by the caller). 100ms sits right at the human just-noticeable
// fade vs snap boundary: smooth enough to avoid stepping on slow drags,
// snappy enough that the lamp tracks the slider in near-real-time.
// Override callers (ColorOverride for wisp paint, peer-swap, etc.) pass
// their own fadeDurationMs and don't touch this default.
constexpr uint16_t kDefaultFadeMs = 100;

class ConfiguratorBehavior : public AnimatedBehavior {
  using AnimatedBehavior::AnimatedBehavior;

 public:
  // Frame-count knob for control() state-machine timing; per-pixel
  // interpolation uses fadeStartMs_/fadeDurationMs_.
  uint32_t easeFrames = 60;
  std::vector<Color> colors;
  unsigned long lastWebSocketUpdateTimeMs = 0;

  void draw() override;

  void control() override;

  // Drive a duration-controlled fade from the current buffer state
  // to `targetColors`. Snapshots the current buffer into fadeFromColors_,
  // assigns `targetColors` as the new `colors` (which is what draw() will
  // eventually write directly once the fade window elapses), records the
  // start timestamp and duration. Calling beginFade() while a fade is in
  // progress snapshots the CURRENT interpolated buffer as the new "from"
  // so the transition stays smooth (no rubber-banding back to the old
  // start). When `fadeDurationMs == 0` (or `targetColors` is empty) this
  // is effectively an instant set.
  void beginFade(const std::vector<Color>& targetColors,
                 uint32_t fadeDurationMs);

  // ColorOverride/BLE color writes need to read back the
  // fade-tracking state to decide whether the configurator is mid-fade.
  // Exposed publicly so the override modules can drive the transition
  // without owning their own per-pixel interpolation.
  uint32_t fadeStartMs() const { return fadeStartMs_; }
  uint32_t fadeDurationMs() const { return fadeDurationMs_; }
  bool fadeActive(uint32_t nowMs) const;

 private:
  // The buffer snapshot taken at beginFade() time. Allocated once per
  // beginFade(); per-pixel interp in draw() walks it alongside `colors`.
  // Sized to pixelCount on first beginFade() and resized only when the
  // pixel count changes (rare; only on configuration boot).
  std::vector<Color> fadeFromColors_;
  uint32_t fadeStartMs_ = 0;
  uint32_t fadeDurationMs_ = 0;
};

}  // namespace lamp
