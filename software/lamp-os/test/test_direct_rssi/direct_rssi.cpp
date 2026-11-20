// DirectRssi: a peer's direct ESP-NOW reading, its expiry, and the OTA offer
// gate's hold at the real floor.

#include <unity.h>

#include <cstdint>

#include "components/network/mesh/direct_rssi.hpp"

using lamp::DirectRssi;

namespace {

constexpr int8_t kFloor = lamp_protocol::kOtaMinRssiDbm;
constexpr uint32_t kHello = lamp_protocol::kLampHelloIntervalMs;
constexpr uint32_t kT0 = 7000;

// Records `rssi` every `everyMs` from `fromMs` through `toMs` inclusive.
void feed(DirectRssi& d, int8_t rssi, uint32_t fromMs, uint32_t toMs, uint32_t everyMs) {
  for (uint32_t t = fromMs; t <= toMs; t += everyMs) d.record(rssi, t);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_no_reading_is_unknown_and_never_holds() {
  const DirectRssi d;
  TEST_ASSERT_EQUAL_INT8(-127, d.fresh(kT0));
  TEST_ASSERT_FALSE(d.holdsOtaFloor(kT0));
}

void test_the_unavailable_sentinel_records_nothing() {
  DirectRssi d;
  d.record(-70, kT0);
  d.record(-127, kT0 + 1000);
  TEST_ASSERT_EQUAL_INT8(-70, d.fresh(kT0 + 1000));
  TEST_ASSERT_EQUAL_INT8(-70, d.fresh(kT0 + DirectRssi::kMaxAgeMs));
  TEST_ASSERT_EQUAL_INT8(-127, d.fresh(kT0 + DirectRssi::kMaxAgeMs + 1));
}

// The recv task can stamp a reading after the loop read its clock; that
// reading is fresh at the earlier now, with its whole span ahead.
void test_a_reading_stamped_ahead_of_now_is_fresh() {
  DirectRssi d;
  d.record(-70, kT0 + 2);
  TEST_ASSERT_EQUAL_INT8(-70, d.fresh(kT0));
  TEST_ASSERT_EQUAL_UINT32(DirectRssi::kMaxAgeMs, d.freshForMs(kT0));
}

// A hold that has just reached the window still holds when the loop's clock
// trails the recv task's latest stamp.
void test_a_just_held_link_holds_at_a_now_behind_the_stamp() {
  DirectRssi d;
  const uint32_t last = kT0 + 9 * 5000;
  feed(d, kFloor, kT0, last, 5000);
  TEST_ASSERT_TRUE(d.holdsOtaFloor(last - 2));
}

// freshForMs counts down to the moment fresh() lets go of the reading.
void test_fresh_span_ends_when_the_reading_goes_stale() {
  DirectRssi d;
  d.record(-70, kT0);
  TEST_ASSERT_EQUAL_UINT32(DirectRssi::kMaxAgeMs - 1234, d.freshForMs(kT0 + 1234));
  TEST_ASSERT_EQUAL_UINT32(0, d.freshForMs(kT0 + DirectRssi::kMaxAgeMs));
  TEST_ASSERT_EQUAL_INT8(-70, d.fresh(kT0 + DirectRssi::kMaxAgeMs));
  TEST_ASSERT_EQUAL_UINT32(0, d.freshForMs(kT0 + DirectRssi::kMaxAgeMs + 1));
}

// A reading stays for kMaxAgeMs: one lost steady HELLO keeps it, two expire it.
void test_a_reading_goes_stale_after_two_lost_hellos() {
  static_assert(2 * kHello <= DirectRssi::kMaxAgeMs && DirectRssi::kMaxAgeMs < 3 * kHello,
                "one lost HELLO keeps the reading, two expire it");
  DirectRssi d;
  d.record(-66, kT0);
  TEST_ASSERT_EQUAL_INT8(-66, d.fresh(kT0 + 2 * kHello));
  TEST_ASSERT_EQUAL_INT8(-127, d.fresh(kT0 + 3 * kHello));
}

// The steady cadence gives two readings inside the hold; the gate opens once
// the hold has run from the first, and not before.
void test_steady_cadence_at_the_floor_holds_after_the_window() {
  DirectRssi d;
  feed(d, kFloor, kT0, kT0 + DirectRssi::kOtaHoldMs, kHello);
  TEST_ASSERT_FALSE(d.holdsOtaFloor(kT0 + DirectRssi::kOtaHoldMs - 1));
  TEST_ASSERT_TRUE(d.holdsOtaFloor(kT0 + DirectRssi::kOtaHoldMs));
}

// A single reading never proves a hold, however long it stays fresh.
void test_one_reading_never_holds() {
  static_assert(DirectRssi::kOtaHoldMs < DirectRssi::kMaxAgeMs,
                "a lone reading is still fresh when the hold has run");
  DirectRssi d;
  d.record(-50, kT0);
  TEST_ASSERT_FALSE(d.holdsOtaFloor(kT0 + DirectRssi::kOtaHoldMs));
  TEST_ASSERT_FALSE(d.holdsOtaFloor(kT0 + DirectRssi::kMaxAgeMs));
}

// One reading below the floor restarts the clock from the next good one.
void test_one_reading_below_the_floor_restarts_the_hold() {
  DirectRssi d;
  feed(d, kFloor + 4, kT0, kT0 + kHello, kHello);
  TEST_ASSERT_TRUE(d.holdsOtaFloor(kT0 + DirectRssi::kOtaHoldMs));
  const uint32_t dip = kT0 + 2 * kHello;
  d.record(kFloor - 1, dip);
  TEST_ASSERT_FALSE(d.holdsOtaFloor(dip));
  TEST_ASSERT_EQUAL_INT8(kFloor - 1, d.fresh(dip));
  const uint32_t back = dip + kHello;
  feed(d, kFloor + 4, back, back + kHello, kHello);
  TEST_ASSERT_FALSE(d.holdsOtaFloor(back + DirectRssi::kOtaHoldMs - 1));
  TEST_ASSERT_TRUE(d.holdsOtaFloor(back + DirectRssi::kOtaHoldMs));
}

// A held link closes once its latest reading goes stale.
void test_a_stale_hold_closes() {
  DirectRssi d;
  feed(d, kFloor + 9, kT0, kT0 + 3 * kHello, kHello);
  const uint32_t last = kT0 + 3 * kHello;
  TEST_ASSERT_TRUE(d.holdsOtaFloor(last + DirectRssi::kMaxAgeMs));
  TEST_ASSERT_FALSE(d.holdsOtaFloor(last + DirectRssi::kMaxAgeMs + 1));
}

// A reading after the last went stale starts a new hold, so an old reading
// cannot count toward it.
void test_a_gap_past_expiry_restarts_the_hold() {
  DirectRssi d;
  d.record(kFloor + 3, kT0);
  const uint32_t later = kT0 + DirectRssi::kMaxAgeMs + 1;
  d.record(kFloor + 3, later);
  TEST_ASSERT_FALSE(d.holdsOtaFloor(later));
  d.record(kFloor + 3, later + kHello);
  TEST_ASSERT_FALSE(d.holdsOtaFloor(later + DirectRssi::kOtaHoldMs - 1));
  TEST_ASSERT_TRUE(d.holdsOtaFloor(later + DirectRssi::kOtaHoldMs));
}

// Four steady readings span more than the held span's 16 bits; it saturates
// rather than wraps, so the hold stays open.
void test_the_held_span_saturates() {
  DirectRssi d;
  const uint32_t last = kT0 + 3 * kHello;
  feed(d, kFloor + 1, kT0, last, kHello);
  TEST_ASSERT_TRUE(d.holdsOtaFloor(last));
}

// The boot burst's 5 s cadence for 256 readings: the count saturates rather
// than wraps, so the hold stays open.
void test_the_reading_count_saturates() {
  DirectRssi d;
  const uint32_t last = kT0 + 255 * 5000;
  feed(d, kFloor + 1, kT0, last, 5000);
  TEST_ASSERT_TRUE(d.holdsOtaFloor(last));
}

// A reading the roster retires keeps reading stale one millis() wrap later.
void test_a_retired_reading_stays_stale_across_the_wrap() {
  DirectRssi d;
  feed(d, kFloor + 6, kT0, kT0 + 2 * kHello, kHello);
  const uint32_t last = kT0 + 2 * kHello;
  for (uint64_t t = last; t <= uint64_t{last} + (uint64_t{1} << 32); t += 3600000) {
    d.retire(static_cast<uint32_t>(t));
  }
  const uint32_t wrapped = last + 1000;  // one wrap past the last reading
  TEST_ASSERT_EQUAL_INT8(-127, d.fresh(wrapped));
  TEST_ASSERT_FALSE(d.holdsOtaFloor(wrapped));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_no_reading_is_unknown_and_never_holds);
  RUN_TEST(test_the_unavailable_sentinel_records_nothing);
  RUN_TEST(test_a_reading_stamped_ahead_of_now_is_fresh);
  RUN_TEST(test_fresh_span_ends_when_the_reading_goes_stale);
  RUN_TEST(test_a_just_held_link_holds_at_a_now_behind_the_stamp);
  RUN_TEST(test_a_reading_goes_stale_after_two_lost_hellos);
  RUN_TEST(test_steady_cadence_at_the_floor_holds_after_the_window);
  RUN_TEST(test_one_reading_never_holds);
  RUN_TEST(test_one_reading_below_the_floor_restarts_the_hold);
  RUN_TEST(test_a_stale_hold_closes);
  RUN_TEST(test_a_gap_past_expiry_restarts_the_hold);
  RUN_TEST(test_the_held_span_saturates);
  RUN_TEST(test_the_reading_count_saturates);
  RUN_TEST(test_a_retired_reading_stays_stale_across_the_wrap);
  return UNITY_END();
}
