// Native-host unit tests for WispFleetCache paint accumulation:
//   - round-trip: each lamp MAC retrieves its exact base/shade RGB.
//   - unknown MAC returns false.
//   - entries accumulate across frames (the wisp rotates a partial window).
//   - per-entry staleness eviction at kStaleMs.
//   - upsert refreshes in place; a full cache evicts the oldest entry.

#include <unity.h>

#include <cstdint>
#include <cstring>

// Native-test seam: include the .cpp to get the definitions.
#include "components/network/mesh/wisp_fleet_cache.cpp"

using lamp::WispFleetCache;

void setUp() {}
void tearDown() {}

namespace {

void makePaintEntry(uint8_t* out, const uint8_t mac[6],
                    uint8_t baseSeed, uint8_t shadeSeed) {
  std::memcpy(out, mac, 6);
  out[6] = baseSeed;
  out[7] = static_cast<uint8_t>(baseSeed + 1);
  out[8] = static_cast<uint8_t>(baseSeed + 2);
  out[9] = shadeSeed;
  out[10] = static_cast<uint8_t>(shadeSeed + 1);
  out[11] = static_cast<uint8_t>(shadeSeed + 2);
}

void macForIndex(uint8_t out[6], uint8_t i) {
  const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, i};
  std::memcpy(out, mac, 6);
}

}  // namespace

void test_two_entry_round_trip() {
  WispFleetCache cache;
  uint8_t mac0[6], mac1[6];
  macForIndex(mac0, 0);
  macForIndex(mac1, 1);
  uint8_t frame[2 * 12];
  makePaintEntry(frame, mac0, 0x10, 0x40);
  makePaintEntry(frame + 12, mac1, 0xA0, 0xD0);
  cache.upsertPaints(frame, 2, 1000);

  uint8_t base[3], shade[3];
  TEST_ASSERT_TRUE(cache.findPaint(mac0, 1000, base, shade));
  TEST_ASSERT_EQUAL_UINT8(0x10, base[0]);
  TEST_ASSERT_EQUAL_UINT8(0x11, base[1]);
  TEST_ASSERT_EQUAL_UINT8(0x12, base[2]);
  TEST_ASSERT_EQUAL_UINT8(0x40, shade[0]);
  TEST_ASSERT_TRUE(cache.findPaint(mac1, 1000, base, shade));
  TEST_ASSERT_EQUAL_UINT8(0xA0, base[0]);
  TEST_ASSERT_EQUAL_UINT8(0xD0, shade[0]);
}

void test_unknown_mac_returns_false() {
  WispFleetCache cache;
  uint8_t mac[6], other[6];
  macForIndex(mac, 0);
  macForIndex(other, 9);
  uint8_t frame[12];
  makePaintEntry(frame, mac, 0x10, 0x40);
  cache.upsertPaints(frame, 1, 1000);

  uint8_t base[3], shade[3];
  TEST_ASSERT_FALSE(cache.findPaint(other, 1000, base, shade));
}

// Frames accumulate: a later frame must not evict earlier fresh entries.
void test_entries_accumulate_across_frames() {
  WispFleetCache cache;
  uint8_t mac0[6], mac1[6];
  macForIndex(mac0, 0);
  macForIndex(mac1, 1);
  uint8_t frame[12];
  makePaintEntry(frame, mac0, 0x10, 0x40);
  cache.upsertPaints(frame, 1, 1000);
  makePaintEntry(frame, mac1, 0xA0, 0xD0);
  cache.upsertPaints(frame, 1, 3000);

  uint8_t base[3], shade[3];
  TEST_ASSERT_TRUE(cache.findPaint(mac0, 4000, base, shade));
  TEST_ASSERT_TRUE(cache.findPaint(mac1, 4000, base, shade));
}

// Staleness is per entry, not per frame.
void test_per_entry_staleness() {
  WispFleetCache cache;
  uint8_t mac0[6], mac1[6];
  macForIndex(mac0, 0);
  macForIndex(mac1, 1);
  uint8_t frame[12];
  makePaintEntry(frame, mac0, 0x10, 0x40);
  cache.upsertPaints(frame, 1, 0);
  makePaintEntry(frame, mac1, 0xA0, 0xD0);
  cache.upsertPaints(frame, 1, 50000);

  const uint32_t nowMs = WispFleetCache::kStaleMs + 10000;
  uint8_t base[3], shade[3];
  TEST_ASSERT_FALSE(cache.findPaint(mac0, nowMs, base, shade));
  TEST_ASSERT_TRUE(cache.findPaint(mac1, nowMs, base, shade));
}

// Fresh exactly at the boundary (inclusive edge), stale one past it.
void test_boundary_age_still_fresh() {
  WispFleetCache cache;
  uint8_t mac[6];
  macForIndex(mac, 0);
  uint8_t frame[12];
  makePaintEntry(frame, mac, 0x10, 0x40);
  cache.upsertPaints(frame, 1, 1000);

  uint8_t base[3], shade[3];
  TEST_ASSERT_TRUE(
      cache.findPaint(mac, 1000 + WispFleetCache::kStaleMs, base, shade));
  TEST_ASSERT_FALSE(
      cache.findPaint(mac, 1001 + WispFleetCache::kStaleMs, base, shade));
}

// Re-hearing a mac refreshes its entry in place with the new colors.
void test_upsert_refreshes_in_place() {
  WispFleetCache cache;
  uint8_t mac[6];
  macForIndex(mac, 0);
  uint8_t frame[12];
  makePaintEntry(frame, mac, 0x10, 0x40);
  cache.upsertPaints(frame, 1, 1000);
  makePaintEntry(frame, mac, 0x77, 0x99);
  cache.upsertPaints(frame, 1, 2000);

  uint8_t base[3], shade[3];
  TEST_ASSERT_TRUE(cache.findPaint(mac, 2000, base, shade));
  TEST_ASSERT_EQUAL_UINT8(0x77, base[0]);
  TEST_ASSERT_EQUAL_UINT8(0x99, shade[0]);

  // Single blob entry: no duplicate slot was consumed.
  uint8_t blob[1 + 2 * 12];
  TEST_ASSERT_EQUAL_size_t(1 + 12,
                           cache.buildClaimsBlob(blob, sizeof(blob), 2000));
}

// At capacity, a new mac evicts the oldest entry.
void test_full_cache_evicts_oldest() {
  static WispFleetCache cache;
  uint8_t frame[12];
  for (size_t i = 0; i < WispFleetCache::kCapacity; ++i) {
    const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x01,
                            static_cast<uint8_t>(i >> 8),
                            static_cast<uint8_t>(i)};
    makePaintEntry(frame, mac, 0x10, 0x40);
    cache.upsertPaints(frame, 1, static_cast<uint32_t>(1000 + i));
  }
  const uint8_t newcomer[6] = {0x02, 0x00, 0x00, 0x02, 0x00, 0x00};
  makePaintEntry(frame, newcomer, 0x55, 0x66);
  const uint32_t nowMs =
      static_cast<uint32_t>(1000 + WispFleetCache::kCapacity);
  cache.upsertPaints(frame, 1, nowMs);

  uint8_t base[3], shade[3];
  TEST_ASSERT_TRUE(cache.findPaint(newcomer, nowMs, base, shade));
  const uint8_t oldestMac[6] = {0x02, 0x00, 0x00, 0x01, 0x00, 0x00};
  TEST_ASSERT_FALSE(cache.findPaint(oldestMac, nowMs, base, shade));
  const uint8_t secondOldest[6] = {0x02, 0x00, 0x00, 0x01, 0x00, 0x01};
  TEST_ASSERT_TRUE(cache.findPaint(secondOldest, nowMs, base, shade));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_two_entry_round_trip);
  RUN_TEST(test_unknown_mac_returns_false);
  RUN_TEST(test_entries_accumulate_across_frames);
  RUN_TEST(test_per_entry_staleness);
  RUN_TEST(test_boundary_age_still_fresh);
  RUN_TEST(test_upsert_refreshes_in_place);
  RUN_TEST(test_full_cache_evicts_oldest);
  return UNITY_END();
}
