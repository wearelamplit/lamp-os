// Native tests for inverseGamma8: for every byte, the smallest input whose
// gamma8 output is nearest it.

#include <unity.h>

#include <cstdlib>

#include "util/gamma.hpp"

void setUp(void) {}
void tearDown(void) {}

namespace {

int distance(int s, int v) { return std::abs(int(Adafruit_NeoPixel::gamma8(uint8_t(s))) - v); }

}  // namespace

void test_every_byte_maps_to_the_smallest_nearest_input() {
  for (int v = 0; v < 256; ++v) {
    const int s = lamp::inverseGamma8(uint8_t(v));
    for (int other = 0; other < 256; ++other) {
      TEST_ASSERT_TRUE_MESSAGE(distance(other, v) >= distance(s, v), "a nearer input exists");
      if (other < s) {
        TEST_ASSERT_TRUE_MESSAGE(distance(other, v) > distance(s, v), "a smaller input is as near");
      }
    }
  }
}

// gamma8 attains every byte up to 66, and none above is more than one off.
void test_exact_up_to_66_and_within_one_above() {
  for (int v = 0; v < 256; ++v) {
    const int s = lamp::inverseGamma8(uint8_t(v));
    TEST_ASSERT_LESS_OR_EQUAL_INT(v <= 66 ? 0 : 1, distance(s, v));
  }
  TEST_ASSERT_EQUAL_UINT8(0, lamp::inverseGamma8(0));
  TEST_ASSERT_EQUAL_UINT8(255, lamp::inverseGamma8(255));
}

void test_monotone() {
  for (int v = 1; v < 256; ++v) {
    TEST_ASSERT_TRUE(lamp::inverseGamma8(uint8_t(v)) >= lamp::inverseGamma8(uint8_t(v - 1)));
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_byte_maps_to_the_smallest_nearest_input);
  RUN_TEST(test_exact_up_to_66_and_within_one_above);
  RUN_TEST(test_monotone);
  return UNITY_END();
}
