// software/lamp-os/test/test_version_string/version_string.cpp
//
// Native test verifies FIRMWARE_VERSION_STR mirrors LAMP_FW_*.
// Native env lacks build_flags so values are 0/0/0.

#include <unity.h>
#include "version.hpp"

void setUp(void) {}
void tearDown(void) {}

void test_string_matches_components_default() {
  TEST_ASSERT_EQUAL_STRING("0.0.0", lamp::FIRMWARE_VERSION_STR);
}

void test_uint32_packing_default() {
  TEST_ASSERT_EQUAL_UINT32(0, lamp::FIRMWARE_VERSION);
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_string_matches_components_default);
  RUN_TEST(test_uint32_packing_default);
  return UNITY_END();
}
