#include <unity.h>

#include "../../src/core/animated_behavior.cpp"
#include "../../src/expressions/expression.cpp"

using namespace lamp;

namespace lamp {
// Probe never triggers, but expression.cpp's trigger()/shouldAffectBuffer()
// still need these production globals to link.
OverrideAggregate overrides;
void ExpressionManager::onExpressionFired(Expression*) {}
}  // namespace lamp

namespace {
class Probe : public Expression {
 public:
  using Expression::Expression;
  const ExpressionDescriptor& descriptor() const override { return d_; }
  void configureFromParameters(const std::map<std::string, uint32_t>& p) override {
    configureOpacity(p);
  }
  uint8_t opacity() const { return opacityPct_; }
  void draw() override {}

  // Forward to the base easing helpers under test.
  void configureEasingParam(const std::map<std::string, uint32_t>& p) { configureEasing(p, 0); }
  Easing currentEasing() const { return easing_; }
  void rollEasing() { if (easingRaw_ == (uint32_t)Easing::Random) easing_ = randomEasing(rng); }
 protected:
  void onTrigger() override {}
 private:
  ExpressionDescriptor d_{};
};
}  // namespace

void test_opacity_default_is_100() {
  Probe e(nullptr, 1);
  e.configureFromParameters({});
  TEST_ASSERT_EQUAL_UINT8(100, e.opacity());
}

void test_opacity_reads_param() {
  Probe e(nullptr, 1);
  e.configureFromParameters({{"opacity", 40}});
  TEST_ASSERT_EQUAL_UINT8(40, e.opacity());
}

void test_opacity_clamps_over_100() {
  Probe e(nullptr, 1);
  e.configureFromParameters({{"opacity", 250}});
  TEST_ASSERT_EQUAL_UINT8(100, e.opacity());
}

void test_opacity_floors_under_10() {
  Probe e(nullptr, 1);
  e.configureFromParameters({{"opacity", 0}});
  TEST_ASSERT_EQUAL_UINT8(10, e.opacity());
  e.configureFromParameters({{"opacity", 3}});
  TEST_ASSERT_EQUAL_UINT8(10, e.opacity());
}

void test_random_easing_resolves_to_concrete() {
  Probe e(nullptr, 1);
  e.configureEasingParam({{"easing", (uint32_t)Easing::Random}});
  TEST_ASSERT_TRUE(e.currentEasing() != Easing::Random);
  e.rollEasing();
  TEST_ASSERT_TRUE(e.currentEasing() != Easing::Random);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_opacity_default_is_100);
  RUN_TEST(test_opacity_reads_param);
  RUN_TEST(test_opacity_clamps_over_100);
  RUN_TEST(test_opacity_floors_under_10);
  RUN_TEST(test_random_easing_resolves_to_concrete);
  return UNITY_END();
}
