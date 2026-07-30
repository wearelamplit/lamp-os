#pragma once

#include "expressions/expression.hpp"
#include "expressions/expression_schema.hpp"
#include "util/easing.hpp"

namespace lamp {

// Frames in one bloom swell (~0.75 s at 60 fps).
inline constexpr uint32_t kBloomFrames = 45;

// Eased 0..255 swell for a one-shot bloom: dark at the ends, peak at the
// midpoint, so the animation self-terminates back to no contribution. One
// scalar per frame (float here); the per-pixel apply is pure integer.
inline uint8_t bloomSwell(uint32_t frame, uint32_t frames) {
  if (frames < 2) return 0;
  const float p = static_cast<float>(frame) / static_cast<float>(frames);
  const float tri = p < 0.5f ? p * 2.0f : (1.0f - p) * 2.0f;  // 0..1..0
  const float eased = applyEasing(Easing::Smooth, tri < 0.0f ? 0.0f : tri);
  return static_cast<uint8_t>(eased * 255.0f + 0.5f);
}

// Integer per-channel lerp of `c` toward white by scalar s (0..255). s=0
// leaves the color unchanged; s=255 is full white. Luminance-add only, so it
// composites cleanly over a greeting instead of repainting its hue.
inline Color bloomWhiten(Color c, uint8_t s) {
  auto up = [s](uint8_t ch) -> uint8_t {
    return static_cast<uint8_t>(ch + (255 - ch) * static_cast<uint16_t>(s) / 255);
  };
  return Color(up(c.r), up(c.g), up(c.b), up(c.w));
}

inline constexpr ExpressionDescriptor kBloomDescriptorData{
  .id       = "bloom",
  .name     = "Bloom",
  .colors   = { .max = 0 },
  .internal = true,
};

// One-shot white-ward luminance swell. Reads the live buffer each frame and
// lerps every pixel toward white by an eased scalar, so it layers over
// whatever a greeting drew below it. Framework-internal: fired via
// triggerInvocation, never listed in the app catalog.
class BloomExpression : public Expression {
 public:
  using Expression::Expression;
  explicit BloomExpression(FrameBuffer* inBuffer, uint32_t inFrames = kBloomFrames);

  void configureFromParameters(const std::map<std::string, uint32_t>&) override {}
  static const ExpressionDescriptor& classDescriptor();
  const ExpressionDescriptor& descriptor() const override;

  void draw() override;

 protected:
  void onTrigger() override;
};

}  // namespace lamp
