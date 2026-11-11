#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "util/color.hpp"
#include "util/eased_scalar.hpp"
#include "util/fade.hpp"

namespace lamp {

// The wisp layer's color for one surface. Eases from its current value toward
// the last broadcast target, reusing EasedScalar as the [0,1] progress driver
// and fade.hpp's mixColorWeight for the per-channel blend, or runs a timed fade
// on lamp::fade's curve. A new target mid-ease or mid-fade snapshots the
// current color as the new start, so it never jumps.
// setTarget is edge-triggered: call on a new target, not every frame — re-arming
// the ease each tick keeps restarting progress and it never settles.
class EasedColor {
 public:
  explicit EasedColor(float rate = kDefaultEaseRate, Color initial = Color())
      : rate_(rate), from_(initial), to_(initial), t_(rate, 1.0f) {}

  void setTarget(Color c) {
    from_ = value();
    to_ = c;
    t_ = EasedScalar(rate_, 0.0f);
    t_.setTarget(1.0f);
    timed_ = false;
  }

  // Fade to `to` over `durMs` from `nowMs`, ending any ease or fade.
  void setTargetOver(Color to, uint32_t durMs, uint32_t nowMs) {
    from_ = value();
    to_ = to;
    startMs_ = nowMs;
    durMs_ = durMs;
    nowMs_ = nowMs;
    timed_ = true;
    fadeDone_ = durMs == 0;
  }

  // Show `c` now, with nothing in progress.
  void snap(Color c) {
    from_ = c;
    to_ = c;
    t_ = EasedScalar(rate_, 1.0f);
    timed_ = false;
  }

  void tick(uint32_t nowMs) {
    t_.tick();
    nowMs_ = nowMs;
    if (timed_ && !fadeDone_ && nowMs - startMs_ >= durMs_) fadeDone_ = true;
  }

  Color value() const {
    if (!timed_) return mixColorWeight(from_, to_, t_.value());
    if (fadeDone_) return to_;
    return fade(from_, to_, durMs_, nowMs_ - startMs_);
  }

  bool timed() const { return timed_; }
  bool fadeRunning(uint32_t nowMs) const {
    return timed_ && !fadeDone_ && nowMs - startMs_ < durMs_;
  }
  // True for a timed fade toward `to` that ends within `toleranceMs` of `endMs`.
  bool fadeEndsNear(Color to, uint32_t endMs, uint32_t toleranceMs) const {
    if (!timed_ || !(to_ == to)) return false;
    const int32_t off = static_cast<int32_t>(endMs - (startMs_ + durMs_));
    const int32_t tol = static_cast<int32_t>(toleranceMs);
    return off <= tol && off >= -tol;
  }
  Color target() const { return to_; }

 private:
  float rate_;
  Color from_;
  Color to_;
  EasedScalar t_;
  bool timed_ = false;
  bool fadeDone_ = false;
  uint32_t startMs_ = 0;
  uint32_t durMs_ = 0;
  uint32_t nowMs_ = 0;
};

// What a drift fade record did to the wisp layer.
enum class WispFadeSync : uint8_t { Kept, Started, Retimed };

// One surface's compositing stack: own color, then the wisp layer (eased color
// at the shared presence opacity `w`), then expression layers (each an eased
// opacity via opacityTarget). Base and shade are two stacks sharing one `w`; the
// caller ticks a single `w` and feeds both. The stack owns the persistent ease
// state; the caller feeds each frame's own color + expression outputs.
class LayerStack {
 public:
  static constexpr size_t kMaxExpressionLayers = 4;

  explicit LayerStack(float wispEaseRate = kDefaultEaseRate)
      : wisp_(wispEaseRate) {}

  // Edge-triggered: call on a new broadcast target, not per-frame (see EasedColor).
  void setWispTarget(Color c) { wisp_.setTarget(c); }
  const EasedColor& wisp() const { return wisp_; }
  void snapWisp(Color c) { wisp_.snap(c); }

  // A drift fade record: fade to `to`, ending `leftMs` after `nowMs`. Leaves a
  // fade already heading to `to` alone when its end lands within
  // kDriftRetimeToleranceMs of that, so a repeated record never restarts it.
  WispFadeSync syncWispFade(Color to, uint32_t leftMs, uint32_t nowMs) {
    const uint32_t endMs = nowMs + leftMs;
    if (wisp_.fadeEndsNear(to, endMs, kDriftRetimeToleranceMs)) {
      return WispFadeSync::Kept;
    }
    const bool retime = wisp_.timed() && wisp_.target() == to;
    wisp_.setTargetOver(to, leftMs, nowMs);
    return retime ? WispFadeSync::Retimed : WispFadeSync::Started;
  }
  bool wispFadeRunning(uint32_t nowMs) const { return wisp_.fadeRunning(nowMs); }
  bool wispTimed() const { return wisp_.timed(); }

  // Rides out drain latency, the record's 100 ms rounding and clock drift
  // across an hour's fade; too tight and every repeat restarts the fade.
  static constexpr uint32_t kDriftRetimeToleranceMs = 1000;

  // Register/refresh the expression layer at `index` for this frame. enabled /
  // userOpacity / wispDimFloor feed opacityTarget(); the layer's opacity chases
  // it in tick(). Out-of-range index is ignored.
  void setExpressionLayer(size_t index, Color color, bool enabled,
                          float userOpacity, float wispDimFloor) {
    if (index >= kMaxExpressionLayers) return;
    ExpressionLayer& layer = expr_[index];
    layer.color = color;
    layer.enabled = enabled;
    layer.userOpacity = userOpacity;
    layer.wispDimFloor = wispDimFloor;
    layer.active = true;
  }

  void clearExpressionLayer(size_t index) {
    if (index >= kMaxExpressionLayers) return;
    expr_[index].active = false;
  }

  // Advance all eased state one frame for the shared wisp presence `w`.
  void tick(float w, uint32_t nowMs) {
    w_ = clamp01(w);
    wisp_.tick(nowMs);
    for (ExpressionLayer& layer : expr_) {
      if (!layer.active) continue;
      layer.opacity.setTarget(opacityTarget(layer.enabled, layer.userOpacity,
                                            layer.wispDimFloor, w_));
      layer.opacity.tick();
    }
  }

  // Composite the surface's own color up through the stack at the current
  // eased opacities. Pure; call after tick().
  Color composite(Color own) const {
    Color out = mixColorWeight(own, wisp_.value(), w_);
    for (const ExpressionLayer& layer : expr_) {
      if (!layer.active) continue;
      out = mixColorWeight(out, layer.color, layer.opacity.value());
    }
    return out;
  }

 private:
  struct ExpressionLayer {
    Color color;
    EasedScalar opacity{kDefaultEaseRate, 0.0f};
    float userOpacity = 1.0f;
    float wispDimFloor = 1.0f;
    bool enabled = false;
    bool active = false;
  };

  EasedColor wisp_;
  float w_ = 0.0f;
  std::array<ExpressionLayer, kMaxExpressionLayers> expr_{};
};

}  // namespace lamp
