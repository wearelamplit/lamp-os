// Native-host test for lamp::resolveConfiguredPx — the replacement for
// the old applyDefaults magic-number guard
// (`if (shade.px == 32 || shade.px == 38) shade.px = d.shadePx;`).
//
// A persisted px of 0 means the field was absent from NVS (fresh lamp);
// fill from the variant default. ANY stored value wins — including one
// that equals a former factory baseline (32/38 shade, 35/36 base) which
// the old guard wrongly clobbered.

#include <unity.h>

#include "config/config_types.hpp"

void setUp(void) {}
void tearDown(void) {}

void test_absent_falls_back_to_default() {
  TEST_ASSERT_EQUAL_UINT8(32, lamp::resolveConfiguredPx(0, 32));
}

void test_stored_value_wins() {
  TEST_ASSERT_EQUAL_UINT8(40, lamp::resolveConfiguredPx(40, 32));
}

void test_stored_baseline_value_is_not_clobbered() {
  // The bug the old guard caused: a user who legitimately saves 32 (or
  // 38) must keep it, not get reset to the variant default.
  TEST_ASSERT_EQUAL_UINT8(32, lamp::resolveConfiguredPx(32, 50));
  TEST_ASSERT_EQUAL_UINT8(38, lamp::resolveConfiguredPx(38, 50));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_absent_falls_back_to_default);
  RUN_TEST(test_stored_value_wins);
  RUN_TEST(test_stored_baseline_value_is_not_clobbered);
  return UNITY_END();
}
