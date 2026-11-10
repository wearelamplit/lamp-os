#pragma once

#include <vector>

#include "expressions/expression.hpp"
#include "expressions/expression_schema.hpp"
#include "expressions/shimmer/shimmer_math.hpp"
#include "expressions/primitives.hpp"

namespace lamp {

inline constexpr EnumOption kFireOptions[] = {
  { .value = 0, .label = "Twinkle" },
  { .value = 1, .label = "Coals" },
  { .value = 2, .label = "Candle" },
  { .value = 3, .label = "Campfire" },
};

inline constexpr ParamSpec kShimmerParams[] = {
  {
    .key   = "fire",
    .kind  = ParamKind::Enum,
    .label = "Style",
    .max   = 3,
    .def   = 1,
    .help  = "From sparse Twinkle stars to a lively Campfire blaze.",
    .options = kFireOptions,
  },
  kOpacityParam,
};

inline constexpr ExpressionDescriptor kShimmerDescriptorData{
  // "flicker" is the stable persisted/wire key (NVS + exprcat catalog + app),
  // intentionally not renamed with the code identity; a rename needs an NVS
  // migration + app coordination.
  .id         = "flicker",
  .name       = "Shimmer",
  .continuous = true,
  .colors     = { .max = 0 },
  .hasZone      = true,
  .zoneOptional = true,
  .advanced     = false,
  .params       = kShimmerParams,
};

class ShimmerExpression : public Expression {
 public:
  using Expression::Expression;
  ShimmerExpression(FrameBuffer* inBuffer, uint32_t inFrames = 90);
  void configureFromParameters(const ExpressionParams& parameters) override;
  static const ExpressionDescriptor& classDescriptor();
  const ExpressionDescriptor& descriptor() const override;
  void draw() override;
  void control() override;
  float wispDimFloor() const override { return 0.3f; }

 protected:
  void onTrigger() override;

 private:
  struct Cell {
    float heat = 0.0f;
    float target = 0.0f;
  };
  void advanceWind(uint32_t deltaMs);
  void drawBurst();

  Zone zone_;
  FireStyle style_ = fireStyle(1);
  std::vector<Cell> cells_;
  float windOffset_ = 0.0f;
  float windTarget_ = 0.0f;
  Flutter flutter_;
  // Gust deadline and burst start on lamp::uptimeMs(): home mode can freeze a
  // shimmer for weeks.
  uint64_t nextGustMs_ = 0;
  uint32_t lastUpdateMs_ = 0;
  // Hidden burst params: `count` stars over `durationMax` ms, both > 0, make a
  // transient's trigger one short burst that completes; a colour in the
  // invocation tints its stars (docs/dev/moments.md). A configured instance,
  // enabled or not, runs continuously whatever it carries.
  bool bursting() const { return burstMs_ > 0 && isTransient; }
  uint16_t burstCount_ = 0;
  uint32_t burstMs_ = 0;
  uint64_t burstStartMs_ = 0;
  bool burstTinted_ = false;
  Color burstTint_;
};

}  // namespace lamp
