#pragma once

#include <array>
#include <cstddef>

#include "util/color.hpp"
#include "util/eased_scalar.hpp"
#include "util/fade.hpp"

namespace lamp {

// The wisp layer's color for one surface. Eases from its current value toward
// the last broadcast target, reusing EasedScalar as the [0,1] progress driver
// and fade.hpp's mixColorWeight for the per-channel blend. A new target
// mid-ease snapshots the current color as the new start, so it never jumps.
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
  }

  void tick() { t_.tick(); }
  Color value() const { return mixColorWeight(from_, to_, t_.value()); }

 private:
  float rate_;
  Color from_;
  Color to_;
  EasedScalar t_;
};

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
  void tick(float w) {
    w_ = clamp01(w);
    wisp_.tick();
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
