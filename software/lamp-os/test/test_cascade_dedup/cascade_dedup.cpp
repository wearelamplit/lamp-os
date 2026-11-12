// Native tests for RecentCascade, the ring that keeps a TARGET_BOTH
// expression's shade and base auto-triggers to one cascade and one MSG_EVENT
// announce per logical trigger.

#include <unity.h>

#include <cstdint>
#include <string>

#include "expressions/recent_cascade.hpp"

void setUp(void) {}
void tearDown(void) {}

void test_first_record_then_seen_within_window() {
  // Fresh ring: nothing has fired. record() at t=100, then seen() at
  // t=200 (within the 250 ms window) returns true — the second entry of
  // a TARGET_BOTH double-fire would be suppressed.
  lamp::RecentCascade ring;

  TEST_ASSERT_FALSE(ring.seen("glitchy", 0, 100));
  ring.record("glitchy", 0, 100);
  TEST_ASSERT_TRUE(ring.seen("glitchy", 0, 200));
}

void test_record_outside_window_fires_fresh() {
  // record at t=100; seen() at t=400 returns false because 300 ms > the
  // 250 ms window. A deliberate back-to-back manual trigger after the
  // window must still cascade.
  lamp::RecentCascade ring;

  ring.record("pulse", 0, 100);
  TEST_ASSERT_FALSE(ring.seen("pulse", 0, 400));
}

void test_distinct_type_is_independent() {
  // Same time, same intervalIdx, but different type — must NOT collide.
  // Two unrelated expressions firing in the same control() tick both
  // get their cascades through.
  lamp::RecentCascade ring;

  ring.record("glitchy", 0, 100);
  TEST_ASSERT_FALSE(ring.seen("pulse", 0, 100));
}

void test_distinct_intervalIdx_is_independent() {
  // Same type, same time, different intervalIdx — independent. (Reserved
  // for a future where two configured entries of the same type can
  // coexist; today the manager passes a stable per-entry key.)
  lamp::RecentCascade ring;

  ring.record("breathing", 0, 100);
  TEST_ASSERT_FALSE(ring.seen("breathing", 1, 100));
}

void test_ring_evicts_oldest_after_capacity_plus_one() {
  // Insert CAPACITY+1 distinct entries; the very first one (oldest by
  // insertion order) is evicted and looks unseen, while the most-recent
  // CAPACITY entries are still seen() within the window.
  lamp::RecentCascade ring;

  for (uint32_t i = 0; i < lamp::RecentCascade::CAPACITY; ++i) {
    ring.record("t" + std::to_string(i), 0, 100);
  }
  // All CAPACITY entries currently in the ring — each is seen() at t=100.
  TEST_ASSERT_TRUE(ring.seen("t0", 0, 100));
  TEST_ASSERT_TRUE(ring.seen(
      "t" + std::to_string(lamp::RecentCascade::CAPACITY - 1), 0, 100));

  // One more distinct entry — overwrites slot[0], which held "t0".
  ring.record("t" + std::to_string(lamp::RecentCascade::CAPACITY), 0, 100);

  // "t0" is evicted; seen() returns false. The newest still seen.
  TEST_ASSERT_FALSE(ring.seen("t0", 0, 100));
  TEST_ASSERT_TRUE(ring.seen(
      "t" + std::to_string(lamp::RecentCascade::CAPACITY), 0, 100));
}

// Event-announce dedup (recentEvents_) uses the same ring type and keying as
// recentCascades_. A TARGET_BOTH expression's shade and base entries auto-fire
// microseconds apart; the second emitEvent call must be suppressed so observers
// receive exactly one announce per logical trigger.
void test_event_dedup_suppresses_target_both_second_fire() {
  lamp::RecentCascade ring;  // models recentEvents_ for a TARGET_BOTH expression

  // Both shade and base report target=3 (TARGET_BOTH). Simulate the shade
  // entry firing first (records) then the base entry firing within the window.
  const uint32_t target_both_idx = 3;
  TEST_ASSERT_FALSE(ring.seen("glitchy", target_both_idx, 100));
  ring.record("glitchy", target_both_idx, 100);
  // Base fires 50 µs later — well within the 250 ms window.
  TEST_ASSERT_TRUE(ring.seen("glitchy", target_both_idx, 100));
}

// A fire recorded once, then a whole wrap with no other: the ring retired
// every few days must not read it as seen again.
void test_fire_stays_unseen_a_wrap_later() {
  lamp::RecentCascade ring;
  const uint32_t firedMs = 1000;
  ring.record("pulse", 0, firedMs);
  for (uint64_t t = firedMs; t < firedMs + (1ull << 32); t += 1ull << 28) {
    ring.retire(static_cast<uint32_t>(t));
  }
  TEST_ASSERT_FALSE(ring.seen("pulse", 0, firedMs + 100));
}

// Inside the window retire keeps the fire.
void test_retire_keeps_a_fire_inside_the_window() {
  lamp::RecentCascade ring;
  ring.record("pulse", 0, 1000);
  ring.retire(1000 + lamp::kCascadeDedupWindowMs);
  TEST_ASSERT_TRUE(ring.seen("pulse", 0, 1000 + lamp::kCascadeDedupWindowMs));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_first_record_then_seen_within_window);
  RUN_TEST(test_record_outside_window_fires_fresh);
  RUN_TEST(test_distinct_type_is_independent);
  RUN_TEST(test_distinct_intervalIdx_is_independent);
  RUN_TEST(test_ring_evicts_oldest_after_capacity_plus_one);
  RUN_TEST(test_event_dedup_suppresses_target_both_second_fire);
  RUN_TEST(test_fire_stays_unseen_a_wrap_later);
  RUN_TEST(test_retire_keeps_a_fire_inside_the_window);

  return UNITY_END();
}
