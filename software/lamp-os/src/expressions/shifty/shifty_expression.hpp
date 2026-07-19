#pragma once

#include <vector>

#include "expressions/expression.hpp"
#include "expressions/expression_schema.hpp"
#include "expressions/primitives.hpp"
#include "util/easing.hpp"

namespace lamp {

// Make-less descriptor data the .cpp composes via withMake(); native-test
// seam so test_builtin_descriptors pins the production catalog.
inline constexpr EnumOption kShiftyFillOpts[] = {
  { .value = 0, .label = "Uniform" },
  { .value = 1, .label = "Up"      },
  { .value = 2, .label = "Down"    },
  { .value = 3, .label = "Bloom"   },
};
inline constexpr ParamSpec kShiftyParams[] = {
  {
    .key     = "fillMode",
    .kind    = ParamKind::Enum,
    .label   = "Fill",
    .max     = 3,
    .help    = "How the new color spreads: Uniform all at once, Up/Down from one end, Bloom outward from the center.",
    .options = kShiftyFillOpts,
  },
  {
    .key        = "fadeDuration",
    .kind       = ParamKind::Int,
    .label      = "Fade duration",
    .min        = 30,
    .max        = 600,
    .def        = 60,
    .unit       = "s",
    .leftLabel  = "quick",
    .rightLabel = "slow",
    .help       = "How long each color transition takes",
  },
  kEasingParam,
};
// duration models the hold time (shiftDurationMin/Max in seconds).
inline constexpr ExpressionDescriptor kShiftyDescriptorData{
  .id           = "shifty",
  .name         = "Shifty",
  .continuous   = true,
  .pausesWispOverride = true,
  .colors       = { .max = 8, .label = "Colors" },
  .duration     = RangeSpec{
    .min    = 60,
    .max    = 1800,
    .step   = 30,
    .unit   = "s",
    .defLo  = 300,
    .defHi  = 600,
    .label  = "Hold time",
    .help   = "How long each color holds before the next shift (a random time in this range).",
    .minKey = "shiftDurationMin",
    .maxKey = "shiftDurationMax",
  },
  .hasZone      = true,
  .zoneOptional = true,
  .params       = kShiftyParams,
};

/**
 * Shifty expression - smoothly transitions to random colors.
 *
 * Creates subtle ambient shifts by slowly fading to a random color,
 * staying for a duration, then fading back to original colors. Every phase
 * is timed off millis(); the frame counter never ends a fade or a hold.
 */
class ShiftyExpression : public Expression {
 private:
  Zone zone_;
  uint8_t fillMode_ = 0;
  Easing easing_ = Easing::Linear;
  std::vector<uint32_t> pixelStartOffsetMs_;

  // Currently shifted color
  Color shiftedColor;

  // State machine
  enum ShiftState {
    IDLE,               // Waiting for next trigger
    FADING_TO_PALETTE,  // Transitioning to new palette
    SHIFTED,            // Displaying palette colors
    FADING_BACK         // Returning to original colors
  };
  ShiftState state = IDLE;

  // Timing configuration
  uint32_t shiftDurationMinMs = 300000;  // 5 min default
  uint32_t shiftDurationMaxMs = 600000;  // 10 min default
  uint32_t fadeDurationMs = 60000;

  // Runtime state
  uint32_t shiftStartMs = 0;
  uint32_t currentShiftDurationMs = 0;
  uint32_t fadeStartMs = 0;

  // Color buffers for smooth transitions
  std::vector<Color> fadeStartColors;
  std::vector<Color> fadeTargetColors;

  // Per-frame cache (perf): the factor from (elapsed, fadeDurationMs) is
  // identical for every pixel within a fade frame. Hoisted out of the
  // per-pixel hot path in draw(). Updated in onUpdate(); read-only in draw().
  uint32_t cachedFadeFactor_ = 0;       // Precomputed linear "factor" (matches easeLinear)
  bool cachedFadeAtEnd_ = false;        // True when elapsed >= fadeDurationMs (end-clamp short-circuit)


  /**
   * Start shifting to a new color.
   */
  void startShift();

  /**
   * Start returning to original colors.
   */
  void startUnshift();

  /**
   * Get random shift duration within configured range.
   */
  uint32_t getRandomShiftDuration();

  void populatePixelStartOffsets(bool fadingBack);

 public:
  using Expression::Expression;

  /**
   * @param inBuffer Frame buffer to use
   * @param inFrames Initial frame count
   */
  ShiftyExpression(FrameBuffer* inBuffer, uint32_t inFrames = 120);

  /**
   * Configure shifty-specific parameters from generic parameter map.
   * @param parameters Map containing expression-specific parameters
   */
  void configureFromParameters(const std::map<std::string, uint32_t>& parameters) override;
  static const ExpressionDescriptor& classDescriptor();
  const ExpressionDescriptor& descriptor() const override;

  void draw() override;

  // Continuous palette-shift animation; visually fights the wisp's
  // hold colour, so must pause while wisp is overriding.
  bool disabledDuringWispOverride() const override { return true; }

protected:
  void onTrigger() override;
  void onUpdate() override;
  void onComplete() override;
};

}  // namespace lamp
