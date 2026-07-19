#pragma once

#include "expressions/expression.hpp"
#include "expressions/expression_schema.hpp"
#include "expressions/primitives.hpp"
#include "util/easing.hpp"

namespace lamp {

// Make-less descriptor data the .cpp composes via withMake(); native-test
// seam so test_builtin_descriptors pins the production catalog.
inline constexpr EnumOption kPulseLoopOpts[] = {
  { .value = 0, .label = "Trigger"    },
  { .value = 1, .label = "Continuous" },
};
inline constexpr ParamSpec kPulseParams[] = {
  {
    .key        = "pulseSpeed",
    .kind       = ParamKind::Int,
    .label      = "Pulse speed",
    .min        = 1,
    .max        = 10,
    .def        = 3,
    .unit       = "s",
    .invert     = true,
    .leftLabel  = "slow",
    .rightLabel = "fast",
    .help       = "How fast the wave travels the strip",
  },
  {
    .key   = "size",
    .kind  = ParamKind::Int,
    .label = "Size",
    .min   = 5,
    .max   = 100,
    .def   = 40,
    .unit  = "%",
    .help  = "Width of the pulse as a share of the strip",
  },
  kEasingParam,
  {
    .key     = kLoopParamKey,
    .kind    = ParamKind::Enum,
    .label   = "Loop",
    .max     = 1,
    .help    = "Trigger runs one sweep then stops. Continuous ping-pongs back and forth forever.",
    .options = kPulseLoopOpts,
  },
};
// Continuous pulse runs its wave center between the visible edge pixels (not
// off-screen), so Float's end-dwell leaves a lit blob hanging at each end
// instead of a dark gap. Inset shrinks that range symmetrically; 0 = full zone.
// Bench-tunable.
inline constexpr float kContinuousTravelInset = 0.0f;

// Continuous pulse ramps its applied brightness 0 -> full over this window on
// first appearance so it grows in rather than popping on at the edge.
inline constexpr uint32_t kEbbInMs = 800;

inline constexpr ExpressionDescriptor kPulseDescriptorData{
  .id       = "pulse",
  .name     = "Pulse",
  .colors   = { .max = 8, .label = "Colors" },
  .interval = RangeSpec{
    .min   = 60,
    .max   = 900,
    .step  = 30,
    .unit  = "s",
    .defLo = 60,
    .defHi = 900,
    .help  = kIntervalHelp,
  },
  .hasZone      = true,
  .zoneOptional = true,
  .params       = kPulseParams,
};

/**
 * Pulse expression - creates a vertical wave that travels through the strips.
 *
 * Creates a smooth pulse effect that moves vertically, with configurable
 * speed and colors. The pulse blends with existing colors for a layered effect.
 */
class PulseExpression : public Expression {
 private:
  // Wave state
  float wavePosition = 0.0f;      // Current position of wave center (in pixels)
  int waveDirection = 1;          // 1 for up, -1 for down
  float progress_ = 0.0f;         // Linear travel phase [0,1]; eased into wavePosition
  float travelStart_ = 0.0f;      // wavePosition at progress 0
  float travelSpan_ = 1.0f;       // Pixels covered from progress 0 to 1

  // Configuration
  uint32_t pulseSpeedMs = 100;    // Time for wave to move one pixel (ms)
  uint32_t pulseWidth = 3;        // Fade radius in pixels on each side
  Color pulseColor;                // Current pulse color
  Zone zone_;                      // Wave travel bounds
  Easing easing_ = Easing::Linear;
  bool loopContinuous_ = false;   // Ping-pong forever instead of ending on exit
  bool reachedFarEnd_ = false;    // Continuous wave has touched the far end; arms preview one-cycle stop

  // Timing
  uint32_t lastUpdateMs = 0;      // Last time wave position was updated
  uint32_t ebbStartMs_ = 0;       // Continuous ebb-in origin, set at first appearance

  // Brightness scale applied to the whole pulse: 1.0 except during the
  // continuous ebb-in ramp. Trigger mode is always 1.0.
  float ebbInScale() const;

  /**
   * Calculate blend factor for a pixel based on distance from wave center.
   * @param pixelIndex Index of the pixel
   * @return Blend factor (0 to 100, representing 0% to 100%)
   */
  uint32_t calculateBlendFactor(int pixelIndex) const;

  /**
   * Update wave position based on elapsed time.
   */
  void updateWavePosition();

  /**
   * Select next pulse color from palette.
   */
  void selectNextColor();

 public:
  using Expression::Expression;

  /**
   * @param inBuffer Frame buffer to use
   * @param inFrames Animation duration (not used for continuous pulse)
   */
  PulseExpression(FrameBuffer* inBuffer, uint32_t inFrames = 60);

  /**
   * Configure pulse-specific parameters from generic parameter map.
   * @param parameters Map containing expression-specific parameters
   */
  void configureFromParameters(const std::map<std::string, uint32_t>& parameters) override;
  static const ExpressionDescriptor& classDescriptor();
  const ExpressionDescriptor& descriptor() const override;

  void draw() override;

protected:
  void onTrigger() override;
  void onUpdate() override;
};

}  // namespace lamp
