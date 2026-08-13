// Native tests for the compositor home-mode skip predicate.
// Tests homeModeExpressionSkips from home_mode_gate.hpp directly —
// no Compositor, AnimatedBehavior, or Arduino deps required.

#include <unity.h>

#include <string>
#include <vector>

#include "core/home_mode_gate.hpp"

using namespace lamp;

void setUp(void) {}
void tearDown(void) {}

void test_null_id_not_skipped() {
  TEST_ASSERT_FALSE(homeModeExpressionSkips(nullptr, {"glitchy"}));
}

void test_expression_in_list_skipped() {
  TEST_ASSERT_TRUE(homeModeExpressionSkips("glitchy", {"glitchy"}));
}

void test_expression_not_in_list_not_skipped() {
  TEST_ASSERT_FALSE(homeModeExpressionSkips("shifty", {"glitchy"}));
}

void test_empty_disabled_list_skips_nothing() {
  TEST_ASSERT_FALSE(homeModeExpressionSkips("glitchy", {}));
}

void test_null_id_with_empty_list_not_skipped() {
  TEST_ASSERT_FALSE(homeModeExpressionSkips(nullptr, {}));
}

void test_multiple_disabled_expressions() {
  std::vector<std::string> disabled = {"glitchy", "pulse"};
  TEST_ASSERT_TRUE(homeModeExpressionSkips("glitchy", disabled));
  TEST_ASSERT_TRUE(homeModeExpressionSkips("pulse", disabled));
  TEST_ASSERT_FALSE(homeModeExpressionSkips("shifty", disabled));
  TEST_ASSERT_FALSE(homeModeExpressionSkips("breathing", disabled));
}

void test_case_sensitive_match() {
  TEST_ASSERT_FALSE(homeModeExpressionSkips("Glitchy", {"glitchy"}));
  TEST_ASSERT_FALSE(homeModeExpressionSkips("GLITCHY", {"glitchy"}));
}

// Mirror of Compositor::homeModeSkipsType: the received-cascade drop decision.
// A cascade is dropped at receipt only when home mode is ON and the type is
// in the disabled set; otherwise it creates + renders as normal.
static bool receivedCascadeDropped(bool homeMode, const char* type,
                                   const std::vector<std::string>& disabled) {
  return homeMode && homeModeExpressionSkips(type, disabled);
}

void test_received_cascade_home_off_never_dropped() {
  TEST_ASSERT_FALSE(receivedCascadeDropped(false, "glitchy", {"glitchy"}));
  TEST_ASSERT_FALSE(receivedCascadeDropped(false, "shifty", {"glitchy"}));
}

void test_received_cascade_home_on_enabled_type_kept() {
  TEST_ASSERT_FALSE(receivedCascadeDropped(true, "shifty", {"glitchy"}));
}

void test_received_cascade_home_on_disabled_type_dropped() {
  TEST_ASSERT_TRUE(receivedCascadeDropped(true, "glitchy", {"glitchy"}));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_null_id_not_skipped);
  RUN_TEST(test_expression_in_list_skipped);
  RUN_TEST(test_expression_not_in_list_not_skipped);
  RUN_TEST(test_empty_disabled_list_skips_nothing);
  RUN_TEST(test_null_id_with_empty_list_not_skipped);
  RUN_TEST(test_multiple_disabled_expressions);
  RUN_TEST(test_case_sensitive_match);
  RUN_TEST(test_received_cascade_home_off_never_dropped);
  RUN_TEST(test_received_cascade_home_on_enabled_type_kept);
  RUN_TEST(test_received_cascade_home_on_disabled_type_dropped);
  return UNITY_END();
}
