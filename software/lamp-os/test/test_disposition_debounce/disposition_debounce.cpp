// Native-host unit tests for the DispositionDebouncer clock logic.
//
// Context: audit finding [CRITICAL] flagged NVS write amplification on
// disposition slider drag — `Config::setDisposition` and
// `setDispositionsFromJson` were calling `persistDispositions_()` on every
// update, eagerly burning NVS page wear (~100k writes per 4 KB page) on
// the slider drag UX.
//
// Fix: extract a tiny clock-only helper (`DispositionDebouncer`) that
// records "marked dirty at t" and exposes "should I flush now at t1?"
// (idle window elapsed since the most recent mark). `Config` calls
// `markDirty(now)` on every setter and polls `shouldFlush(now)` from the
// loop drain — only triggering the real NVS write once the user stops
// fiddling for kDispositionFlushIdleMs (5000 ms).
//
// Following test_color/test_fade/test_invocation convention: the helper
// is re-declared inline here so the native test doesn't try to link
// against `Config` (which transitively pulls in Arduino / Preferences /
// NimBLE). The shape must match `src/config/config.hpp` byte-for-byte;
// if you change the API there, mirror here.

#include <unity.h>

#include <cstdint>

namespace lamp {

// Mirror of src/config/config.hpp's DispositionDebouncer. Pure clock
// logic — no NVS, no I/O. The production class is a member of Config.
//
// Invariant: once markDirty(t) is called, shouldFlush(t1) returns true
// iff t1 - lastMarkMs >= idleMs AND there is something to flush. Calling
// clear() resets to a "not dirty" state — the next shouldFlush is false.
class DispositionDebouncer {
 public:
  explicit DispositionDebouncer(uint32_t idleMs) : idleMs_(idleMs) {}

  void markDirty(uint32_t nowMs) {
    dirty_ = true;
    lastMarkMs_ = nowMs;
  }

  bool dirty() const { return dirty_; }

  // Returns true iff dirty AND the idle window has elapsed since the
  // most recent markDirty. Caller is responsible for actually flushing
  // and then calling clear().
  bool shouldFlush(uint32_t nowMs) const {
    if (!dirty_) return false;
    // Use a subtraction-based comparison so millis() wraparound (every
    // ~49 days on ESP32) doesn't strand the dirty flag forever.
    return (nowMs - lastMarkMs_) >= idleMs_;
  }

  void clear() {
    dirty_ = false;
    lastMarkMs_ = 0;
  }

 private:
  bool dirty_ = false;
  uint32_t lastMarkMs_ = 0;
  uint32_t idleMs_;
};

static constexpr uint32_t kDispositionFlushIdleMs = 5000;

}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

void test_no_flush_when_not_dirty() {
  // Fresh debouncer at "now=10s" should never want to flush — no marks.
  lamp::DispositionDebouncer d(lamp::kDispositionFlushIdleMs);
  TEST_ASSERT_FALSE(d.dirty());
  TEST_ASSERT_FALSE(d.shouldFlush(0));
  TEST_ASSERT_FALSE(d.shouldFlush(10000));
  TEST_ASSERT_FALSE(d.shouldFlush(60000));
}

void test_no_flush_when_within_debounce_window() {
  // markDirty at t=1000ms, poll at t=2000ms (1s elapsed) — must NOT flush
  // because the 5s idle window hasn't elapsed.
  lamp::DispositionDebouncer d(lamp::kDispositionFlushIdleMs);
  d.markDirty(1000);
  TEST_ASSERT_TRUE(d.dirty());
  TEST_ASSERT_FALSE(d.shouldFlush(1000));
  TEST_ASSERT_FALSE(d.shouldFlush(2000));
  TEST_ASSERT_FALSE(d.shouldFlush(5999));  // 4999ms elapsed — still under.
}

void test_flush_at_exact_window_boundary() {
  // markDirty at t=0, poll at t=5000ms exactly — the >= comparison means
  // we should flush right at the boundary, not one tick later.
  lamp::DispositionDebouncer d(lamp::kDispositionFlushIdleMs);
  d.markDirty(0);
  TEST_ASSERT_FALSE(d.shouldFlush(4999));
  TEST_ASSERT_TRUE(d.shouldFlush(5000));
}

void test_flush_after_window_passes() {
  // Standard "user finishes slider drag, waits, persistor fires" case.
  lamp::DispositionDebouncer d(lamp::kDispositionFlushIdleMs);
  d.markDirty(100);
  TEST_ASSERT_TRUE(d.shouldFlush(100 + 5000));
  TEST_ASSERT_TRUE(d.shouldFlush(100 + 6000));
  TEST_ASSERT_TRUE(d.shouldFlush(100 + 60000));
}

void test_subsequent_mark_resets_window() {
  // Slider drag: many markDirty calls in rapid succession. Each one
  // should extend the idle window so a half-finished drag doesn't flush.
  lamp::DispositionDebouncer d(lamp::kDispositionFlushIdleMs);
  d.markDirty(0);
  // 4 seconds in, another touch — window restarts from this moment.
  d.markDirty(4000);
  // Now at t=6000, only 2s have elapsed since the last touch — no flush.
  TEST_ASSERT_FALSE(d.shouldFlush(6000));
  TEST_ASSERT_FALSE(d.shouldFlush(8999));
  // 5s after the last mark — flush.
  TEST_ASSERT_TRUE(d.shouldFlush(9000));
}

void test_clear_makes_subsequent_should_flush_false() {
  // After the caller actually persists, it must clear() so the next
  // tick of the loop drain doesn't keep firing.
  lamp::DispositionDebouncer d(lamp::kDispositionFlushIdleMs);
  d.markDirty(0);
  TEST_ASSERT_TRUE(d.shouldFlush(5000));
  d.clear();
  TEST_ASSERT_FALSE(d.dirty());
  TEST_ASSERT_FALSE(d.shouldFlush(10000));
  TEST_ASSERT_FALSE(d.shouldFlush(60000));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_no_flush_when_not_dirty);
  RUN_TEST(test_no_flush_when_within_debounce_window);
  RUN_TEST(test_flush_at_exact_window_boundary);
  RUN_TEST(test_flush_after_window_passes);
  RUN_TEST(test_subsequent_mark_resets_window);
  RUN_TEST(test_clear_makes_subsequent_should_flush_false);

  return UNITY_END();
}
