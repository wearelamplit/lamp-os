#include <unity.h>

#include <map>
#include <string>

#include "expressions/expression_registry.hpp"

using namespace lamp;

static constexpr EnumOption kLoopOpts[] = {{0, "Trigger"}, {1, "Continuous"}};
static constexpr ParamSpec kLoopParams[] = {
  { .key = kLoopParamKey, .kind = ParamKind::Enum, .label = "Loop", .max = 1,
    .options = kLoopOpts },
};
static constexpr ExpressionDescriptor kWithLoop{
  .id = "loopy", .name = "Loopy", .continuous = false, .params = kLoopParams };
static constexpr ExpressionDescriptor kContinuousNoLoop{
  .id = "cont", .name = "Cont", .continuous = true };
static constexpr ExpressionDescriptor kOneShotNoLoop{
  .id = "one", .name = "One", .continuous = false };

void setUp() {}
void tearDown() {}

void test_loop_param_continuous_is_continuous() {
  std::map<std::string, uint32_t> p = {{"loop", 1}};
  TEST_ASSERT_TRUE(effectiveContinuous(kWithLoop, p));
}

void test_loop_param_trigger_is_not_continuous() {
  std::map<std::string, uint32_t> p = {{"loop", 0}};
  TEST_ASSERT_FALSE(effectiveContinuous(kWithLoop, p));
}

void test_loop_param_absent_defaults_to_trigger() {
  std::map<std::string, uint32_t> p;
  TEST_ASSERT_FALSE(effectiveContinuous(kWithLoop, p));
}

void test_no_loop_param_uses_descriptor_continuous() {
  std::map<std::string, uint32_t> p;
  TEST_ASSERT_TRUE(effectiveContinuous(kContinuousNoLoop, p));
  TEST_ASSERT_FALSE(effectiveContinuous(kOneShotNoLoop, p));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_loop_param_continuous_is_continuous);
  RUN_TEST(test_loop_param_trigger_is_not_continuous);
  RUN_TEST(test_loop_param_absent_defaults_to_trigger);
  RUN_TEST(test_no_loop_param_uses_descriptor_continuous);
  return UNITY_END();
}
