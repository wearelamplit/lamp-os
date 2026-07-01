#include <unity.h>
#include <cstring>
#include "lamp_protocol.hpp"

using namespace lamp_protocol;

static const uint8_t kMac[6] = {0xDE,0xAD,0xBE,0xEF,0x00,0x01};

void setUp() {}
void tearDown() {}

// FIXED_SIZE is insufficient for the v0x05 tlv_count trailer.
void test_fixed_size_buffer_is_insufficient() {
  uint8_t tooSmall[WISP_HELLO_FIXED_SIZE];
  size_t n = buildWispHello(tooSmall, sizeof(tooSmall), 1, kMac,
                            0x00010000u, 0x00, "abcd", 4, nullptr, 0, 0);
  TEST_ASSERT_EQUAL_UINT(0, n);
}

// A correctly-sized buffer yields a 46-byte frame with tlv_count = 0 that
// round-trips through the parser.
void test_max_size_buffer_builds_and_parses() {
  uint8_t buf[WISP_HELLO_MAX_SIZE];
  size_t n = buildWispHello(buf, sizeof(buf), 7, kMac,
                            0x00010000u, 0x02, "abcd", 4, nullptr, 0, 0);
  TEST_ASSERT_EQUAL_UINT(WISP_HELLO_FIXED_SIZE + 1, n);   // 46
  TEST_ASSERT_EQUAL_UINT8(0, buf[WISP_HELLO_FIXED_SIZE]); // tlv_count

  ParsedWispHello out;
  TEST_ASSERT_TRUE(parseWispHello(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(0x02, out.flags);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fixed_size_buffer_is_insufficient);
  RUN_TEST(test_max_size_buffer_builds_and_parses);
  return UNITY_END();
}
