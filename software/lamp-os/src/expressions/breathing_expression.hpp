#pragma once

#include "expression.hpp"

namespace lamp {

/**
 * @brief Breathing expression - creates a continuous breathing effect
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

  // Per-frame cache (perf): (breathIntensity, 100) → factor is identical for
  // every pixel within a frame. Hoist it out of the per-pixel hot path in
  // draw(). Updated in onUpdate(); read-only in draw().
  uint32_t cachedFadeFactor_ = 0;      // Precomputed linear "factor" (matches easeLinear)
  bool cachedFadeAtEnd_ = false;       // True when breathIntensity >= 100 (end-clamp short-circuit)

  // Color cycling state (for multiple colors)
  uint32_t currentColorIndex = 0;      // Current color in the sequence
  bool cyclingForward = true;          // Direction of color cycling

  /**
   * @brief Update breath phase based on elapsed time
   */
  void updateBreathPhase();

 public:
  using Expression::Expression;

  /**
   * @brief Constructor
   * @param inBuffer Frame buffer to use
   * @param inFrames Animation duration (not used for continuous breathing)
   */
  BreathingExpression(FrameBuffer* inBuffer, uint32_t inFrames = 60);

  /**
   * @brief Configure breathing-specific parameters from generic parameter map
   * @param parameters Map containing expression-specific parameters
   */
  void configureFromParameters(const std::map<std::string, uint32_t>& parameters);

  void draw() override;
  void control() override;  // Override to keep always running

  // Continuous fade between palette colours — visually fights the wisp's
  // hold colour, so must pause while wisp is overriding.
  bool disabledDuringWispOverride() const override { return true; }

protected:
  void onTrigger() override;
  void onUpdate() override;
};

}  // namespace lamp
