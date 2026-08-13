#pragma once

namespace lamp {

// Full 0->1 traverse takes 1/kDefaultEaseRate frames; 30 matches the greeting
// easeInFrames. Bench-tunable.
inline constexpr float kDefaultEaseRate = 1.0f / 30.0f;

inline constexpr float clamp01(float v) {
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// A scalar in [0,1] that chases a moving target, advancing at most `rate` per
// tick(). Reversal is a new setTarget() at any instant; the next tick turns
// around from wherever `current` sits, never jumping.
class EasedScalar {
 public:
  explicit EasedScalar(float rate = kDefaultEaseRate, float initial = 0.0f)
      : rate_(rate > 0.0f ? rate : kDefaultEaseRate),
        current_(clamp01(initial)),
        target_(clamp01(initial)) {}

  void setTarget(float t) { target_ = clamp01(t); }

  void tick() {
    const float delta = target_ - current_;
    if (delta > rate_)
      current_ += rate_;
    else if (delta < -rate_)
      current_ -= rate_;
    else
      current_ = target_;
  }

  float value() const { return current_; }
  float target() const { return target_; }

 private:
  float rate_;
  float current_;
  float target_;
};

// Composed opacity target for one layer: disabled forces 0; the wisp presence
// `w` in [0,1] dims userOpacity toward wispDimFloor (1.0 = ignore the wisp).
inline float opacityTarget(bool enabled, float userOpacity, float wispDimFloor,
                           float w) {
  const float base = enabled ? userOpacity : 0.0f;
  return clamp01(base * (1.0f + (wispDimFloor - 1.0f) * w));
}

}  // namespace lamp
