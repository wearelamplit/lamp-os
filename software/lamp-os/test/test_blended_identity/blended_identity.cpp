#include <unity.h>

#include <cstdint>

#include <lampos/blended_identity.hpp>

using lampos::led::blendedIdentity;

namespace {
struct Rgbw {
  uint8_t r, g, b, w;
};
constexpr Rgbw kRed{255, 0, 0, 0};
constexpr Rgbw kBlue{0, 0, 255, 0};
constexpr Rgbw kCyan{0, 255, 255, 0};
}  // namespace

void setUp() {}
void tearDown() {}

void test_single_stop_is_identity() {
  Rgbw stops[] = {{10, 20, 30, 200}};
  Rgbw out = blendedIdentity(stops, 1);
  TEST_ASSERT_EQUAL_UINT8(10, out.r);
  TEST_ASSERT_EQUAL_UINT8(20, out.g);
  TEST_ASSERT_EQUAL_UINT8(30, out.b);
  TEST_ASSERT_EQUAL_UINT8(200, out.w);
}

void test_two_red_one_blue_leans_red() {
  Rgbw stops[] = {kRed, kRed, kBlue};
  Rgbw out = blendedIdentity(stops, 3);
  TEST_ASSERT_TRUE(out.r > out.b);
  TEST_ASSERT_EQUAL_UINT8(0, out.g);
}

void test_complementary_guard_avoids_grey() {
  Rgbw stops[] = {kRed, kCyan};
  Rgbw out = blendedIdentity(stops, 2);
  // Uniform mean is neutral grey; the guard restores chroma along the
  // first stop's (red) hue.
  TEST_ASSERT_TRUE(out.r > out.g);
  TEST_ASSERT_TRUE(out.r > out.b);
}

void test_white_channel_passthrough() {
  Rgbw stops[] = {{10, 20, 30, 200}, {10, 20, 30, 200}};
  Rgbw out = blendedIdentity(stops, 2);
  TEST_ASSERT_EQUAL_UINT8(200, out.w);
  TEST_ASSERT_EQUAL_UINT8(10, out.r);
  TEST_ASSERT_EQUAL_UINT8(20, out.g);
  TEST_ASSERT_EQUAL_UINT8(30, out.b);
}

void test_empty_is_zero() {
  Rgbw out = blendedIdentity<Rgbw>(nullptr, 0);
  TEST_ASSERT_EQUAL_UINT8(0, out.r);
  TEST_ASSERT_EQUAL_UINT8(0, out.g);
  TEST_ASSERT_EQUAL_UINT8(0, out.b);
  TEST_ASSERT_EQUAL_UINT8(0, out.w);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_stop_is_identity);
  RUN_TEST(test_two_red_one_blue_leans_red);
  RUN_TEST(test_complementary_guard_avoids_grey);
  RUN_TEST(test_white_channel_passthrough);
  RUN_TEST(test_empty_is_zero);
  return UNITY_END();
}
