// Native-host mirror-class test of the CHAR_COMMIT drain's state machine.
//
// WHAT THIS TEST ACTUALLY CHECKS:
//   Mirror of the production drain logic in lamp.cpp's
//   CHAR_COMMIT block. Pinning the state machine invariants — signal
//   latching, idle-window enforcement, hash-dedup, OTA defer, force-
//   flush bypass, persist-failure retry. If production state-machine
//   semantics change, the mock here must change too — same mirror-class
//   discipline as the other apply tests.
//
// WHAT THIS TEST DOES NOT CATCH:
//   - The real drain calling persistConfig() with the wrong serialized
//     state — that's a runtime concern not testable in native env
//     because Config pulls in Preferences/NimBLE deps.

#include <unity.h>

#include <cstdint>
#include <string>

namespace lamp {

struct CommitDrain {
  // Cross-core flags — production uses volatile bool, mirror with plain
  // bool since native env has no concurrent producer.
  bool      pending = false;
  bool      forceFlush = false;

  // Loop-task-only state. Anonymous namespace in production.
  bool      dirty = false;
  uint32_t  lastSignalMs = 0;
  uint32_t  lastPersistedHash = 0;
  uint32_t  flushIdleMs = 1500;

  // Test hooks — fake the side effects.
  bool      otaInProgress = false;
  bool      persistShouldSucceed = true;
  int       persistCallCount = 0;
  int       invalidateCallCount = 0;
  int       notifyCallCount = 0;
  std::string currentSerialized = "{}";

  // FNV-1a — must match production fnv1aHash in lamp.cpp.
  static uint32_t fnv1aHash(const std::string& s) {
    uint32_t h = 2166136261u;
    for (auto c : s) {
      h ^= static_cast<uint8_t>(c);
      h *= 16777619u;
    }
    return h;
  }

  // Mirror of the production drain at one tick.
  void tick(uint32_t nowMs) {
    if (pending) {
      pending = false;
      dirty = true;
      lastSignalMs = nowMs;
    }
    // Clear stale forceFlush if no work pending (fix from eb51830 so a
    // BLE disconnect without a prior commit doesn't bypass the next
    // session's debounce window).
    if (!dirty) {
      forceFlush = false;
    }
    if (!dirty) return;
    bool windowReached = forceFlush ||
                          (nowMs - lastSignalMs) >= flushIdleMs;
    if (!windowReached) return;
    if (otaInProgress) {
      // Don't clear dirty — retry next tick.
      return;
    }
    forceFlush = false;
    uint32_t hash = fnv1aHash(currentSerialized);
    if (hash == lastPersistedHash) {
      dirty = false;
      return;
    }
    persistCallCount++;
    if (persistShouldSucceed) {
      lastPersistedHash = hash;
      invalidateCallCount++;
      notifyCallCount++;
      dirty = false;
    } else {
      notifyCallCount++;
      // dirty stays true — retry next tick.
    }
  }
};

}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

void test_signal_then_idle_window_then_persist() {
  lamp::CommitDrain d;
  d.currentSerialized = R"({"lamp":{"brightness":50}})";
  d.pending = true;
  d.tick(1000);  // signal latches, dirty=true, lastSignalMs=1000
  TEST_ASSERT_TRUE(d.dirty);
  TEST_ASSERT_EQUAL_INT(0, d.persistCallCount);  // not yet — idle window not reached

  d.tick(2000);  // 1000ms after signal — still inside 1500ms window
  TEST_ASSERT_EQUAL_INT(0, d.persistCallCount);

  d.tick(2500);  // 1500ms after signal — flush
  TEST_ASSERT_EQUAL_INT(1, d.persistCallCount);
  TEST_ASSERT_FALSE(d.dirty);
}

void test_signal_resets_idle_window() {
  lamp::CommitDrain d;
  d.currentSerialized = R"({"lamp":{"brightness":50}})";
  d.pending = true;
  d.tick(1000);
  d.pending = true;
  d.tick(2000);  // signal again — resets window to 2000
  d.tick(3000);  // 1000ms after the later signal — not yet
  TEST_ASSERT_EQUAL_INT(0, d.persistCallCount);
  d.tick(3500);  // 1500ms after the later signal — flush
  TEST_ASSERT_EQUAL_INT(1, d.persistCallCount);
}

void test_hash_dedup_skips_identical_persist() {
  lamp::CommitDrain d;
  d.currentSerialized = R"({"lamp":{"brightness":50}})";
  // First commit lands.
  d.pending = true;
  d.tick(1000);
  d.tick(2500);
  TEST_ASSERT_EQUAL_INT(1, d.persistCallCount);

  // Second commit signal with identical serialized state — should skip.
  d.pending = true;
  d.tick(3000);
  d.tick(4500);
  TEST_ASSERT_EQUAL_INT(1, d.persistCallCount);  // still 1 — deduped
  TEST_ASSERT_FALSE(d.dirty);
}

void test_ota_in_progress_defers_flush() {
  lamp::CommitDrain d;
  d.currentSerialized = R"({"lamp":{"brightness":50}})";
  d.otaInProgress = true;
  d.pending = true;
  d.tick(1000);
  d.tick(2500);  // would normally flush — OTA blocks
  TEST_ASSERT_EQUAL_INT(0, d.persistCallCount);
  TEST_ASSERT_TRUE(d.dirty);  // still dirty — will retry

  d.otaInProgress = false;
  d.tick(2500);  // OTA clear — flush fires this tick
  TEST_ASSERT_EQUAL_INT(1, d.persistCallCount);
  TEST_ASSERT_FALSE(d.dirty);
}

void test_force_flush_bypasses_idle_window() {
  lamp::CommitDrain d;
  d.currentSerialized = R"({"lamp":{"brightness":50}})";
  d.pending = true;
  d.tick(1000);  // dirty, lastSignalMs=1000

  d.forceFlush = true;
  d.tick(1100);  // 100ms after — would not normally flush
  TEST_ASSERT_EQUAL_INT(1, d.persistCallCount);
  TEST_ASSERT_FALSE(d.forceFlush);
}

void test_persist_failure_keeps_dirty_for_retry() {
  lamp::CommitDrain d;
  d.currentSerialized = R"({"lamp":{"brightness":50}})";
  d.persistShouldSucceed = false;
  d.pending = true;
  d.tick(1000);
  d.tick(2500);
  TEST_ASSERT_EQUAL_INT(1, d.persistCallCount);
  TEST_ASSERT_TRUE(d.dirty);  // failure — retry expected
  TEST_ASSERT_EQUAL_INT(1, d.notifyCallCount);  // notifyStateChange fires either way

  // Persist succeeds on retry tick.
  d.persistShouldSucceed = true;
  d.tick(2500);
  TEST_ASSERT_EQUAL_INT(2, d.persistCallCount);
  TEST_ASSERT_FALSE(d.dirty);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_signal_then_idle_window_then_persist);
  RUN_TEST(test_signal_resets_idle_window);
  RUN_TEST(test_hash_dedup_skips_identical_persist);
  RUN_TEST(test_ota_in_progress_defers_flush);
  RUN_TEST(test_force_flush_bypasses_idle_window);
  RUN_TEST(test_persist_failure_keeps_dirty_for_retry);
  return UNITY_END();
}
