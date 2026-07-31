#pragma once

#include <vector>

#include "expressions/expression.hpp"
#include "expressions/expression_schema.hpp"
#include "expressions/flicker/flicker_math.hpp"
#include "expressions/primitives.hpp"

namespace lamp {

inline constexpr EnumOption kFireOptions[] = {
  { .value = 0, .label = "Twinkle" },
  { .value = 1, .label = "Coals" },
  { .value = 2, .label = "Candle" },
  { .value = 3, .label = "Campfire" },
};

inline constexpr ParamSpec kFlickerParams[] = {
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

inline constexpr ExpressionDescriptor kFlickerDescriptorData{
  .id         = "flicker",
  .name       = "Shimmer",
  .continuous = true,
  .colors     = { .max = 4, .label = "Colors",
                  .help = "Coolest first (sparkle), hottest last (flame tip)." },
  .hasZone      = true,
  .zoneOptional = true,
  .advanced     = true,
  .params       = kFlickerParams,
};

class FlickerExpression : public Expression {
 public:
  using Expression::Expression;
  FlickerExpression(FrameBuffer* inBuffer, uint32_t inFrames = 90);
  void configureFromParameters(const std::map<std::string, uint32_t>& parameters) override;
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
  void advanceWind(uint32_t nowMs, uint32_t deltaMs);

  Zone zone_;
  FireStyle style_ = fireStyle(1);
  std::vector<Cell> cells_;
  float windOffset_ = 0.0f;
  float windTarget_ = 0.0f;
  uint32_t nextGustMs_ = 0;
  uint32_t lastUpdateMs_ = 0;
};

}  // namespace lamp
