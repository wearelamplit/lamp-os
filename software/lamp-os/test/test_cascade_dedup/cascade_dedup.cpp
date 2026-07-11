// Native-host unit tests for RecentCascade data behavior.
//
// Context: audit finding [HIGH] flagged Expression::trigger() invoking
// mgr->onExpressionFired(this) unconditionally — for TARGET_BOTH expressions
// the per-entry auto-trigger from Expression::control() fires shade+base
// entries independently, each calling maybeCascade(). The codebase's
// "cascade once per logical trigger" invariant was violated on the auto-
// trigger path, doubling mesh traffic.
//
// Fix in expression_manager.{hpp,cpp}: a small RecentCascade ring records
// (type, intervalIdx, fireMs) for the last N fires. Before maybeCascade
// runs the dispatch, the manager asks the ring whether a matching
// (type, intervalIdx) was already cascaded within kCascadeDedupWindowMs;
// if so, the second-and-later entries from the SAME logical trigger are
// suppressed. Outside the window the ring lets the cascade through.
//
// Test contract pinned here (mirror of the data-shape in the manager):
//   - record(type, idx, t) marks (type, idx) as fired at t
//   - seen(type, idx, t) returns true iff a record(type, idx, t') exists
//     with |t - t'| <= kCascadeDedupWindowMs
//   - keying is (type, intervalIdx): distinct types are independent;
//     same type / different idx are independent
//   - the ring holds at most CAPACITY entries; oldest entry is evicted
//     when a CAPACITY+1th distinct fire is recorded.
//
// This test re-declares the ring inline (test_dedup_ring / test_color
// pattern) so it stays self-contained — no Arduino, no FreeRTOS, no
// production header pull-in. If you change the ring's keying or eviction
// policy in expression_manager.hpp, mirror here.

#include <unity.h>

#include <cstdint>
#include <cstddef>
#include <string>

namespace lamp {

// Dedup window: tight enough to coalesce a TARGET_BOTH double-fire
// (which happens in the same control() tick — microseconds apart) while
// loose enough not to suppress a deliberate back-to-back manual trigger.
// 250 ms is the same constant used in expression_manager.cpp.
static constexpr uint32_t kCascadeDedupWindowMs = 250;

// Mirror of the production ring's data behavior. Storage-only — production
// pairs this with a millis() call site. CAPACITY=8 is plenty: a logical
// trigger emits at most 2 entries (TARGET_BOTH), and the window is short.
class RecentCascade {
 public:
  static constexpr size_t CAPACITY = 8;

  // Has (type, intervalIdx) been cascaded within the dedup window of `nowMs`?
  bool seen(const std::string& type, uint32_t intervalIdx, uint32_t nowMs) const {
    for (size_t i = 0; i < CAPACITY; ++i) {
      const Entry& e = entries_[i];
      if (!e.used) continue;
      if (e.type != type || e.intervalIdx != intervalIdx) continue;
      if (nowMs - e.fireMs <= kCascadeDedupWindowMs) return true;
    }
    return false;
  }

  // Record a fresh fire. Always evicts head_ slot (oldest by insertion).
  void record(const std::string& type, uint32_t intervalIdx, uint32_t nowMs) {
    Entry& slot = entries_[head_];
    slot.used = true;
    slot.type = type;
    slot.intervalIdx = intervalIdx;
    slot.fireMs = nowMs;
    head_ = (head_ + 1) % CAPACITY;
  }

 private:
  struct Entry {
    bool used = false;
    std::string type;
    uint32_t intervalIdx = 0;
    uint32_t fireMs = 0;
  };
  Entry entries_[CAPACITY];
  size_t head_ = 0;
};

}  // namespace lamp

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

  return UNITY_END();
}
