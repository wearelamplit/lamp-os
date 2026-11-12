// Native-host unit tests for the CHAR_WISP_CLAIMS blob built by
// WispFleetCache::buildClaimsBlob.

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

// Blob for N claimed macs is exactly 1 + N*6 + N*6 bytes.
void test_blob_size_for_n_macs() {
  WispFleetCache cache;
  const uint8_t macs[3][6] = {
      {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01},
      {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02},
      {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03},
  };
  cache.upsertClaims(macs, 3, 1000);
  uint8_t buf[1 + 3 * lamp::WispFleetCache::kBlobEntrySize];
  const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), 1000);
  TEST_ASSERT_EQUAL_size_t(1 + 3 * lamp::WispFleetCache::kBlobEntrySize, n);
  TEST_ASSERT_EQUAL_UINT8(3, buf[0]);
}

// Colors are positionally aligned to macs: mac[i] pairs with colorPair[i].
void test_color_positional_alignment() {
  WispFleetCache cache;
  const uint8_t macs[2][6] = {
      {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
      {0x11, 0x12, 0x13, 0x14, 0x15, 0x16},
  };
  cache.upsertClaims(macs, 2, 1000);

  // Paint entries: mac0 → base{0x10,0x20,0x30} shade{0x40,0x50,0x60}
  //                mac1 → base{0xA1,0xB2,0xC3} shade{0xD4,0xE5,0xF6}
  uint8_t paint[2 * 12] = {};
  std::memcpy(paint, macs[0], 6);
  paint[6] = 0x10; paint[7] = 0x20; paint[8] = 0x30;
  paint[9] = 0x40; paint[10] = 0x50; paint[11] = 0x60;
  std::memcpy(paint + 12, macs[1], 6);
  paint[12 + 6] = 0xA1;
  paint[12 + 7] = 0xB2;
  paint[12 + 8] = 0xC3;
  paint[12 + 9] = 0xD4;
  paint[12 + 10] = 0xE5;
  paint[12 + 11] = 0xF6;
  upsertFrame(cache, paint, nullptr, 2, 1000);

  uint8_t buf[1 + 2 * lamp::WispFleetCache::kBlobEntrySize];
  const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), 1000);
  TEST_ASSERT_EQUAL_size_t(1 + 2 * lamp::WispFleetCache::kBlobEntrySize, n);

  // mac section: bytes 1..12; color section starts at 1 + 2*6 = 13.
  const size_t colorBase = 1 + 2 * 6;
  TEST_ASSERT_EQUAL_UINT8(0x10, buf[colorBase + 0]);
  TEST_ASSERT_EQUAL_UINT8(0x20, buf[colorBase + 1]);
  TEST_ASSERT_EQUAL_UINT8(0x30, buf[colorBase + 2]);
  TEST_ASSERT_EQUAL_UINT8(0x40, buf[colorBase + 3]);
  TEST_ASSERT_EQUAL_UINT8(0x50, buf[colorBase + 4]);
  TEST_ASSERT_EQUAL_UINT8(0x60, buf[colorBase + 5]);
  TEST_ASSERT_EQUAL_UINT8(0xA1, buf[colorBase + 6]);
  TEST_ASSERT_EQUAL_UINT8(0xB2, buf[colorBase + 7]);
  TEST_ASSERT_EQUAL_UINT8(0xC3, buf[colorBase + 8]);
  TEST_ASSERT_EQUAL_UINT8(0xD4, buf[colorBase + 9]);
  TEST_ASSERT_EQUAL_UINT8(0xE5, buf[colorBase + 10]);
  TEST_ASSERT_EQUAL_UINT8(0xF6, buf[colorBase + 11]);
}

// A claimed mac with no cached paint emits 00 00 00 00 00 00.
void test_no_paint_emits_sentinel() {
  WispFleetCache cache;
  const uint8_t macs[1][6] = {{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01}};
  cache.upsertClaims(macs, 1, 1000);

  uint8_t buf[1 + 1 * lamp::WispFleetCache::kBlobEntrySize];
  cache.buildClaimsBlob(buf, sizeof(buf), 1000);

  const size_t colorBase = 1 + 1 * 6;
  for (size_t i = 0; i < 6; ++i) {
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[colorBase + i]);
  }
}

// The claims blob carries the raw mesh mac.
void test_claims_blob_carries_raw_mac() {
  WispFleetCache cache;
  uint8_t mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
  const uint8_t macs[1][6] = {{0x10, 0x20, 0x30, 0x40, 0x50, 0x60}};
  cache.upsertClaims(macs, 1, 1000);

  uint8_t buf[1 + 1 * lamp::WispFleetCache::kBlobEntrySize];
  cache.buildClaimsBlob(buf, sizeof(buf), 1000);

  TEST_ASSERT_EQUAL_UINT8_ARRAY(mac, buf + 1, 6);
}

// The mac section is byte-identical to the raw claimed macs.
void test_mac_section_matches_raw_macs() {
  WispFleetCache cache;
  const uint8_t macs[2][6] = {
      {0xFC, 0xB4, 0x67, 0xF1, 0xDD, 0xA4},
      {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFC},
  };
  cache.upsertClaims(macs, 2, 1000);

  uint8_t buf[1 + 2 * lamp::WispFleetCache::kBlobEntrySize];
  cache.buildClaimsBlob(buf, sizeof(buf), 1000);

  uint8_t want[1 + 2 * 6];
  want[0] = 2;
  std::memcpy(want + 1, macs[0], 6);
  std::memcpy(want + 7, macs[1], 6);

  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, buf, 1 + 2 * 6);
}

// Mac present only in paint cache (no claim) → appears in blob with color.
void test_paint_only_mac_appears_in_blob() {
  WispFleetCache cache;
  const uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  uint8_t paint[12] = {};
  std::memcpy(paint, mac, 6);
  paint[6] = 0xAA; paint[7] = 0xBB; paint[8] = 0xCC;   // base
  paint[9] = 0xDD; paint[10] = 0xEE; paint[11] = 0xFF;  // shade
  upsertFrame(cache, paint, nullptr, 1, 1000);

  uint8_t buf[1 + 1 * lamp::WispFleetCache::kBlobEntrySize];
  const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), 1000);

  TEST_ASSERT_EQUAL_size_t(1 + 1 * lamp::WispFleetCache::kBlobEntrySize, n);
  TEST_ASSERT_EQUAL_UINT8(1, buf[0]);
  const size_t colorBase = 1 + 1 * 6;
  TEST_ASSERT_EQUAL_UINT8(0xAA, buf[colorBase + 0]);
  TEST_ASSERT_EQUAL_UINT8(0xFF, buf[colorBase + 5]);
}

// Mac in both caches appears exactly once with its color.
void test_union_mac_appears_once_with_color() {
  WispFleetCache cache;
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x10};
  const uint8_t macs[1][6] = {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x10}};
  cache.upsertClaims(macs, 1, 1000);

  uint8_t paint[12] = {};
  std::memcpy(paint, mac, 6);
  paint[6] = 0x10; paint[7] = 0x20; paint[8] = 0x30;
  paint[9] = 0x40; paint[10] = 0x50; paint[11] = 0x60;
  upsertFrame(cache, paint, nullptr, 1, 1000);

  uint8_t buf[1 + 2 * lamp::WispFleetCache::kBlobEntrySize];  // room for 2, but expect only 1
  const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), 1000);

  TEST_ASSERT_EQUAL_size_t(1 + 1 * lamp::WispFleetCache::kBlobEntrySize, n);
  TEST_ASSERT_EQUAL_UINT8(1, buf[0]);
}

// Stale claim + fresh paint → lamp appears via paint cache.
void test_stale_claim_fresh_paint_appears() {
  WispFleetCache cache;
  const uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x02};
  const uint8_t macs[1][6] = {{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x02}};
  cache.upsertClaims(macs, 1, 0);

  const uint32_t nowMs = WispFleetCache::kStaleMs + 1;
  uint8_t paint[12] = {};
  std::memcpy(paint, mac, 6);
  paint[6] = 0x55;
  upsertFrame(cache, paint, nullptr, 1, nowMs);

  uint8_t buf[1 + 1 * lamp::WispFleetCache::kBlobEntrySize];
  const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), nowMs);

  TEST_ASSERT_EQUAL_size_t(1 + 1 * lamp::WispFleetCache::kBlobEntrySize, n);
  TEST_ASSERT_EQUAL_UINT8(1, buf[0]);
}

// Both caches empty → count=0 blob (1 byte).
void test_both_caches_empty_returns_count_zero() {
  WispFleetCache cache;
  uint8_t buf[64];
  const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), 1000);
  TEST_ASSERT_EQUAL_size_t(1, n);
  TEST_ASSERT_EQUAL_UINT8(0, buf[0]);
}

// Claim frames accumulate across the wisp's rotating window.
void test_claims_accumulate_across_frames() {
  WispFleetCache cache;
  const uint8_t frame1[2][6] = {
      {0x02, 0x00, 0x00, 0x00, 0x00, 0x01},
      {0x02, 0x00, 0x00, 0x00, 0x00, 0x02},
  };
  const uint8_t frame2[2][6] = {
      {0x02, 0x00, 0x00, 0x00, 0x00, 0x03},
      {0x02, 0x00, 0x00, 0x00, 0x00, 0x01},  // repeat, must not duplicate
  };
  cache.upsertClaims(frame1, 2, 1000);
  cache.upsertClaims(frame2, 2, 3000);

  uint8_t buf[1 + 4 * lamp::WispFleetCache::kBlobEntrySize];
  const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), 4000);
  TEST_ASSERT_EQUAL_size_t(1 + 3 * lamp::WispFleetCache::kBlobEntrySize, n);
  TEST_ASSERT_EQUAL_UINT8(3, buf[0]);
}

// Entries beyond the buffer capacity are dropped, count matches.
void test_blob_truncates_to_out_cap() {
  WispFleetCache cache;
  const uint8_t macs[3][6] = {
      {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01},
      {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02},
      {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03},
  };
  cache.upsertClaims(macs, 3, 1000);

  uint8_t buf[1 + 2 * lamp::WispFleetCache::kBlobEntrySize];
  const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), 1000);
  TEST_ASSERT_EQUAL_size_t(1 + 2 * lamp::WispFleetCache::kBlobEntrySize, n);
  TEST_ASSERT_EQUAL_UINT8(2, buf[0]);
}

// Painted-lamp extension: [painted, baseW, shadeW] per mac after the pairs.
void test_ext_section_carries_w_and_painted() {
  constexpr size_t kE = WispFleetCache::kBlobEntrySize;
  WispFleetCache cache;
  const uint8_t macs[3][6] = {
      {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},  // painted warm white only
      {0x11, 0x12, 0x13, 0x14, 0x15, 0x16},  // painted black
      {0x21, 0x22, 0x23, 0x24, 0x25, 0x26},  // claimed, never painted
  };
  cache.upsertClaims(macs, 3, 1000);
  uint8_t paint[2 * 12] = {};
  std::memcpy(paint, macs[0], 6);
  std::memcpy(paint + 12, macs[1], 6);
  const uint8_t w[4] = {0xC8, 0x37, 0x00, 0x00};
  upsertFrame(cache, paint, w, 2, 1000);

  uint8_t buf[1 + 3 * kE];
  TEST_ASSERT_EQUAL_size_t(1 + 3 * kE, cache.buildClaimsBlob(buf, sizeof(buf), 1000));
  const size_t ext = 1 + 3 * 12;
  // mac0: painted, W intact, RGB zero.
  TEST_ASSERT_EQUAL_UINT8(1, buf[ext + 0]);
  TEST_ASSERT_EQUAL_UINT8(0xC8, buf[ext + 1]);
  TEST_ASSERT_EQUAL_UINT8(0x37, buf[ext + 2]);
  // mac1: painted black, distinct from mac2 by the flag alone.
  TEST_ASSERT_EQUAL_UINT8(1, buf[ext + 3]);
  TEST_ASSERT_EQUAL_UINT8(0, buf[ext + 4]);
  TEST_ASSERT_EQUAL_UINT8(0, buf[ext + 5]);
  // mac2: unpainted.
  TEST_ASSERT_EQUAL_UINT8(0, buf[ext + 6]);
  TEST_ASSERT_EQUAL_UINT8(0, buf[ext + 7]);
  TEST_ASSERT_EQUAL_UINT8(0, buf[ext + 8]);
  for (size_t i = 0; i < 18; ++i) TEST_ASSERT_EQUAL_UINT8(0, buf[1 + 3 * 6 + i]);
}

// A STATE without a W plane caches W = 0 and still marks the lamp painted.
void test_ext_section_without_w_plane() {
  WispFleetCache cache;
  const uint8_t mac[6] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36};
  uint8_t paint[12] = {};
  std::memcpy(paint, mac, 6);
  paint[6] = 0x40;
  paint[11] = 0x50;
  const uint8_t w[2] = {0x99, 0x88};
  upsertFrame(cache, paint, w, 1, 1000);
  upsertFrame(cache, paint, nullptr, 1, 2000);

  uint8_t buf[1 + WispFleetCache::kBlobEntrySize];
  TEST_ASSERT_EQUAL_size_t(sizeof(buf), cache.buildClaimsBlob(buf, sizeof(buf), 2000));
  const size_t ext = 1 + 12;
  TEST_ASSERT_EQUAL_UINT8(0x40, buf[1 + 6]);
  TEST_ASSERT_EQUAL_UINT8(1, buf[ext + 0]);
  TEST_ASSERT_EQUAL_UINT8(0, buf[ext + 1]);
  TEST_ASSERT_EQUAL_UINT8(0, buf[ext + 2]);
}

// A deployed app's reader takes [count][mac][pair] and ignores what follows:
// the prefix of the new blob is byte-identical to the old layout.
void test_blob_prefix_matches_legacy_layout() {
  WispFleetCache cache;
  const uint8_t macs[2][6] = {
      {0x41, 0x42, 0x43, 0x44, 0x45, 0x46},
      {0x51, 0x52, 0x53, 0x54, 0x55, 0x56},
  };
  cache.upsertClaims(macs, 2, 1000);
  uint8_t paint[12] = {};
  std::memcpy(paint, macs[0], 6);
  for (int i = 0; i < 6; ++i) paint[6 + i] = static_cast<uint8_t>(0x60 + i);
  const uint8_t w[2] = {7, 8};
  upsertFrame(cache, paint, w, 1, 1000);

  uint8_t buf[1 + 2 * WispFleetCache::kBlobEntrySize];
  TEST_ASSERT_EQUAL_size_t(sizeof(buf), cache.buildClaimsBlob(buf, sizeof(buf), 1000));
  uint8_t want[1 + 2 * 12] = {2};
  std::memcpy(want + 1, macs[0], 6);
  std::memcpy(want + 7, macs[1], 6);
  for (int i = 0; i < 6; ++i) want[13 + i] = static_cast<uint8_t>(0x60 + i);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, buf, sizeof(want));
}

// A buffer with room for the pairs but not the paint section gets the
// [count][mac][pair] layout with every entry it can hold.
void test_paint_section_dropped_when_it_does_not_fit() {
  WispFleetCache cache;
  const uint8_t macs[3][6] = {
      {0x61, 0x62, 0x63, 0x64, 0x65, 0x66},
      {0x71, 0x72, 0x73, 0x74, 0x75, 0x76},
      {0x81, 0x82, 0x83, 0x84, 0x85, 0x86},
  };
  cache.upsertClaims(macs, 3, 1000);
  uint8_t buf[1 + 3 * 12 + 2];  // short of 1 + 3 * kBlobEntrySize
  TEST_ASSERT_EQUAL_size_t(1 + 3 * 12, cache.buildClaimsBlob(buf, sizeof(buf), 1000));
  TEST_ASSERT_EQUAL_UINT8(3, buf[0]);
}

// The 512 B direct read: past 34 lamps it keeps the 42 entries the older
// layout carried, and up to 34 it carries the paint section.
void test_att_sized_buffer_keeps_legacy_entry_count() {
  constexpr size_t kAttMax = WispFleetCache::kAttValueMax;
  const size_t kLampCounts[] = {34, 42, 60};
  for (size_t lamps : kLampCounts) {
    WispFleetCache cache;
    uint8_t macs[60][6] = {};
    for (size_t i = 0; i < lamps; ++i) {
      macs[i][0] = 0x02;
      macs[i][5] = static_cast<uint8_t>(i + 1);
    }
    cache.upsertClaims(macs, static_cast<uint8_t>(lamps), 1000);
    uint8_t buf[kAttMax];
    const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), 1000);
    TEST_ASSERT_TRUE(n <= kAttMax);
    const size_t want = lamps < 42 ? lamps : 42;
    TEST_ASSERT_EQUAL_UINT8(want, buf[0]);
    const size_t withPaint = 1 + want * WispFleetCache::kBlobEntrySize;
    TEST_ASSERT_EQUAL_size_t(withPaint <= kAttMax ? withPaint : 1 + want * 12, n);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_blob_size_for_n_macs);
  RUN_TEST(test_color_positional_alignment);
  RUN_TEST(test_no_paint_emits_sentinel);
  RUN_TEST(test_claims_blob_carries_raw_mac);
  RUN_TEST(test_mac_section_matches_raw_macs);
  RUN_TEST(test_paint_only_mac_appears_in_blob);
  RUN_TEST(test_union_mac_appears_once_with_color);
  RUN_TEST(test_stale_claim_fresh_paint_appears);
  RUN_TEST(test_both_caches_empty_returns_count_zero);
  RUN_TEST(test_claims_accumulate_across_frames);
  RUN_TEST(test_blob_truncates_to_out_cap);
  RUN_TEST(test_ext_section_carries_w_and_painted);
  RUN_TEST(test_ext_section_without_w_plane);
  RUN_TEST(test_blob_prefix_matches_legacy_layout);
  RUN_TEST(test_paint_section_dropped_when_it_does_not_fit);
  RUN_TEST(test_att_sized_buffer_keeps_legacy_entry_count);
  return UNITY_END();
}
