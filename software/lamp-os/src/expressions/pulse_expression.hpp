#ifndef LAMP_EXPRESSIONS_PULSE_H
#define LAMP_EXPRESSIONS_PULSE_H

#include "./expression.hpp"

namespace lamp {

/**
 * @brief Pulse expression - creates a vertical wave that travels through the strips
 *
 * Creates a smooth pulse effect that moves vertically, with configurable
 * speed and colors. The pulse blends with existing colors for a layered effect.
 */
class PulseExpression : public Expression {
 private:
  // Wave state
  float wavePosition = 0.0f;      // Current position of wave center (in pixels)
  float waveSpeed = 10.0f;        // Speed in pixels per second
  int waveDirection = 1;          // 1 for up, -1 for down

  // Configuration
  uint32_t pulseSpeedMs = 100;    // Time for wave to move one pixel (ms)
  uint32_t pulseWidth = 3;        // Fade radius in pixels on each side
  Color pulseColor;                // Current pulse color

  // Timing
  uint32_t lastUpdateMs = 0;      // Last time wave position was updated

  /**
   * @brief Calculate blend factor for a pixel based on distance from wave center
   * @param pixelIndex Index of the pixel
   * @return Blend factor (0 to 100, representing 0% to 100%)
   */
  uint32_t calculateBlendFactor(int pixelIndex) const;

  /**
   * @brief Update wave position based on elapsed time
   */
  void updateWavePosition();

  /**
   * @brief Select next pulse color from palette
   */
  void selectNextColor();

 public:
  using Expression::Expression;

  /**
   * @brief Constructor
   * @param inBuffer Frame buffer to use
   * @param inFrames Animation duration (not used for continuous pulse)
   */
  PulseExpression(FrameBuffer* inBuffer, uint32_t inFrames = 60);

  /**
   * @brief Configure pulse-specific parameters from generic parameter map
   * @param parameters Map containing expression-specific parameters
   */
  void configureFromParameters(const std::map<std::string, uint32_t>& parameters);

  void draw() override;

protected:
  void onTrigger() override;
  void onUpdate() override;
};

}  // namespace lamp

#endif