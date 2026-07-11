#include <unity.h>

#include "core/behavior_context.hpp"

void setUp(void) {}
void tearDown(void) {}

void test_new_pointers_default_to_null() {
  lamp::BehaviorContext ctx;
  TEST_ASSERT_NULL(ctx.nearbyLamps);
  TEST_ASSERT_NULL(ctx.greeting);
}

void test_existing_pointers_still_default_to_null() {
  lamp::BehaviorContext ctx;
  TEST_ASSERT_NULL(ctx.compositor);
  TEST_ASSERT_NULL(ctx.expressionManager);
  TEST_ASSERT_NULL(ctx.baseConfigurator);
  TEST_ASSERT_NULL(ctx.shadeConfigurator);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_new_pointers_default_to_null);
  RUN_TEST(test_existing_pointers_still_default_to_null);
  return UNITY_END();
}
