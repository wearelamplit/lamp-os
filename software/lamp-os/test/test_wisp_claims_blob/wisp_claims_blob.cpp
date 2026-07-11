#include <unity.h>
#include <cstdint>
#include <cstring>
#include "components/network/mesh/wisp_claims_addr.hpp"

void setUp() {}
void tearDown() {}

void test_adds_two_simple() {
  const uint8_t mesh[6] = {0xFC, 0xB4, 0x67, 0xF1, 0xDD, 0xA4};
  uint8_t out[6];
  ble_control::bdAddrFromMeshMac(mesh, out);
  const uint8_t want[6] = {0xFC, 0xB4, 0x67, 0xF1, 0xDD, 0xA6};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, 6);
}

void test_carries_across_octets() {
  const uint8_t mesh[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  uint8_t out[6];
  ble_control::bdAddrFromMeshMac(mesh, out);
  const uint8_t want[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEF, 0x01};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, 6);
}

// ---- Inline mirror of buildWispClaimsBlob for native tests ----
// Mirrors production logic without FreeRTOS or Arduino deps.

namespace {

constexpr uint32_t kWispClaimStaleMs  = 60000;
constexpr size_t   WISP_PAINT_ENTRY_SIZE = 12;
constexpr size_t   WISP_PAINT_MAX_ENTRIES = 18;
constexpr size_t   kMaxWispClaimEntries = 32;

struct PaintEntry {
  uint8_t mac[6] = {0};
  uint8_t base[3] = {0};
  uint8_t shade[3] = {0};
  bool valid = false;
};

struct ClaimsCache {
  uint8_t claimedMacs[kMaxWispClaimEntries][6] = {};
  uint8_t claimedCount = 0;
  uint32_t lastClaimMs = 0;
  PaintEntry paintEntries[WISP_PAINT_MAX_ENTRIES] = {};
  uint8_t paintCount = 0;
  uint32_t lastPaintMs = 0;

  void cacheWispClaim(const uint8_t macs[][6], uint8_t count, uint32_t nowMs) {
    const uint8_t safe = count > kMaxWispClaimEntries
                             ? static_cast<uint8_t>(kMaxWispClaimEntries)
                             : count;
    std::memcpy(claimedMacs, macs, static_cast<size_t>(safe) * 6);
    claimedCount = safe;
    lastClaimMs = nowMs;
  }

  void cacheWispPaint(const uint8_t* entries, uint8_t count, uint32_t nowMs) {
    const uint8_t safe = count > WISP_PAINT_MAX_ENTRIES
                             ? static_cast<uint8_t>(WISP_PAINT_MAX_ENTRIES)
                             : count;
    paintCount = safe;
    lastPaintMs = nowMs;
    for (uint8_t i = 0; i < safe; ++i) {
      const uint8_t* e = entries + static_cast<size_t>(i) * WISP_PAINT_ENTRY_SIZE;
      std::memcpy(paintEntries[i].mac, e, 6);
      std::memcpy(paintEntries[i].base, e + 6, 3);
      std::memcpy(paintEntries[i].shade, e + 9, 3);
      paintEntries[i].valid = true;
    }
    for (uint8_t i = safe; i < WISP_PAINT_MAX_ENTRIES; ++i) {
      paintEntries[i].valid = false;
    }
  }

  bool findWispPaint(const uint8_t mac[6], uint32_t nowMs,
                     uint8_t baseOut[3], uint8_t shadeOut[3]) const {
    if (lastPaintMs == 0 || (nowMs - lastPaintMs) > kWispClaimStaleMs) {
      return false;
    }
    for (uint8_t i = 0; i < paintCount; ++i) {
      if (paintEntries[i].valid &&
          std::memcmp(paintEntries[i].mac, mac, 6) == 0) {
        std::memcpy(baseOut, paintEntries[i].base, 3);
        std::memcpy(shadeOut, paintEntries[i].shade, 3);
        return true;
      }
    }
    return false;
  }

  size_t buildWispClaimsBlob(uint8_t* out, size_t outCap, uint32_t nowMs) const {
    if (!out || outCap == 0) return 0;

    // Union of fresh claim macs + fresh paint macs (deduped, claim order first).
    const bool claimFresh = lastClaimMs != 0 && (nowMs - lastClaimMs) <= kWispClaimStaleMs;
    const bool paintFresh = lastPaintMs != 0 && (nowMs - lastPaintMs) <= kWispClaimStaleMs;

    uint8_t unionMacs[kMaxWispClaimEntries][6] = {};
    uint8_t unionCount = 0;

    if (claimFresh) {
      for (uint8_t i = 0; i < claimedCount && unionCount < kMaxWispClaimEntries; ++i) {
        std::memcpy(unionMacs[unionCount++], claimedMacs[i], 6);
      }
    }
    if (paintFresh) {
      for (uint8_t i = 0; i < paintCount && unionCount < kMaxWispClaimEntries; ++i) {
        if (!paintEntries[i].valid) continue;
        bool dup = false;
        for (uint8_t j = 0; j < unionCount; ++j) {
          if (std::memcmp(unionMacs[j], paintEntries[i].mac, 6) == 0) { dup = true; break; }
        }
        if (!dup) std::memcpy(unionMacs[unionCount++], paintEntries[i].mac, 6);
      }
    }

    const size_t needed = 1 + static_cast<size_t>(unionCount) * 12;
    if (needed > outCap) { out[0] = 0; return 1; }
    out[0] = unionCount;
    for (uint8_t i = 0; i < unionCount; ++i) {
      ble_control::bdAddrFromMeshMac(unionMacs[i], out + 1 + static_cast<size_t>(i) * 6);
    }
    const size_t colorBase = 1 + static_cast<size_t>(unionCount) * 6;
    for (uint8_t i = 0; i < unionCount; ++i) {
      uint8_t* dst = out + colorBase + static_cast<size_t>(i) * 6;
      uint8_t base[3], shade[3];
      if (findWispPaint(unionMacs[i], nowMs, base, shade)) {
        std::memcpy(dst, base, 3);
        std::memcpy(dst + 3, shade, 3);
      } else {
        std::memset(dst, 0, 6);
      }
    }
    return needed;
  }
};

}  // namespace

// Blob for N claimed macs is exactly 1 + N*6 + N*6 bytes.
void test_blob_size_for_n_macs() {
  ClaimsCache cache;
  const uint8_t macs[3][6] = {
      {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01},
      {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02},
      {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03},
  };
  cache.cacheWispClaim(macs, 3, 1000);
  uint8_t buf[1 + 3 * 12];
  const size_t n = cache.buildWispClaimsBlob(buf, sizeof(buf), 1000);
  TEST_ASSERT_EQUAL_size_t(1 + 3 * 12, n);
  TEST_ASSERT_EQUAL_UINT8(3, buf[0]);
}

// Colors are positionally aligned to macs: mac[i] pairs with colorPair[i].
void test_color_positional_alignment() {
  ClaimsCache cache;
  const uint8_t macs[2][6] = {
      {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
      {0x11, 0x12, 0x13, 0x14, 0x15, 0x16},
  };
  cache.cacheWispClaim(macs, 2, 1000);

  // Build paint entries: mac0 → base{0x10,0x20,0x30} shade{0x40,0x50,0x60}
  //                      mac1 → base{0xA1,0xB2,0xC3} shade{0xD4,0xE5,0xF6}
  uint8_t paint[2 * WISP_PAINT_ENTRY_SIZE] = {};
  std::memcpy(paint, macs[0], 6);
  paint[6] = 0x10; paint[7] = 0x20; paint[8] = 0x30;
  paint[9] = 0x40; paint[10] = 0x50; paint[11] = 0x60;
  std::memcpy(paint + WISP_PAINT_ENTRY_SIZE, macs[1], 6);
  paint[WISP_PAINT_ENTRY_SIZE + 6] = 0xA1;
  paint[WISP_PAINT_ENTRY_SIZE + 7] = 0xB2;
  paint[WISP_PAINT_ENTRY_SIZE + 8] = 0xC3;
  paint[WISP_PAINT_ENTRY_SIZE + 9] = 0xD4;
  paint[WISP_PAINT_ENTRY_SIZE + 10] = 0xE5;
  paint[WISP_PAINT_ENTRY_SIZE + 11] = 0xF6;
  cache.cacheWispPaint(paint, 2, 1000);

  uint8_t buf[1 + 2 * 12];
  const size_t n = cache.buildWispClaimsBlob(buf, sizeof(buf), 1000);
  TEST_ASSERT_EQUAL_size_t(1 + 2 * 12, n);

  // mac section: bytes 1..12
  // color section starts at 1 + 2*6 = 13
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
  ClaimsCache cache;
  const uint8_t macs[1][6] = {{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01}};
  cache.cacheWispClaim(macs, 1, 1000);
  // no cacheWispPaint call

  uint8_t buf[1 + 1 * 12];
  cache.buildWispClaimsBlob(buf, sizeof(buf), 1000);

  const size_t colorBase = 1 + 1 * 6;
  for (size_t i = 0; i < 6; ++i) {
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[colorBase + i]);
  }
}

// The first 1 + N*6 bytes are byte-identical to the legacy mac-only blob.
void test_legacy_mac_prefix_unchanged() {
  ClaimsCache cache;
  const uint8_t macs[2][6] = {
      {0xFC, 0xB4, 0x67, 0xF1, 0xDD, 0xA4},
      {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFC},
  };
  cache.cacheWispClaim(macs, 2, 1000);

  uint8_t newBlob[1 + 2 * 12];
  cache.buildWispClaimsBlob(newBlob, sizeof(newBlob), 1000);

  // Reconstruct what the legacy blob would have been: [count][bdAddr*count]
  uint8_t legacy[1 + 2 * 6];
  legacy[0] = 2;
  ble_control::bdAddrFromMeshMac(macs[0], legacy + 1);
  ble_control::bdAddrFromMeshMac(macs[1], legacy + 7);

  TEST_ASSERT_EQUAL_UINT8_ARRAY(legacy, newBlob, 1 + 2 * 6);
}

// Mac present only in paint cache (no claim) → appears in blob with its color.
// This is the desync-gap regression test: before the union fix this fails.
void test_paint_only_mac_appears_in_blob() {
  ClaimsCache cache;
  // No cacheWispClaim call: claim cache is empty.

  const uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  uint8_t paint[WISP_PAINT_ENTRY_SIZE] = {};
  std::memcpy(paint, mac, 6);
  paint[6] = 0xAA; paint[7] = 0xBB; paint[8] = 0xCC;   // base
  paint[9] = 0xDD; paint[10] = 0xEE; paint[11] = 0xFF;  // shade
  cache.cacheWispPaint(paint, 1, 1000);

  uint8_t buf[1 + 1 * 12];
  const size_t n = cache.buildWispClaimsBlob(buf, sizeof(buf), 1000);

  TEST_ASSERT_EQUAL_size_t(1 + 1 * 12, n);
  TEST_ASSERT_EQUAL_UINT8(1, buf[0]);
  // Color section starts at 1 + 1*6 = 7.
  const size_t colorBase = 1 + 1 * 6;
  TEST_ASSERT_EQUAL_UINT8(0xAA, buf[colorBase + 0]);
  TEST_ASSERT_EQUAL_UINT8(0xBB, buf[colorBase + 1]);
  TEST_ASSERT_EQUAL_UINT8(0xCC, buf[colorBase + 2]);
  TEST_ASSERT_EQUAL_UINT8(0xDD, buf[colorBase + 3]);
  TEST_ASSERT_EQUAL_UINT8(0xEE, buf[colorBase + 4]);
  TEST_ASSERT_EQUAL_UINT8(0xFF, buf[colorBase + 5]);
}

// Mac in both caches appears exactly once with its color.
void test_union_mac_appears_once_with_color() {
  ClaimsCache cache;
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x10};
  const uint8_t macs[1][6] = {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x10}};
  cache.cacheWispClaim(macs, 1, 1000);

  uint8_t paint[WISP_PAINT_ENTRY_SIZE] = {};
  std::memcpy(paint, mac, 6);
  paint[6] = 0x10; paint[7] = 0x20; paint[8] = 0x30;
  paint[9] = 0x40; paint[10] = 0x50; paint[11] = 0x60;
  cache.cacheWispPaint(paint, 1, 1000);

  uint8_t buf[1 + 2 * 12];  // room for 2, but expect only 1
  const size_t n = cache.buildWispClaimsBlob(buf, sizeof(buf), 1000);

  TEST_ASSERT_EQUAL_size_t(1 + 1 * 12, n);
  TEST_ASSERT_EQUAL_UINT8(1, buf[0]);
}

// Stale claim + fresh paint → lamp appears via paint cache.
void test_stale_claim_fresh_paint_appears() {
  ClaimsCache cache;
  const uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x02};
  const uint8_t macs[1][6] = {{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x02}};
  // Claim is stale (t=0, check at t=kWispClaimStaleMs+1).
  cache.cacheWispClaim(macs, 1, 0);

  const uint32_t nowMs = kWispClaimStaleMs + 1;
  uint8_t paint[WISP_PAINT_ENTRY_SIZE] = {};
  std::memcpy(paint, mac, 6);
  paint[6] = 0x55;
  cache.cacheWispPaint(paint, 1, nowMs);

  uint8_t buf[1 + 1 * 12];
  const size_t n = cache.buildWispClaimsBlob(buf, sizeof(buf), nowMs);

  TEST_ASSERT_EQUAL_size_t(1 + 1 * 12, n);
  TEST_ASSERT_EQUAL_UINT8(1, buf[0]);
}

// Both caches empty → count=0 blob (1 byte).
void test_both_caches_empty_returns_count_zero() {
  ClaimsCache cache;
  uint8_t buf[64];
  const size_t n = cache.buildWispClaimsBlob(buf, sizeof(buf), 1000);
  TEST_ASSERT_EQUAL_size_t(1, n);
  TEST_ASSERT_EQUAL_UINT8(0, buf[0]);
}

// Claim-only mac (no paint entry) → sentinel color pair 00*6.
void test_claim_only_mac_emits_sentinel() {
  ClaimsCache cache;
  const uint8_t macs[1][6] = {{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01}};
  cache.cacheWispClaim(macs, 1, 1000);
  // no cacheWispPaint

  uint8_t buf[1 + 1 * 12];
  cache.buildWispClaimsBlob(buf, sizeof(buf), 1000);

  const size_t colorBase = 1 + 1 * 6;
  for (size_t i = 0; i < 6; ++i) {
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[colorBase + i]);
  }
}

// The first 1 + N*6 bytes are byte-identical to the legacy mac-only blob
// even when membership comes from the union.
void test_legacy_mac_prefix_union() {
  ClaimsCache cache;
  // mac0 via claim only, mac1 via paint only.
  const uint8_t mac0[6] = {0xFC, 0xB4, 0x67, 0xF1, 0xDD, 0xA4};
  const uint8_t mac1[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFC};
  const uint8_t claimMacs[1][6] = {{0xFC, 0xB4, 0x67, 0xF1, 0xDD, 0xA4}};
  cache.cacheWispClaim(claimMacs, 1, 1000);

  uint8_t paint[WISP_PAINT_ENTRY_SIZE] = {};
  std::memcpy(paint, mac1, 6);
  cache.cacheWispPaint(paint, 1, 1000);

  uint8_t newBlob[1 + 2 * 12];
  cache.buildWispClaimsBlob(newBlob, sizeof(newBlob), 1000);
  TEST_ASSERT_EQUAL_UINT8(2, newBlob[0]);

  // Both macs must appear as bdAddr in the mac section (order: claim then paint).
  uint8_t expBd0[6], expBd1[6];
  ble_control::bdAddrFromMeshMac(mac0, expBd0);
  ble_control::bdAddrFromMeshMac(mac1, expBd1);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expBd0, newBlob + 1, 6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expBd1, newBlob + 7, 6);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_adds_two_simple);
  RUN_TEST(test_carries_across_octets);
  RUN_TEST(test_blob_size_for_n_macs);
  RUN_TEST(test_color_positional_alignment);
  RUN_TEST(test_no_paint_emits_sentinel);
  RUN_TEST(test_legacy_mac_prefix_unchanged);
  // Union membership tests (RED before fix):
  RUN_TEST(test_paint_only_mac_appears_in_blob);
  RUN_TEST(test_union_mac_appears_once_with_color);
  RUN_TEST(test_stale_claim_fresh_paint_appears);
  RUN_TEST(test_both_caches_empty_returns_count_zero);
  RUN_TEST(test_claim_only_mac_emits_sentinel);
  RUN_TEST(test_legacy_mac_prefix_union);
  return UNITY_END();
}
