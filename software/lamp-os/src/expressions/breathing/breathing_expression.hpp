#pragma once

#include <array>

#include "expressions/expression.hpp"
#include "expressions/expression_schema.hpp"
#include "expressions/primitives.hpp"
#include "util/easing.hpp"

namespace lamp {

// Make-less descriptor data the .cpp composes via withMake(); native-test
// seam so test_builtin_descriptors pins the production catalog.
inline constexpr ParamSpec kBreathingParams[] = {
  {
    .key        = "breathSpeed",
    .kind       = ParamKind::Int,
    .label      = "Breath cycle length",
    .min        = 8,
    .max        = 60,
    .def        = 10,
    .unit       = "s",
    .invert     = true,
    .leftLabel  = "slow",
    .rightLabel = "fast",
    .help       = "How long one full fade in and out takes",
  },
  {
    .key   = "sections",
    .kind  = ParamKind::Int,
    .label = "Sections",
    .min   = 1,
    .max   = 5,
    .def   = 1,
    .help  = "How many segments breathe independently",
  },
  // Shared easing enum, but the default is Smooth: it reproduces the sine
  // breath curve, where the shared kEasingParam's Linear default would not.
  {
    .key     = "easing",
    .kind    = ParamKind::Enum,
    .label   = "Motion",
    .max     = 4,
    .def     = 1,
    .help    = kEasingHelp,
    .options = kEasingOptions,
  },
};
inline constexpr ExpressionDescriptor kBreathingDescriptorData{
  .id         = "breathing",
  .name       = "Breathing",
  .continuous = true,
  .pausesWispOverride = true,
  .colors     = { .max = 8, .label = "Colors" },
  .hasZone      = true,
  .zoneOptional = true,
  .params       = kBreathingParams,
};

/**
 * Breathing expression - creates a continuous breathing effect.
 *
 * Creates a smooth breathing effect that continuously fades between the base
 * color and a single target color. This expression is always running and non-exclusive.
 */
class BreathingExpression : public Expression {
 private:
  // Breath state
  float breathPhase = 0.0f;           // Current phase in breath cycle (0.0 to 1.0)
  uint32_t lastBreathUpdateMs = 0;    // Last time breath phase was updated

  // Configuration
  uint32_t breathSpeedMs = 10000;     // Total breath cycle time in ms (default 10s)
  Color targetColor;                   // Target color to breathe towards
  Easing easing_ = Easing::Smooth;    // Smooth reproduces the sine breath curve

  // Bench-tunable aesthetic: pixels tapered at each end (higher = wider soft
  // shoulder).
  static constexpr uint16_t kBreathTaperWidth = 5;

  // Bench-tunable phase step between consecutive breath-order sections. Smaller
  // = more overlap (the next section fades in while the prior nears the end of
  // its fade-out). Max offset (5-1)*this stays < 1, so one wrap suffices.
  static constexpr float kBreathStaggerFrac = 0.15f;

  Zone zone_;
  uint16_t sections_ = 1;
  uint16_t usableSections_ = 1;
  // Random breath order, so the wave hops around the strip instead of sweeping
  // it. Resolved once per trigger; sectionOrder_[b] is the breath index of band
  // b. Sized for the max sections.
  std::array<uint8_t, 5> sectionOrder_{};

  /**
   * Update breath phase based on elapsed time.
   */
  void updateBreathPhase();

 public:
  using Expression::Expression;

  /**
   * @param inBuffer Frame buffer to use
   * @param inFrames Animation duration (not used for continuous breathing)
   */
  BreathingExpression(FrameBuffer* inBuffer, uint32_t inFrames = 60);

  /**
   * Configure breathing-specific parameters from generic parameter map.
   * @param parameters Map containing expression-specific parameters
   */
  void configureFromParameters(const std::map<std::string, uint32_t>& parameters) override;
  static const ExpressionDescriptor& classDescriptor();
  const ExpressionDescriptor& descriptor() const override;

  void draw() override;
  void control() override;  // Override to keep always running

  // Continuous fade between palette colours; visually fights the wisp's
  // hold colour, so must pause while wisp is overriding.
  bool disabledDuringWispOverride() const override { return true; }

protected:
  void onTrigger() override;
  void onUpdate() override;
};

}  // namespace lamp
