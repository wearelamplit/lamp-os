// Native-host unit tests for the CHAR_WISP_CLAIMS blob built by
// WispFleetCache::buildClaimsBlob.

#include <unity.h>
#include <cstdint>
#include <cstring>

// Native-test seam: include the .cpp to get the definitions.
#include "components/network/mesh/wisp_fleet_cache.cpp"

using lamp::WispFleetCache;

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
  uint8_t buf[1 + 3 * 12];
  const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), 1000);
  TEST_ASSERT_EQUAL_size_t(1 + 3 * 12, n);
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
  cache.upsertPaints(paint, 2, 1000);

  uint8_t buf[1 + 2 * 12];
  const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), 1000);
  TEST_ASSERT_EQUAL_size_t(1 + 2 * 12, n);

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

  uint8_t buf[1 + 1 * 12];
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

  uint8_t buf[1 + 1 * 12];
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

  uint8_t buf[1 + 2 * 12];
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
  cache.upsertPaints(paint, 1, 1000);

  uint8_t buf[1 + 1 * 12];
  const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), 1000);

  TEST_ASSERT_EQUAL_size_t(1 + 1 * 12, n);
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
  cache.upsertPaints(paint, 1, 1000);

  uint8_t buf[1 + 2 * 12];  // room for 2, but expect only 1
  const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), 1000);

  TEST_ASSERT_EQUAL_size_t(1 + 1 * 12, n);
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
  cache.upsertPaints(paint, 1, nowMs);

  uint8_t buf[1 + 1 * 12];
  const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), nowMs);

  TEST_ASSERT_EQUAL_size_t(1 + 1 * 12, n);
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

  uint8_t buf[1 + 4 * 12];
  const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), 4000);
  TEST_ASSERT_EQUAL_size_t(1 + 3 * 12, n);
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

  uint8_t buf[1 + 2 * 12];
  const size_t n = cache.buildClaimsBlob(buf, sizeof(buf), 1000);
  TEST_ASSERT_EQUAL_size_t(1 + 2 * 12, n);
  TEST_ASSERT_EQUAL_UINT8(2, buf[0]);
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
  return UNITY_END();
}
