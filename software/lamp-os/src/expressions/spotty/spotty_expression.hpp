#pragma once
#include <vector>
#include "expressions/expression.hpp"
#include "expressions/expression_schema.hpp"
#include "expressions/primitives.hpp"
#include "util/easing.hpp"

namespace lamp {

inline constexpr uint16_t kSpottyMaxCount = 5;
inline constexpr uint16_t kSpottyMaxSize = 6;

// Make-less descriptor data the .cpp composes via withMake(); native-test
// seam so test_builtin_descriptors pins the production catalog.
inline constexpr ParamSpec kSpottyParams[] = {
  {
    .key   = "count",
    .kind  = ParamKind::Int,
    .label = "Points",
    .min   = 1,
    .max   = Bound::pixels(kSpottyMaxCount),
    .def   = 3,
    .help  = "How many spots appear at once",
  },
  {
    .key        = "size",
    .kind       = ParamKind::Int,
    .label      = "Size",
    .min        = 1,
    .max        = Bound::pixels(kSpottyMaxSize),
    .def        = 3,
    .leftLabel  = "Small",
    .rightLabel = "Large",
    .help       = "Width of each spot",
  },
  {
    .key        = "spotSpeed",
    .kind       = ParamKind::Int,
    .label      = "Speed",
    .min        = 1,
    .max        = 10,
    .def        = 3,
    .invert     = true,
    .leftLabel  = "Slow",
    .rightLabel = "Fast",
    .help       = "Slow: gentle, unpredictable fades. Fast: quick flickers mixed with slower fades.",
  },
  kEasingParam,
};
inline constexpr ExpressionDescriptor kSpottyDescriptorData{
  .id         = "spotty",
  .name       = "Spotty",
  .continuous = true,
  .pausesWispOverride = true,
  .colors     = { .max = 8, .label = "Colors" },
  .hasZone      = true,
  .zoneOptional = true,
  .params       = kSpottyParams,
};

class SpottyExpression : public Expression {
 public:
  using Expression::Expression;
  SpottyExpression(FrameBuffer* inBuffer, uint32_t inFrames = 90);
  void configureFromParameters(const std::map<std::string, uint32_t>& parameters) override;
  static const ExpressionDescriptor& classDescriptor();
  const ExpressionDescriptor& descriptor() const override;
  void draw() override;
  void control() override;
  bool disabledDuringWispOverride() const override { return true; }

 protected:
  void onTrigger() override;

 private:
  struct Spot {
    uint16_t pos = 0;
    Color color;
    uint32_t ageMs = 0;
    uint32_t lifeMs = 3000;
  };
  void respawn(Spot& spot);

  Zone zone_;
  uint16_t points_ = 1;
  uint16_t size_ = 3;
  uint16_t spotSpeed_ = 3;  // 1=fastest..10=slowest; scales the per-spot lifetime roll
  Easing easing_ = Easing::Linear;
  uint32_t lastUpdateMs_ = 0;
  std::vector<Spot> spots_;
};

}  // namespace lamp
