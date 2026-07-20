#pragma once

#include "expressions/expression.hpp"
#include "expressions/expression_schema.hpp"
#include "expressions/primitives.hpp"

namespace lamp {

// Make-less descriptor data the .cpp composes via withMake(); native-test
// seam so test_builtin_descriptors pins the production catalog.
inline constexpr ParamSpec kGlitchyParams[] = {
  {
    .key            = "scatter",
    .kind           = ParamKind::Int,
    .label          = "Scatter",
    .min            = 0,
    .max            = Bound{5},
    .step           = 1,
    .def            = 0,
    .help           = "0 is a solid glitch; higher scatters it into finer, sparser flecks.",
    .requiresZoning = false,
  },
};
inline constexpr ExpressionDescriptor kGlitchyDescriptorData{
  .id           = "glitchy",
  .name         = "Glitchy",
  .colors       = { .max = 8, .label = "Colors" },
  .interval     = RangeSpec{
    .min    = 600,
    .max    = 18000,
    .step   = 30,
    .unit   = "s",
    .defLo  = 1800,
    .defHi  = 7200,
    .minGap = 1800,
    .help   = kIntervalHelp,
  },
  .duration     = RangeSpec{
    .min    = 30,
    .max    = 1000,
    .step   = 30,
    .unit   = "ms",
    .defLo  = 30,
    .defHi  = 120,
    .label  = "Glitch duration",
    .help   = "Each glitch lasts a random time in this range.",
    .minKey = "durationMin",
    .maxKey = "durationMax",
  },
  .hasZone      = true,
  .zoneOptional = true,
  .params       = kGlitchyParams,
};

/**
 * Glitchy expression - creates random visual glitches.
 */
class GlitchyExpression : public Expression {
 private:
  Color glitchColor;
  uint32_t glitchDurationMinMs = 30;
  uint32_t glitchDurationMaxMs = 120;
  uint32_t glitchEndMs = 0;
  // A glitch shorter than one flush window still paints one frame before the
  // deadline-driven restore.
  bool painted_ = false;
  Zone zone_;
  uint16_t scatter_ = kGlitchScatterMax;

  void paintGlitch_();

 public:
  using Expression::Expression;

  /**
   * @param inBuffer Frame buffer to use
   * @param inFrames Initial frame count
   */
  GlitchyExpression(FrameBuffer* inBuffer, uint32_t inFrames = 3);

  /**
   * Configure glitchy-specific parameters from generic parameter map.
   * @param parameters Map containing expression-specific parameters
   */
  void configureFromParameters(const std::map<std::string, uint32_t>& parameters) override;
  static const ExpressionDescriptor& classDescriptor();
  const ExpressionDescriptor& descriptor() const override;

  void draw() override;

protected:
  void onTrigger() override;
  void onComplete() override;
};

}  // namespace lamp
