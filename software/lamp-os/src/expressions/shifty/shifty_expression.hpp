#pragma once

#include <vector>

#include "expressions/expression.hpp"
#include "expressions/expression_schema.hpp"
#include "expressions/primitives.hpp"

namespace lamp {

/**
 * @brief Shifty expression - smoothly transitions to random colors
 *
 * Creates subtle ambient shifts by slowly fading to a random color,
 * staying for a duration, then fading back to original colors.
 */
class ShiftyExpression : public Expression {
 private:
  Zone zone_;
  uint8_t fillMode_ = 0;
  std::vector<uint32_t> pixelStartFrame_;

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
  uint32_t fadeDurationFrames = 2400;    // 80 seconds at 30fps

  // Runtime state
  uint32_t shiftStartMs = 0;
  uint32_t currentShiftDurationMs = 0;

  // Color buffers for smooth transitions
  std::vector<Color> fadeStartColors;
  std::vector<Color> fadeTargetColors;

  // Per-frame cache (perf): factor from (frame, frames) is identical for every
  // pixel within a fade frame. Hoisted out of the per-pixel hot path in draw().
  // Updated in onUpdate(); read-only in draw().
  uint32_t cachedFadeFactor_ = 0;       // Precomputed linear "factor" (matches easeLinear)
  bool cachedFadeAtEnd_ = false;        // True when frame >= frames (end-clamp short-circuit)


  /**
   * @brief Start shifting to a new color
   */
  void startShift();

  /**
   * @brief Start returning to original colors
   */
  void startUnshift();

  /**
   * @brief Get random shift duration within configured range
   */
  uint32_t getRandomShiftDuration();

  void populatePixelStartFrames(bool fadingBack);

 public:
  using Expression::Expression;

  /**
   * @brief Constructor
   * @param inBuffer Frame buffer to use
   * @param inFrames Initial frame count
   */
  ShiftyExpression(FrameBuffer* inBuffer, uint32_t inFrames = 120);

  /**
   * @brief Configure shifty-specific parameters from generic parameter map
   * @param parameters Map containing expression-specific parameters
   */
  void configureFromParameters(const std::map<std::string, uint32_t>& parameters) override;
  static const ExpressionDescriptor& descriptor();

  void draw() override;

  // Continuous palette-shift animation — visually fights the wisp's
  // hold colour, so must pause while wisp is overriding.
  bool disabledDuringWispOverride() const override { return true; }

protected:
  void onTrigger() override;
  void onUpdate() override;
  void onComplete() override;
};

}  // namespace lamp
