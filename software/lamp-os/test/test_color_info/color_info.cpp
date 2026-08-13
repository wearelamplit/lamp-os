#include <unity.h>
#include <cstring>
#include "components/network/protocol/color_info.hpp"

namespace lp = lamp_protocol;

static const uint8_t kSrc[6]    = {0x01,0x02,0x03,0x04,0x05,0x06};
static const uint8_t kTarget[6] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};

void setUp(void) {}
void tearDown(void) {}

void test_query_roundtrip() {
  uint8_t buf[lp::COLOR_QUERY_SIZE];
  const size_t n = lp::buildColorQuery(buf, sizeof(buf), 0x1234, kSrc, kTarget);
  TEST_ASSERT_EQUAL_UINT32(lp::COLOR_QUERY_SIZE, n);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_COLOR_QUERY, lp::inspect(buf, n));

  lp::ParsedColorQuery out;
  TEST_ASSERT_TRUE(lp::parseColorQuery(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(0x1234, out.seq);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kSrc, out.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kTarget, out.targetMac, 6);
}

void test_info_roundtrip_base_and_shade() {
  const uint8_t base[]  = {10,20,30,40,  50,60,70,80};        // 2 stops
  const uint8_t shade[] = {1,2,3,4,  5,6,7,8,  9,10,11,12};  // 3 stops
  uint8_t buf[lp::COLOR_INFO_MAX_SIZE];
  const size_t n = lp::buildColorInfo(buf, sizeof(buf), 0x2222, kSrc, kTarget,
                                      base, 2, shade, 3);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_COLOR_INFO, lp::inspect(buf, n));

  lp::ParsedColorInfo out;
  TEST_ASSERT_TRUE(lp::parseColorInfo(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(0x2222, out.seq);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kSrc, out.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kTarget, out.targetMac, 6);
  TEST_ASSERT_EQUAL_UINT8(2, out.baseCount);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(base, out.baseStops, 2*4);
  TEST_ASSERT_EQUAL_UINT8(3, out.shadeCount);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(shade, out.shadeStops, 3*4);
}

void test_info_zero_stops_ok() {
  uint8_t buf[lp::COLOR_INFO_MAX_SIZE];
  const size_t n = lp::buildColorInfo(buf, sizeof(buf), 1, kSrc, kTarget,
                                      nullptr, 0, nullptr, 0);
  TEST_ASSERT_TRUE(n > 0);
  lp::ParsedColorInfo out;
  TEST_ASSERT_TRUE(lp::parseColorInfo(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(0, out.baseCount);
  TEST_ASSERT_EQUAL_UINT8(0, out.shadeCount);
}

void test_info_rejects_over_max_stops() {
  uint8_t big[9*4] = {0};
  uint8_t buf[lp::COLOR_INFO_MAX_SIZE];
  const size_t n = lp::buildColorInfo(buf, sizeof(buf), 1, kSrc, kTarget,
                                      big, 9, nullptr, 0);
  TEST_ASSERT_EQUAL_UINT32(0, n);  // 9 > COLOR_INFO_MAX_STOPS
}

void test_info_parse_rejects_truncated() {
  const uint8_t base[] = {10,20,30,40};
  uint8_t buf[lp::COLOR_INFO_MAX_SIZE];
  const size_t n = lp::buildColorInfo(buf, sizeof(buf), 1, kSrc, kTarget,
                                      base, 1, nullptr, 0);
  lp::ParsedColorInfo out;
  TEST_ASSERT_FALSE(lp::parseColorInfo(buf, n - 1, out));  // one byte short
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_query_roundtrip);
  RUN_TEST(test_info_roundtrip_base_and_shade);
  RUN_TEST(test_info_zero_stops_ok);
  RUN_TEST(test_info_rejects_over_max_stops);
  RUN_TEST(test_info_parse_rejects_truncated);
  return UNITY_END();
}
