#include <unity.h>

#include <string>
#include <vector>

#include "util/color.hpp"

#include "../../src/util/color.cpp"

void setUp(void) {}
void tearDown(void) {}

void test_single_hex_yields_one_element() {
  auto v = lamp::parseColorList("#30078300");
  TEST_ASSERT_EQUAL_UINT(1, v.size());
  TEST_ASSERT_EQUAL_UINT8(0x30, v[0].r);
  TEST_ASSERT_EQUAL_UINT8(0x07, v[0].g);
  TEST_ASSERT_EQUAL_UINT8(0x83, v[0].b);
  TEST_ASSERT_EQUAL_UINT8(0x00, v[0].w);
}

void test_two_hex_yields_two_elements() {
  auto v = lamp::parseColorList("#30078300,#64149600");
  TEST_ASSERT_EQUAL_UINT(2, v.size());
  TEST_ASSERT_EQUAL_UINT8(0x30, v[0].r);
  TEST_ASSERT_EQUAL_UINT8(0x64, v[1].r);
}

void test_four_hex_yields_four_elements() {
  auto v = lamp::parseColorList("#962d0000,#097d0400,#003c7800,#78003200");
  TEST_ASSERT_EQUAL_UINT(4, v.size());
  TEST_ASSERT_EQUAL_UINT8(0x96, v[0].r);
  TEST_ASSERT_EQUAL_UINT8(0x09, v[1].r);
  TEST_ASSERT_EQUAL_UINT8(0x00, v[2].r);
  TEST_ASSERT_EQUAL_UINT8(0x78, v[3].r);
}

void test_five_hex_yields_five_elements() {
  auto v = lamp::parseColorList("#9b000000,#965a0000,#407a0000,#00647800,#7a004e00");
  TEST_ASSERT_EQUAL_UINT(5, v.size());
}

void test_spaces_around_tokens_are_trimmed() {
  auto v = lamp::parseColorList(" #30078300 , #64149600 ");
  TEST_ASSERT_EQUAL_UINT(2, v.size());
  TEST_ASSERT_EQUAL_UINT8(0x30, v[0].r);
  TEST_ASSERT_EQUAL_UINT8(0x64, v[1].r);
}

void test_empty_string_yields_empty_vector() {
  auto v = lamp::parseColorList("");
  TEST_ASSERT_EQUAL_UINT(0, v.size());
}

void test_whitespace_only_token_is_skipped() {
  auto v = lamp::parseColorList("#30078300, ");
  TEST_ASSERT_EQUAL_UINT(1, v.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_hex_yields_one_element);
  RUN_TEST(test_two_hex_yields_two_elements);
  RUN_TEST(test_four_hex_yields_four_elements);
  RUN_TEST(test_five_hex_yields_five_elements);
  RUN_TEST(test_spaces_around_tokens_are_trimmed);
  RUN_TEST(test_empty_string_yields_empty_vector);
  RUN_TEST(test_whitespace_only_token_is_skipped);
  return UNITY_END();
}
