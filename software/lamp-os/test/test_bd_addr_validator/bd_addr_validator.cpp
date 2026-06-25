// Native-host unit tests for the BD_ADDR canonical-form validator.
//
// Context: the per-peer disposition NVS blob is silently filtered on
// load to drop any key that isn't canonical-form BD_ADDR
// ("AA:BB:CC:DD:EE:FF" — 17 chars, uppercase or lowercase hex,
// colons at positions 2/5/8/11/14). This is the "no migration"
// strategy: pre-Phase-C lamps had name-keyed entries; on first boot
// of Phase-C firmware those entries fail validation and disappear.
//
// The predicate has zero dependencies (no Arduino, no NimBLE) so it
// lives in a single header `src/util/bd_addr.hpp` that links cleanly
// into both the production build and this native test env.

#include <unity.h>

#include "util/bd_addr.hpp"

void setUp(void) {}
void tearDown(void) {}

void test_canonical_uppercase_accepted() {
  TEST_ASSERT_TRUE(lamp::isValidBdAddr("AA:BB:CC:DD:EE:FF"));
  TEST_ASSERT_TRUE(lamp::isValidBdAddr("00:00:00:00:00:00"));
  TEST_ASSERT_TRUE(lamp::isValidBdAddr("FF:FF:FF:FF:FF:FF"));
}

void test_canonical_lowercase_accepted() {
  // Real devices sometimes emit lowercase; accept for forward compat.
  // We canonicalize to uppercase at the BLE scan callback (bluetooth.cpp)
  // but a third-party app could write lowercase via CHAR_SOCIAL_DISPOSITIONS.
  TEST_ASSERT_TRUE(lamp::isValidBdAddr("aa:bb:cc:dd:ee:ff"));
  TEST_ASSERT_TRUE(lamp::isValidBdAddr("a0:b1:c2:d3:e4:f5"));
}

void test_mixed_case_accepted() {
  // No reason to reject. Real-world data is messy.
  TEST_ASSERT_TRUE(lamp::isValidBdAddr("Aa:Bb:Cc:Dd:Ee:Ff"));
}

void test_name_strings_rejected() {
  // Pre-Phase-C disposition keys. These are the ones we want to silently
  // drop on NVS load. None of them are 17 chars with colons at the right
  // positions.
  TEST_ASSERT_FALSE(lamp::isValidBdAddr("jacko"));
  TEST_ASSERT_FALSE(lamp::isValidBdAddr("floral"));
  TEST_ASSERT_FALSE(lamp::isValidBdAddr(""));
  TEST_ASSERT_FALSE(lamp::isValidBdAddr("anonymous-lamp"));
  TEST_ASSERT_FALSE(lamp::isValidBdAddr("a-name-that-is-exactly-17ch"));  // > 17
}

void test_wrong_length_rejected() {
  TEST_ASSERT_FALSE(lamp::isValidBdAddr("AA:BB:CC:DD:EE:F"));    // 16
  TEST_ASSERT_FALSE(lamp::isValidBdAddr("AA:BB:CC:DD:EE:FF0"));  // 18
  TEST_ASSERT_FALSE(lamp::isValidBdAddr("AA:BB:CC:DD:EE"));      // 14
}

void test_wrong_separator_positions_rejected() {
  // Colons at the wrong indices — e.g., dashes (Windows-style) or no
  // separators at all.
  TEST_ASSERT_FALSE(lamp::isValidBdAddr("AA-BB-CC-DD-EE-FF"));
  TEST_ASSERT_FALSE(lamp::isValidBdAddr("AABBCCDDEEFF:::::"));
  TEST_ASSERT_FALSE(lamp::isValidBdAddr("A:ABB:CC:DD:EE:FF"));  // shifted
}

void test_non_hex_characters_rejected() {
  // Letters g-z and any punctuation in a hex-pair slot.
  TEST_ASSERT_FALSE(lamp::isValidBdAddr("GG:BB:CC:DD:EE:FF"));
  TEST_ASSERT_FALSE(lamp::isValidBdAddr("AA:BB:CC:DD:EE:Xy"));
  TEST_ASSERT_FALSE(lamp::isValidBdAddr("AA:BB:CC:DD:EE:F!"));
  TEST_ASSERT_FALSE(lamp::isValidBdAddr("  :BB:CC:DD:EE:FF"));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_canonical_uppercase_accepted);
  RUN_TEST(test_canonical_lowercase_accepted);
  RUN_TEST(test_mixed_case_accepted);
  RUN_TEST(test_name_strings_rejected);
  RUN_TEST(test_wrong_length_rejected);
  RUN_TEST(test_wrong_separator_positions_rejected);
  RUN_TEST(test_non_hex_characters_rejected);

  return UNITY_END();
}
