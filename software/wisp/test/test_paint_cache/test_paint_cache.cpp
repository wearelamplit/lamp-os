// Native tests for LampPaintCache.
//
// Pins the contract:
//   - stamp/find round-trips full RGBW (base + shade), fadeEndMs, lastPaintMs.
//   - stamp on an already-cached mac upserts in place, doesn't grow.
//   - touchLastPaint only moves lastPaintMs; target + fadeEndMs untouched.
//   - unknown mac: find fails, touchLastPaint no-ops.
//   - pruneToRoster evicts a mac that left, keeps one that stayed, survives
//     the roster array being rebuilt/re-sorted (MAC-keyed, not positional).

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "paint/drift.hpp"
#include "paint/paint_cache.hpp"

using wisp::LampPaintCache;

namespace {

void mac(uint8_t out[6], uint8_t last) {
  const uint8_t m[6] = {0x11, 0x22, 0x33, 0x44, 0x55, last};
  std::memcpy(out, m, 6);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_stamp_find_roundtrips_rgbw() {
  LampPaintCache cache;
  uint8_t m[6];
  mac(m, 1);
  const uint8_t base[4]  = {0x10, 0x20, 0x30, 0x40};
  const uint8_t shade[4] = {0x50, 0x60, 0x70, 0x80};
  cache.stamp(m, base, shade, /*fadeEndMs=*/5000, /*lastPaintMs=*/1000);

  LampPaintCache::Entry e;
  TEST_ASSERT_TRUE(cache.find(m, e));
  TEST_ASSERT_EQUAL_MEMORY(m, e.mac, 6);
  TEST_ASSERT_EQUAL_MEMORY(base, e.base, 4);
  TEST_ASSERT_EQUAL_MEMORY(shade, e.shade, 4);
  TEST_ASSERT_EQUAL_UINT32(5000, e.fadeEndMs);
  TEST_ASSERT_EQUAL_UINT32(1000, e.lastPaintMs);
}

void test_unknown_mac_find_fails() {
  LampPaintCache cache;
  uint8_t m[6];
  mac(m, 9);
  LampPaintCache::Entry e;
  TEST_ASSERT_FALSE(cache.find(m, e));
}

void test_stamp_upserts_in_place() {
  LampPaintCache cache;
  uint8_t m[6];
  mac(m, 1);
  const uint8_t base1[4] = {1, 1, 1, 1};
  const uint8_t shade1[4] = {2, 2, 2, 2};
  cache.stamp(m, base1, shade1, 1000, 0);
  TEST_ASSERT_EQUAL_size_t(1, cache.size());

  const uint8_t base2[4] = {9, 9, 9, 9};
  const uint8_t shade2[4] = {8, 8, 8, 8};
  cache.stamp(m, base2, shade2, 9000, 5000);
  TEST_ASSERT_EQUAL_size_t(1, cache.size());

  LampPaintCache::Entry e;
  TEST_ASSERT_TRUE(cache.find(m, e));
  TEST_ASSERT_EQUAL_MEMORY(base2, e.base, 4);
  TEST_ASSERT_EQUAL_UINT32(9000, e.fadeEndMs);
  TEST_ASSERT_EQUAL_UINT32(5000, e.lastPaintMs);
}

// Keep-alive re-affirm: only lastPaintMs moves, target + fadeEndMs hold.
void test_touch_last_paint_leaves_target_and_fade_end() {
  LampPaintCache cache;
  uint8_t m[6];
  mac(m, 1);
  const uint8_t base[4] = {1, 2, 3, 4};
  const uint8_t shade[4] = {5, 6, 7, 8};
  cache.stamp(m, base, shade, 40000, 0);

  cache.touchLastPaint(m, 45000);

  LampPaintCache::Entry e;
  TEST_ASSERT_TRUE(cache.find(m, e));
  TEST_ASSERT_EQUAL_MEMORY(base, e.base, 4);
  TEST_ASSERT_EQUAL_MEMORY(shade, e.shade, 4);
  TEST_ASSERT_EQUAL_UINT32(40000, e.fadeEndMs);
  TEST_ASSERT_EQUAL_UINT32(45000, e.lastPaintMs);
}

void test_touch_last_paint_unknown_mac_is_noop() {
  LampPaintCache cache;
  uint8_t m[6];
  mac(m, 9);
  cache.touchLastPaint(m, 12345);
  LampPaintCache::Entry e;
  TEST_ASSERT_FALSE(cache.find(m, e));
}

// A FAILed send re-opens the deadline: lastPaintMs resets so a later
// keepaliveDue() check goes true, while target + fadeEndMs hold (the
// keep-alive resend still needs the cached colour).
void test_mark_overdue_resets_last_paint_keeps_target() {
  LampPaintCache cache;
  uint8_t m[6];
  mac(m, 1);
  const uint8_t base[4] = {1, 2, 3, 4};
  const uint8_t shade[4] = {5, 6, 7, 8};
  cache.stamp(m, base, shade, /*fadeEndMs=*/40000, /*lastPaintMs=*/30000);

  cache.markOverdue(m);

  LampPaintCache::Entry e;
  TEST_ASSERT_TRUE(cache.find(m, e));
  TEST_ASSERT_EQUAL_MEMORY(base, e.base, 4);
  TEST_ASSERT_EQUAL_MEMORY(shade, e.shade, 4);
  TEST_ASSERT_EQUAL_UINT32(40000, e.fadeEndMs);
  TEST_ASSERT_EQUAL_UINT32(0, e.lastPaintMs);
}

// End-to-end: a FAILed MAC's deadline reopens (keepaliveDue true) while an
// un-failed peer painted at the same time stays held.
void test_mark_overdue_makes_keepalive_due_untouched_peer_stays_held() {
  LampPaintCache cache;
  uint8_t failed[6], ok[6];
  mac(failed, 1);
  mac(ok, 2);
  const uint8_t base[4] = {1, 1, 1, 1};
  const uint8_t shade[4] = {2, 2, 2, 2};
  const uint32_t keepaliveMs = 30000;
  const uint32_t nowMs = 40000;
  cache.stamp(failed, base, shade, nowMs + 1500, /*lastPaintMs=*/nowMs);
  cache.stamp(ok, base, shade, nowMs + 1500, /*lastPaintMs=*/nowMs);

  cache.markOverdue(failed);

  LampPaintCache::Entry eFailed, eOk;
  TEST_ASSERT_TRUE(cache.find(failed, eFailed));
  TEST_ASSERT_TRUE(cache.find(ok, eOk));
  TEST_ASSERT_TRUE(wisp::keepaliveDue(nowMs, eFailed.lastPaintMs, keepaliveMs));
  TEST_ASSERT_FALSE(wisp::keepaliveDue(nowMs, eOk.lastPaintMs, keepaliveMs));
}

// forceDue makes keepaliveDue true even inside the boot window, where
// lastPaintMs==0 alone wouldn't yet reach the deadline.
void test_mark_overdue_forces_keepalive_due_within_boot_window() {
  LampPaintCache cache;
  uint8_t m[6];
  mac(m, 1);
  const uint8_t base[4] = {1, 2, 3, 4};
  const uint8_t shade[4] = {5, 6, 7, 8};
  const uint32_t keepaliveMs = 30000;
  cache.stamp(m, base, shade, /*fadeEndMs=*/6500, /*lastPaintMs=*/5000);

  cache.markOverdue(m);

  LampPaintCache::Entry e;
  TEST_ASSERT_TRUE(cache.find(m, e));
  TEST_ASSERT_TRUE(e.forceDue);
  TEST_ASSERT_TRUE(wisp::keepaliveDue(/*nowMs=*/5000, e.lastPaintMs, keepaliveMs, e.forceDue));
}

// A subsequent stamp (fresh paint lands) clears forceDue.
void test_stamp_after_mark_overdue_clears_force_due() {
  LampPaintCache cache;
  uint8_t m[6];
  mac(m, 1);
  const uint8_t base[4] = {1, 2, 3, 4};
  const uint8_t shade[4] = {5, 6, 7, 8};
  cache.stamp(m, base, shade, 6500, 5000);
  cache.markOverdue(m);

  cache.stamp(m, base, shade, 12000, 10000);

  LampPaintCache::Entry e;
  TEST_ASSERT_TRUE(cache.find(m, e));
  TEST_ASSERT_FALSE(e.forceDue);
}

// A keep-alive re-affirm (touchLastPaint) also clears forceDue.
void test_touch_last_paint_after_mark_overdue_clears_force_due() {
  LampPaintCache cache;
  uint8_t m[6];
  mac(m, 1);
  const uint8_t base[4] = {1, 2, 3, 4};
  const uint8_t shade[4] = {5, 6, 7, 8};
  cache.stamp(m, base, shade, 6500, 5000);
  cache.markOverdue(m);

  cache.touchLastPaint(m, 10000);

  LampPaintCache::Entry e;
  TEST_ASSERT_TRUE(cache.find(m, e));
  TEST_ASSERT_FALSE(e.forceDue);
}

void test_mark_overdue_unknown_mac_is_noop() {
  LampPaintCache cache;
  uint8_t m[6];
  mac(m, 9);
  cache.markOverdue(m);
  LampPaintCache::Entry e;
  TEST_ASSERT_FALSE(cache.find(m, e));
}

// A lamp that leaves the roster (dropped from the driftMacs_ rebuild) must
// be evicted; a lamp that stays survives the rebuild/re-sort untouched.
void test_prune_to_roster_evicts_departed_keeps_present() {
  LampPaintCache cache;
  uint8_t m1[6], m2[6], m3[6];
  mac(m1, 1);
  mac(m2, 2);
  mac(m3, 3);
  const uint8_t base[4] = {1, 1, 1, 1};
  const uint8_t shade[4] = {2, 2, 2, 2};
  cache.stamp(m1, base, shade, 1000, 0);
  cache.stamp(m2, base, shade, 1000, 0);
  cache.stamp(m3, base, shade, 1000, 0);
  TEST_ASSERT_EQUAL_size_t(3, cache.size());

  // m2 left; roster rebuild also reorders (m3 now before m1).
  uint8_t roster[2][6];
  std::memcpy(roster[0], m3, 6);
  std::memcpy(roster[1], m1, 6);
  cache.pruneToRoster(roster, 2);

  TEST_ASSERT_EQUAL_size_t(2, cache.size());
  LampPaintCache::Entry e;
  TEST_ASSERT_TRUE(cache.find(m1, e));
  TEST_ASSERT_TRUE(cache.find(m3, e));
  TEST_ASSERT_FALSE(cache.find(m2, e));
}

// A newcomer (never painted, so never cached) isn't touched by prune, and
// simply has no entry until its first paint stamps one.
void test_prune_to_roster_ignores_uncached_newcomer() {
  LampPaintCache cache;
  uint8_t m1[6], newcomer[6];
  mac(m1, 1);
  mac(newcomer, 4);
  const uint8_t base[4] = {1, 1, 1, 1};
  const uint8_t shade[4] = {2, 2, 2, 2};
  cache.stamp(m1, base, shade, 1000, 0);

  uint8_t roster[2][6];
  std::memcpy(roster[0], m1, 6);
  std::memcpy(roster[1], newcomer, 6);
  cache.pruneToRoster(roster, 2);

  TEST_ASSERT_EQUAL_size_t(1, cache.size());
  LampPaintCache::Entry e;
  TEST_ASSERT_TRUE(cache.find(m1, e));
  TEST_ASSERT_FALSE(cache.find(newcomer, e));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stamp_find_roundtrips_rgbw);
  RUN_TEST(test_unknown_mac_find_fails);
  RUN_TEST(test_stamp_upserts_in_place);
  RUN_TEST(test_touch_last_paint_leaves_target_and_fade_end);
  RUN_TEST(test_touch_last_paint_unknown_mac_is_noop);
  RUN_TEST(test_mark_overdue_resets_last_paint_keeps_target);
  RUN_TEST(test_mark_overdue_makes_keepalive_due_untouched_peer_stays_held);
  RUN_TEST(test_mark_overdue_forces_keepalive_due_within_boot_window);
  RUN_TEST(test_stamp_after_mark_overdue_clears_force_due);
  RUN_TEST(test_touch_last_paint_after_mark_overdue_clears_force_due);
  RUN_TEST(test_mark_overdue_unknown_mac_is_noop);
  RUN_TEST(test_prune_to_roster_evicts_departed_keeps_present);
  RUN_TEST(test_prune_to_roster_ignores_uncached_newcomer);
  return UNITY_END();
}
