#include <unity.h>
#include <cstring>
#include "wire/lamp_protocol.hpp"

using namespace lamp_protocol;

static const uint8_t kSrcMac[6]  = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
static const uint8_t kLampA[6]   = {0x11, 0x22, 0x33, 0x44, 0x55, 0x01};
static const uint8_t kLampB[6]   = {0x11, 0x22, 0x33, 0x44, 0x55, 0x02};
static const uint8_t kBaseA[3]   = {0xFF, 0x80, 0x10};
static const uint8_t kShadeA[3]  = {0x10, 0x20, 0x30};
static const uint8_t kBaseB[3]   = {0x01, 0x02, 0x03};
static const uint8_t kShadeB[3]  = {0x04, 0x05, 0x06};

void setUp() {}
void tearDown() {}

// 2-entry round-trip: build→parse, verify macs + both RGB triples exact.
void test_roundtrip_two_entries(void) {
  uint8_t entries[2 * WISP_PAINT_ENTRY_SIZE];
  std::memcpy(&entries[0],  kLampA,  6);
  std::memcpy(&entries[6],  kBaseA,  3);
  std::memcpy(&entries[9],  kShadeA, 3);
  std::memcpy(&entries[12], kLampB,  6);
  std::memcpy(&entries[18], kBaseB,  3);
  std::memcpy(&entries[21], kShadeB, 3);

  uint8_t buf[WISP_PAINT_MAX_SIZE];
  size_t n = buildWispPaint(buf, sizeof(buf), /*seq=*/42, kSrcMac, entries, 2);
  TEST_ASSERT_EQUAL_UINT(WISP_PAINT_FIXED_PREFIX + 2 * WISP_PAINT_ENTRY_SIZE, n);

  ParsedWispPaint out;
  TEST_ASSERT_TRUE(parseWispPaint(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(42, out.seq);
  TEST_ASSERT_EQUAL_UINT8(2, out.count);
  TEST_ASSERT_EQUAL_MEMORY(kSrcMac, out.sourceMac, 6);

  // Entry 0: lampMac + baseRGB + shadeRGB.
  TEST_ASSERT_EQUAL_MEMORY(kLampA,  out.entries,      6);
  TEST_ASSERT_EQUAL_MEMORY(kBaseA,  out.entries + 6,  3);
  TEST_ASSERT_EQUAL_MEMORY(kShadeA, out.entries + 9,  3);
  // Entry 1.
  TEST_ASSERT_EQUAL_MEMORY(kLampB,  out.entries + 12, 6);
  TEST_ASSERT_EQUAL_MEMORY(kBaseB,  out.entries + 18, 3);
  TEST_ASSERT_EQUAL_MEMORY(kShadeB, out.entries + 21, 3);
}

// count > WISP_PAINT_MAX_ENTRIES → build returns 0.
void test_build_rejects_overflow_count(void) {
  uint8_t entries[1] = {0};
  uint8_t buf[WISP_PAINT_MAX_SIZE];
  size_t n = buildWispPaint(buf, sizeof(buf), 1, kSrcMac, entries,
                            WISP_PAINT_MAX_ENTRIES + 1);
  TEST_ASSERT_EQUAL_UINT(0, n);
}

// parse rejects a truncated buffer.
void test_parse_rejects_truncated(void) {
  uint8_t entries[2 * WISP_PAINT_ENTRY_SIZE] = {};
  uint8_t buf[WISP_PAINT_MAX_SIZE];
  size_t n = buildWispPaint(buf, sizeof(buf), 1, kSrcMac, entries, 2);
  TEST_ASSERT_NOT_EQUAL(0, n);

  ParsedWispPaint out;
  // One byte short of what count=2 requires.
  TEST_ASSERT_FALSE(parseWispPaint(buf, n - 1, out));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_roundtrip_two_entries);
  RUN_TEST(test_build_rejects_overflow_count);
  RUN_TEST(test_parse_rejects_truncated);
  return UNITY_END();
}
