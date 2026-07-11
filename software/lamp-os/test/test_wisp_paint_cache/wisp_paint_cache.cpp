// Native-host unit tests for NearbyLamps::cacheWispPaint + findWispPaint.
//
// Mirrors the production logic inline (no Arduino deps) to verify:
//   - 2-entry round-trip: each lamp MAC retrieves its exact base/shade RGB.
//   - Unknown MAC returns false.
//   - Entries age out when nowMs - lastPaintMs > kWispClaimStaleMs.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// Must match wisp_cache.cpp's constant.
constexpr uint32_t kWispClaimStaleMs = 60000;
constexpr size_t WISP_PAINT_ENTRY_SIZE = 12;
constexpr size_t WISP_PAINT_MAX_ENTRIES = 18;

struct PaintEntry {
  uint8_t mac[6] = {0};
  uint8_t base[3] = {0};
  uint8_t shade[3] = {0};
  bool valid = false;
};

// Minimal inline mirror of the production cacheWispPaint / findWispPaint logic.
class PaintCache {
 public:
  void cacheWispPaint(const uint8_t* entries, uint8_t count, uint32_t nowMs) {
    const uint8_t safeCount =
        count > WISP_PAINT_MAX_ENTRIES
            ? static_cast<uint8_t>(WISP_PAINT_MAX_ENTRIES)
            : count;
    count_ = safeCount;
    lastPaintMs_ = nowMs;
    for (uint8_t i = 0; i < safeCount; ++i) {
      const uint8_t* e = entries + static_cast<size_t>(i) * WISP_PAINT_ENTRY_SIZE;
      std::memcpy(entries_[i].mac, e, 6);
      std::memcpy(entries_[i].base, e + 6, 3);
      std::memcpy(entries_[i].shade, e + 9, 3);
      entries_[i].valid = true;
    }
    for (uint8_t i = safeCount; i < WISP_PAINT_MAX_ENTRIES; ++i) {
      entries_[i].valid = false;
    }
  }

  bool findWispPaint(const uint8_t lampMac[6], uint32_t nowMs,
                     uint8_t baseOut[3], uint8_t shadeOut[3]) {
    if (lastPaintMs_ == 0 || (nowMs - lastPaintMs_) > kWispClaimStaleMs) {
      return false;
    }
    for (uint8_t i = 0; i < count_; ++i) {
      if (entries_[i].valid &&
          std::memcmp(entries_[i].mac, lampMac, 6) == 0) {
        std::memcpy(baseOut, entries_[i].base, 3);
        std::memcpy(shadeOut, entries_[i].shade, 3);
        return true;
      }
    }
    return false;
  }

 private:
  PaintEntry entries_[WISP_PAINT_MAX_ENTRIES] = {};
  uint8_t count_ = 0;
  uint32_t lastPaintMs_ = 0;
};

}  // namespace

void setUp() {}
void tearDown() {}

// 2-entry round-trip: both MACs retrieve their exact base/shade RGB.
void test_two_entry_round_trip() {
  PaintCache cache;

  const uint8_t mac0[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  const uint8_t mac1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

  // Build a 2-entry wire payload.
  uint8_t payload[2 * WISP_PAINT_ENTRY_SIZE] = {};
  std::memcpy(payload, mac0, 6);
  payload[6] = 0x10; payload[7] = 0x20; payload[8] = 0x30;   // base0
  payload[9] = 0x40; payload[10] = 0x50; payload[11] = 0x60; // shade0
  std::memcpy(payload + WISP_PAINT_ENTRY_SIZE, mac1, 6);
  payload[WISP_PAINT_ENTRY_SIZE + 6] = 0xA1;
  payload[WISP_PAINT_ENTRY_SIZE + 7] = 0xB2;
  payload[WISP_PAINT_ENTRY_SIZE + 8] = 0xC3;
  payload[WISP_PAINT_ENTRY_SIZE + 9] = 0xD4;
  payload[WISP_PAINT_ENTRY_SIZE + 10] = 0xE5;
  payload[WISP_PAINT_ENTRY_SIZE + 11] = 0xF6;

  cache.cacheWispPaint(payload, 2, 1000);

  uint8_t base[3], shade[3];

  TEST_ASSERT_TRUE(cache.findWispPaint(mac0, 1000, base, shade));
  TEST_ASSERT_EQUAL_UINT8(0x10, base[0]);
  TEST_ASSERT_EQUAL_UINT8(0x20, base[1]);
  TEST_ASSERT_EQUAL_UINT8(0x30, base[2]);
  TEST_ASSERT_EQUAL_UINT8(0x40, shade[0]);
  TEST_ASSERT_EQUAL_UINT8(0x50, shade[1]);
  TEST_ASSERT_EQUAL_UINT8(0x60, shade[2]);

  TEST_ASSERT_TRUE(cache.findWispPaint(mac1, 1000, base, shade));
  TEST_ASSERT_EQUAL_UINT8(0xA1, base[0]);
  TEST_ASSERT_EQUAL_UINT8(0xB2, base[1]);
  TEST_ASSERT_EQUAL_UINT8(0xC3, base[2]);
  TEST_ASSERT_EQUAL_UINT8(0xD4, shade[0]);
  TEST_ASSERT_EQUAL_UINT8(0xE5, shade[1]);
  TEST_ASSERT_EQUAL_UINT8(0xF6, shade[2]);
}

// Unknown MAC returns false.
void test_unknown_mac_returns_false() {
  PaintCache cache;
  const uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  uint8_t entry[WISP_PAINT_ENTRY_SIZE] = {};
  std::memcpy(entry, mac, 6);
  cache.cacheWispPaint(entry, 1, 1000);

  const uint8_t other[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t base[3], shade[3];
  TEST_ASSERT_FALSE(cache.findWispPaint(other, 1000, base, shade));
}

// Entry ages out when nowMs - lastPaintMs > kWispClaimStaleMs.
void test_stale_entry_not_returned() {
  PaintCache cache;
  const uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
  uint8_t entry[WISP_PAINT_ENTRY_SIZE] = {};
  std::memcpy(entry, mac, 6);
  entry[6] = 0xFF;  // base R

  cache.cacheWispPaint(entry, 1, 1000);

  // nowMs past the stale window.
  const uint32_t staleNow = 1000 + kWispClaimStaleMs + 1;
  uint8_t base[3], shade[3];
  TEST_ASSERT_FALSE(cache.findWispPaint(mac, staleNow, base, shade));
}

// Entry is returned when exactly at the stale boundary (inclusive edge).
void test_entry_at_stale_boundary_returned() {
  PaintCache cache;
  const uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  uint8_t entry[WISP_PAINT_ENTRY_SIZE] = {};
  std::memcpy(entry, mac, 6);
  entry[6] = 0xAB;

  cache.cacheWispPaint(entry, 1, 1000);

  const uint32_t boundaryNow = 1000 + kWispClaimStaleMs;
  uint8_t base[3], shade[3];
  TEST_ASSERT_TRUE(cache.findWispPaint(mac, boundaryNow, base, shade));
  TEST_ASSERT_EQUAL_UINT8(0xAB, base[0]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_two_entry_round_trip);
  RUN_TEST(test_unknown_mac_returns_false);
  RUN_TEST(test_stale_entry_not_returned);
  RUN_TEST(test_entry_at_stale_boundary_returned);
  return UNITY_END();
}
