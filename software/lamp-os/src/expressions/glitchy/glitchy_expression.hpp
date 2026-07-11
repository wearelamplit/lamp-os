#pragma once

#include "expressions/expression.hpp"
#include "expressions/expression_schema.hpp"
#include "expressions/primitives.hpp"

namespace lamp {

/**
 * @brief Glitchy expression - creates random visual glitches
 */
class GlitchyExpression : public Expression {
 private:
  Color glitchColor;
  uint32_t glitchDurationMin = 1;  // minimum frames
  uint32_t glitchDurationMax = 3;  // maximum frames
  Zone zone_;
  uint16_t points_ = 1;
  uint16_t size_ = 1;
  bool fullStrip_ = true;

  void paintPoints_();

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
  void configureFromParameters(const std::map<std::string, uint32_t>& parameters) override;
  static const ExpressionDescriptor& descriptor();

  void draw() override;

protected:
  void onTrigger() override;
  void onComplete() override;
};

}  // namespace lamp
