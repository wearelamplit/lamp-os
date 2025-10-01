#ifndef LAMP_EXPRESSIONS_GLITCHY_H
#define LAMP_EXPRESSIONS_GLITCHY_H

#include "./expression.hpp"

namespace lamp {

/**
 * @brief Glitchy expression - creates random visual glitches
 */
class GlitchyExpression : public Expression {
 private:
  Color glitchColor;
  uint32_t glitchDurationMin = 1;  // minimum frames
  uint32_t glitchDurationMax = 3;  // maximum frames

 public:
  using Expression::Expression;

  /**
   * @brief Constructor
   * @param inBuffer Frame buffer to use
   * @param inFrames Initial frame count
   */
  GlitchyExpression(FrameBuffer* inBuffer, uint32_t inFrames = 3);

  /**
   * @brief Configure glitchy-specific parameters from generic parameter map
   * @param parameters Map containing expression-specific parameters
   */
  void configureFromParameters(const std::map<std::string, uint32_t>& parameters);

  void draw() override;

protected:
  void onTrigger() override;
};

}  // namespace lamp

#endif