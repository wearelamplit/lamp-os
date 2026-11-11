// Native-host unit tests for WispFleetCache paint accumulation:
//   - round-trip: each lamp MAC retrieves its exact base/shade RGB.
//   - unknown MAC returns false.
//   - entries accumulate across frames (the wisp rotates a partial window).
//   - per-entry staleness eviction at kStaleMs.
//   - upsert refreshes in place; a full cache evicts the oldest entry.
//   - keepColors holds a lamp's own fresh colours only.

#include <unity.h>

#include <cstdint>
#include <cstring>

// Native-test seam: include the .cpp to get the definitions.
#include "components/network/mesh/wisp_fleet_cache.cpp"

using lamp::WispFleetCache;

// One STATE frame's entries into `cache`: count * 12 bytes of
// lampMac(6)+baseRGB(3)+shadeRGB(3), `w` count * 2 bytes or nullptr for W = 0.
void upsertFrame(WispFleetCache& cache, const uint8_t* entries,
                 const uint8_t* w, uint8_t count, uint32_t nowMs) {
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t* src = entries + static_cast<size_t>(i) * 12;
    const uint8_t base[4] = {src[6], src[7], src[8],
                             static_cast<uint8_t>(w ? w[i * 2] : 0)};
    const uint8_t shade[4] = {src[9], src[10], src[11],
                              static_cast<uint8_t>(w ? w[i * 2 + 1] : 0)};
    cache.upsertPaint(src, base, shade, nowMs, false);
  }
}

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
  upsertFrame(cache, frame, nullptr, 2, 1000);

  uint8_t base[4], shade[4];
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
  upsertFrame(cache, frame, nullptr, 1, 1000);

  uint8_t base[4], shade[4];
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
  upsertFrame(cache, frame, nullptr, 1, 1000);
  makePaintEntry(frame, mac1, 0xA0, 0xD0);
  upsertFrame(cache, frame, nullptr, 1, 3000);

  uint8_t base[4], shade[4];
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
  upsertFrame(cache, frame, nullptr, 1, 0);
  makePaintEntry(frame, mac1, 0xA0, 0xD0);
  upsertFrame(cache, frame, nullptr, 1, 50000);

  const uint32_t nowMs = WispFleetCache::kStaleMs + 10000;
  uint8_t base[4], shade[4];
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
  upsertFrame(cache, frame, nullptr, 1, 1000);

  uint8_t base[4], shade[4];
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
  upsertFrame(cache, frame, nullptr, 1, 1000);
  makePaintEntry(frame, mac, 0x77, 0x99);
  upsertFrame(cache, frame, nullptr, 1, 2000);

  uint8_t base[4], shade[4];
  TEST_ASSERT_TRUE(cache.findPaint(mac, 2000, base, shade));
  TEST_ASSERT_EQUAL_UINT8(0x77, base[0]);
  TEST_ASSERT_EQUAL_UINT8(0x99, shade[0]);

  // Single blob entry: no duplicate slot was consumed.
  uint8_t blob[1 + 2 * lamp::WispFleetCache::kBlobEntrySize];
  TEST_ASSERT_EQUAL_size_t(1 + lamp::WispFleetCache::kBlobEntrySize,
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
    upsertFrame(cache, frame, nullptr, 1, static_cast<uint32_t>(1000 + i));
  }
  const uint8_t newcomer[6] = {0x02, 0x00, 0x00, 0x02, 0x00, 0x00};
  makePaintEntry(frame, newcomer, 0x55, 0x66);
  const uint32_t nowMs =
      static_cast<uint32_t>(1000 + WispFleetCache::kCapacity);
  upsertFrame(cache, frame, nullptr, 1, nowMs);

  uint8_t base[4], shade[4];
  TEST_ASSERT_TRUE(cache.findPaint(newcomer, nowMs, base, shade));
  const uint8_t oldestMac[6] = {0x02, 0x00, 0x00, 0x01, 0x00, 0x00};
  TEST_ASSERT_FALSE(cache.findPaint(oldestMac, nowMs, base, shade));
  const uint8_t secondOldest[6] = {0x02, 0x00, 0x00, 0x01, 0x00, 0x01};
  TEST_ASSERT_TRUE(cache.findPaint(secondOldest, nowMs, base, shade));
}

// keepColors holds only a lamp's own fresh colours: a lamp new to the cache
// takes the ones given, the one it evicts from a full cache included.
void test_keep_colors_takes_given_colours_for_a_new_lamp() {
  WispFleetCache cache;
  uint8_t mac[6];
  for (uint8_t i = 0; i < WispFleetCache::kCapacity; ++i) {
    uint8_t frame[12];
    macForIndex(mac, i);
    makePaintEntry(frame, mac, i, i);
    upsertFrame(cache, frame, nullptr, 1, static_cast<uint32_t>(1000 + i));
  }
  const uint8_t base[4] = {0xE1, 0xE2, 0xE3, 0xE4};
  const uint8_t shade[4] = {0xF1, 0xF2, 0xF3, 0xF4};
  macForIndex(mac, 0xEE);
  cache.upsertPaint(mac, base, shade, 1200, true);
  uint8_t gotBase[4], gotShade[4];
  TEST_ASSERT_TRUE(cache.findPaint(mac, 1200, gotBase, gotShade));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(base, gotBase, 4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(shade, gotShade, 4);

  const uint8_t other[4] = {1, 2, 3, 4};
  cache.upsertPaint(mac, other, other, 1300, true);
  TEST_ASSERT_TRUE(cache.findPaint(mac, 1300, gotBase, gotShade));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(base, gotBase, 4);
}

// A lamp whose entry went stale takes the colours given under keepColors, not
// the ones it had before it went silent.
void test_keep_colors_takes_given_colours_for_a_stale_lamp() {
  WispFleetCache cache;
  uint8_t mac[6];
  macForIndex(mac, 7);
  const uint8_t old[4] = {1, 2, 3, 4};
  cache.upsertPaint(mac, old, old, 1000, false);
  const uint8_t base[4] = {0xA1, 0xA2, 0xA3, 0xA4};
  const uint8_t shade[4] = {0xB1, 0xB2, 0xB3, 0xB4};
  const uint32_t later = 1000 + WispFleetCache::kStaleMs + 1;
  cache.upsertPaint(mac, base, shade, later, true);
  uint8_t gotBase[4], gotShade[4];
  TEST_ASSERT_TRUE(cache.findPaint(mac, later, gotBase, gotShade));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(base, gotBase, 4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(shade, gotShade, 4);
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
  RUN_TEST(test_keep_colors_takes_given_colours_for_a_new_lamp);
  RUN_TEST(test_keep_colors_takes_given_colours_for_a_stale_lamp);
  return UNITY_END();
}
