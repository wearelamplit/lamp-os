#include <unity.h>

#include <initializer_list>

#include "status/status_cadence.hpp"

using wisp::StatusCadence;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kStepMs = 100;
constexpr uint32_t kSpanMs = 20000;

// Counts emits from polling every kStepMs over [fromMs, fromMs + spanMs).
int countEmits(StatusCadence& c, uint32_t fromMs, uint32_t spanMs) {
  int n = 0;
  for (uint32_t t = fromMs; t - fromMs < spanMs; t += kStepMs) {
    if (c.takeEmit(t, true)) ++n;
  }
  return n;
}

}  // namespace

// A request sends one status and opens no burst.
void test_request_emits_once_over_burst_span() {
  StatusCadence c;
  c.request();
  TEST_ASSERT_EQUAL_INT(1, countEmits(c, 40000, kSpanMs));
}

// A change re-sends every kBurstIntervalMs for kBurstMs, across a millis()
// wrap too.
void test_change_emits_burst() {
  for (const uint32_t start : {40000u, 0u - 5000u}) {
    StatusCadence c;
    c.change();
    TEST_ASSERT_EQUAL_INT(
        (StatusCadence::kBurstMs + StatusCadence::kBurstIntervalMs - 1) /
            StatusCadence::kBurstIntervalMs,
        countEmits(c, start, kSpanMs + 5 * kStepMs));
  }
}

// A request inside a running burst neither extends nor shortens it. The last
// burst emit lands where it would have without the request.
void test_request_inside_burst_keeps_burst_end() {
  StatusCadence c;
  const uint32_t start = 40000;
  c.change();
  uint32_t lastEmit = 0;
  for (uint32_t t = start; t - start < 2 * kSpanMs; t += kStepMs) {
    if (t == start + 7300) c.request();
    if (c.takeEmit(t, true)) lastEmit = t;
  }
  const uint32_t burstLastEmit =
      start + StatusCadence::kBurstMs - StatusCadence::kBurstIntervalMs;
  TEST_ASSERT_EQUAL_UINT32(burstLastEmit, lastEmit);
}

// A second change inside a running burst restarts its kBurstMs.
void test_change_inside_burst_restarts_burst() {
  StatusCadence c;
  const uint32_t start = 40000;
  const uint32_t again = start + 7300;
  c.change();
  uint32_t lastEmit = 0;
  for (uint32_t t = start; t - start < 3 * kSpanMs; t += kStepMs) {
    if (t == again) c.change();
    if (c.takeEmit(t, true)) lastEmit = t;
  }
  TEST_ASSERT_UINT32_WITHIN(StatusCadence::kBurstIntervalMs,
                            again + StatusCadence::kBurstMs, lastEmit);
}

// A request 1 s after an emit waits out the 5 s window, then fires.
void test_request_after_emit_deferred_not_dropped() {
  StatusCadence c;
  const uint32_t first = 40000;
  c.request();
  TEST_ASSERT_TRUE(c.takeEmit(first, true));
  c.request();
  uint32_t fired = 0;
  for (uint32_t t = first + 1000; t - first < kSpanMs; t += kStepMs) {
    if (c.takeEmit(t, true)) {
      fired = t;
      break;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(first + StatusCadence::kMinIntervalMs, fired);
}

// A due status waits while a wispConfig is too close behind, then goes out.
void test_held_until_status_clear() {
  StatusCadence c;
  c.request();
  TEST_ASSERT_FALSE(c.takeEmit(40000, false));
  TEST_ASSERT_TRUE(c.takeEmit(40100, true));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_request_emits_once_over_burst_span);
  RUN_TEST(test_change_emits_burst);
  RUN_TEST(test_request_inside_burst_keeps_burst_end);
  RUN_TEST(test_change_inside_burst_restarts_burst);
  RUN_TEST(test_request_after_emit_deferred_not_dropped);
  RUN_TEST(test_held_until_status_clear);
  return UNITY_END();
}
