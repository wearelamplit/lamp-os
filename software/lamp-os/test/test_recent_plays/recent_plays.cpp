// Native pins for RecentPlays: a type reads as played inside its window, a
// replay refreshes its one slot, a full table drops its oldest type, and a
// stamp stays old across a millis() wrap while retire() runs.

#include <unity.h>

#include <cstdint>

#include "expressions/recent_plays.cpp"

using lamp::RecentPlays;

namespace {
constexpr uint32_t kWindowMs = 180000;
int g_types[RecentPlays::kSlots + 1];
}  // namespace

void setUp() {}
void tearDown() {}

void test_played_inside_its_window_only() {
  RecentPlays p;
  TEST_ASSERT_FALSE(p.playedWithin(&g_types[0], kWindowMs, 5000));
  p.record(&g_types[0], 5000);
  TEST_ASSERT_TRUE(p.playedWithin(&g_types[0], kWindowMs, 5000 + kWindowMs - 1));
  TEST_ASSERT_FALSE(p.playedWithin(&g_types[0], kWindowMs, 5000 + kWindowMs));
  TEST_ASSERT_FALSE(p.playedWithin(&g_types[1], kWindowMs, 5001));
  TEST_ASSERT_FALSE(p.playedWithin(&g_types[0], 0, 5000));
  TEST_ASSERT_FALSE(p.playedWithin(nullptr, kWindowMs, 5000));
}

// A replay moves the type's stamp forward instead of taking a second slot, so
// kSlots distinct types still fit after it.
void test_replay_refreshes_its_slot() {
  RecentPlays p;
  p.record(&g_types[0], 1000);
  p.record(&g_types[0], 100000);
  TEST_ASSERT_TRUE(p.playedWithin(&g_types[0], kWindowMs, 1000 + kWindowMs + 10));
  for (size_t i = 1; i < RecentPlays::kSlots; ++i) p.record(&g_types[i], 100000 + i);
  TEST_ASSERT_TRUE(p.playedWithin(&g_types[0], kWindowMs, 1000 + kWindowMs + 10));
  for (size_t i = 1; i < RecentPlays::kSlots; ++i) {
    TEST_ASSERT_TRUE(p.playedWithin(&g_types[i], kWindowMs, 100100));
  }
}

// One type past capacity drops the oldest-played type and no other.
void test_full_table_drops_the_oldest() {
  RecentPlays p;
  const size_t oldest = 5;
  for (size_t i = 0; i < RecentPlays::kSlots; ++i) {
    p.record(&g_types[i], i == oldest ? 1000 : 2000 + i);
  }
  p.record(&g_types[RecentPlays::kSlots], 3000);
  for (size_t i = 0; i <= RecentPlays::kSlots; ++i) {
    TEST_ASSERT_EQUAL(i != oldest, p.playedWithin(&g_types[i], kWindowMs, 3001));
  }
}

void test_retired_stamp_stays_old_a_wrap_later() {
  RecentPlays p;
  const uint32_t playedMs = 1000;
  p.record(&g_types[0], playedMs);
  for (uint64_t now = playedMs; now < playedMs + (1ull << 32); now += 1ull << 28) {
    p.retire(static_cast<uint32_t>(now));
  }
  TEST_ASSERT_FALSE(p.playedWithin(&g_types[0], kWindowMs, playedMs + 10));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_played_inside_its_window_only);
  RUN_TEST(test_replay_refreshes_its_slot);
  RUN_TEST(test_full_table_drops_the_oldest);
  RUN_TEST(test_retired_stamp_stays_old_a_wrap_later);
  return UNITY_END();
}
