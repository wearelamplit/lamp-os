// A person's expression upsert: which types and targets are admitted, and
// what reaches the saved settings.
#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "expressions/bloom/bloom_expression.hpp"
#include "expressions/expr_variant_sets.hpp"
#include "expressions/breathing/breathing_expression.hpp"
#include "expressions/glitchy/glitchy_expression.hpp"
#include "expressions/pulse/pulse_expression.hpp"
#include "expressions/shifty/shifty_expression.hpp"
#include "expressions/shimmer/shimmer_expression.hpp"
#include "expressions/spotty/spotty_expression.hpp"
#include "expressions/yawn/yawn_expression.hpp"
#include "expressions/expression_upsert.hpp"

#include "../../src/util/color.cpp"
#include "../../src/config/config_codec.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/util/json_writer.cpp"
#include "../../src/expressions/expression_registry.cpp"
#include "../../src/expressions/expression_upsert.cpp"

using namespace lamp;

namespace {

// The staff set: the base set plus bloom, so both internal types are registered.
ExpressionRegistry staffRegistry() {
  ExpressionRegistry reg;
#define X(N) reg.add(k##N##DescriptorData);
  LAMPOS_STAFF_EXPRESSIONS(X)
#undef X
  return reg;
}

ExpressionConfig entry(const char* type, uint8_t target) {
  ExpressionConfig c;
  c.type = type;
  c.target = target;
  return c;
}

ExpressionSettings savedPulse() {
  ExpressionSettings s;
  s.expressions.push_back(entry("pulse", TARGET_SHADE));
  return s;
}

}  // namespace

void setUp() {}
void tearDown() {}

// An app or Web Config upsert of yawn or bloom is refused on either path and
// never reaches the saved settings.
void test_internal_type_upsert_is_refused_and_unsaved() {
  const ExpressionRegistry reg = staffRegistry();
  for (const char* type : {kYawnDescriptorData.id, kBloomDescriptorData.id}) {
    ExpressionSettings saved = savedPulse();
    TEST_ASSERT_TRUE(admitUpsert(reg, entry(type, TARGET_BOTH), &saved) ==
                     UpsertVerdict::NotConfigurable);
    TEST_ASSERT_TRUE(admitUpsert(reg, entry(type, TARGET_BOTH), nullptr) ==
                     UpsertVerdict::NotConfigurable);
    TEST_ASSERT_EQUAL_size_t(1, saved.expressions.size());
    TEST_ASSERT_EQUAL_STRING("pulse", saved.expressions[0].type.c_str());
  }
}

void test_unregistered_type_upsert_is_refused() {
  ExpressionSettings saved = savedPulse();
  TEST_ASSERT_TRUE(admitUpsert(staffRegistry(), entry("nope", TARGET_BASE), &saved) ==
                   UpsertVerdict::NotConfigurable);
  TEST_ASSERT_EQUAL_size_t(1, saved.expressions.size());
}

void test_out_of_range_target_is_refused() {
  ExpressionSettings saved = savedPulse();
  const ExpressionRegistry reg = staffRegistry();
  for (uint8_t target : {uint8_t(kExpressionTargetMin - 1), uint8_t(kExpressionTargetMax + 1)}) {
    TEST_ASSERT_TRUE(admitUpsert(reg, entry("glitchy", target), &saved) ==
                     UpsertVerdict::BadTarget);
  }
  TEST_ASSERT_EQUAL_size_t(1, saved.expressions.size());
}

// A catalog type is admitted: mirrored when there are settings, live when not.
void test_catalog_type_upsert_is_saved_or_live() {
  const ExpressionRegistry reg = staffRegistry();
  ExpressionSettings saved = savedPulse();
  TEST_ASSERT_TRUE(admitUpsert(reg, entry("glitchy", TARGET_BASE), &saved) ==
                   UpsertVerdict::Saved);
  TEST_ASSERT_EQUAL_size_t(2, saved.expressions.size());
  TEST_ASSERT_EQUAL_STRING("glitchy", saved.expressions[1].type.c_str());
  TEST_ASSERT_TRUE(admitUpsert(reg, entry("breathing", TARGET_SHADE), nullptr) ==
                   UpsertVerdict::Live);
}

void test_full_settings_admit_unsaved() {
  const ExpressionRegistry reg = staffRegistry();
  ExpressionSettings saved;
  for (size_t i = 0; i < kMaxExpressions; ++i) {
    saved.expressions.push_back(entry("pulse", static_cast<uint8_t>(100 + i)));
  }
  TEST_ASSERT_TRUE(admitUpsert(reg, entry("glitchy", TARGET_BASE), &saved) ==
                   UpsertVerdict::Unsaved);
  TEST_ASSERT_EQUAL_size_t(kMaxExpressions, saved.expressions.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_internal_type_upsert_is_refused_and_unsaved);
  RUN_TEST(test_unregistered_type_upsert_is_refused);
  RUN_TEST(test_out_of_range_target_is_refused);
  RUN_TEST(test_catalog_type_upsert_is_saved_or_live);
  RUN_TEST(test_full_settings_admit_unsaved);
  return UNITY_END();
}
