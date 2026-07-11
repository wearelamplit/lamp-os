#pragma once
#include <vector>
#include "expressions/expression.hpp"
#include "expressions/expression_schema.hpp"
#include "expressions/primitives.hpp"

namespace lamp {

class SpottyExpression : public Expression {
 public:
  using Expression::Expression;
  SpottyExpression(FrameBuffer* inBuffer, uint32_t inFrames = 90);
  void configureFromParameters(const std::map<std::string, uint32_t>& parameters) override;
  static const ExpressionDescriptor& descriptor();
  void draw() override;
  bool disabledDuringWispOverride() const override { return true; }

 protected:
  void onTrigger() override;

 private:
  Zone zone_;
  uint16_t points_ = 1;
  uint16_t size_ = 4;
  uint32_t lifeFrames_ = 90;  // per-spot fade-in/hold/fade-out span, scaled by spotSpeed
  std::vector<uint16_t> pointPositions_;
  std::vector<Color> pointColors_;
};

}  // namespace lamp
