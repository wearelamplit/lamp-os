// Native-host tests for webappShouldTeardown: the pure boot-window expiry
// decision, isolated from Arduino/WiFi so millis() wrap-safety is testable.

#include <unity.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include "../../src/components/webapp/webapp_deadline.hpp"

using namespace webapp;

void setUp(void) {}
void tearDown(void) {}

void test_never_expire_always_false() {
  TEST_ASSERT_FALSE(webappShouldTeardown(0, 1000, true));
  TEST_ASSERT_FALSE(webappShouldTeardown(5, UINT32_MAX, true));
  // Just after a millis() wrap, deadline still far ahead: must not tear down.
  TEST_ASSERT_FALSE(webappShouldTeardown(10, 0xFFFF0000UL, true));
}

void test_finite_deadline_before_and_after() {
  TEST_ASSERT_FALSE(webappShouldTeardown(999, 1000, false));
  TEST_ASSERT_TRUE(webappShouldTeardown(1000, 1000, false));
  TEST_ASSERT_TRUE(webappShouldTeardown(2000, 1000, false));
}

void test_wrap_safe_finite() {
  // Deadline was computed pre-wrap (0xFFFFFF00 + 0x200 wraps to 0x100).
  const uint32_t deadline = static_cast<uint32_t>(0xFFFFFF00UL + 0x200UL);
  // now past the wrapped deadline: elapsed, tear down.
  TEST_ASSERT_TRUE(webappShouldTeardown(0x00000200UL, deadline, false));
  // now still before it (pre-wrap side): not yet.
  TEST_ASSERT_FALSE(webappShouldTeardown(0xFFFFFF80UL, deadline, false));
}

void test_restore_ble_reboots_once_idle_regardless_of_station() {
  // BLE down + idle window elapsed -> restore (reboot), even with a station
  // still on.
  TEST_ASSERT_TRUE(webappShouldRestoreBle(/*now=*/200000, /*deadlineMs=*/100000,
                                          /*bleDown=*/true));
  // fresh activity (deadline in future) -> hold BLE down.
  TEST_ASSERT_FALSE(webappShouldRestoreBle(150000, 300000, true));
  // BLE not down -> this path never fires.
  TEST_ASSERT_FALSE(webappShouldRestoreBle(200000, 100000, false));
}

// A web save's reboot fires once due; only an OTA in progress holds it.
void test_armed_reboot_waits_for_ota_only() {
  TEST_ASSERT_FALSE(webapp::webappRebootDue(1000, false, 1000, false));
  TEST_ASSERT_FALSE(webapp::webappRebootDue(999, true, 1000, false));
  TEST_ASSERT_TRUE(webapp::webappRebootDue(1000, true, 1000, false));
  TEST_ASSERT_FALSE(webapp::webappRebootDue(5000, true, 1000, true));
  TEST_ASSERT_TRUE(webapp::webappRebootDue(0x00000010u, true, 0xFFFFFFF0u, false));
}

// A reboot armed for millis() 0 still fires.
void test_a_reboot_armed_at_zero_fires() {
  TEST_ASSERT_TRUE(webapp::webappRebootDue(0, true, 0, false));
  TEST_ASSERT_TRUE(webapp::webappRebootDue(5, true, 0, false));
}

// A stuck OTA holds the reboot only kRebootOtaWaitMaxMs past its time.
void test_armed_reboot_waits_out_ota_up_to_the_ceiling() {
  const uint32_t at = 1000;
  TEST_ASSERT_FALSE(webapp::webappRebootDue(at + webapp::kRebootOtaWaitMaxMs - 1, true, at, true));
  TEST_ASSERT_TRUE(webapp::webappRebootDue(at + webapp::kRebootOtaWaitMaxMs, true, at, true));
  TEST_ASSERT_TRUE(webapp::webappRebootDue(0x00000010u + webapp::kRebootOtaWaitMaxMs, true,
                                           0x00000010u, true));
  TEST_ASSERT_FALSE(webapp::webappRebootDue(at + webapp::kRebootOtaWaitMaxMs, false, at, true));
}

// A deadline that passed while a station stayed joined, then over 24.8 days
// with no request: held each loop, it still reads as passed when the station
// leaves, so the AP comes down.
void test_passed_deadline_stays_passed_past_half_a_wrap() {
  const uint32_t deadline = 1000;
  uint32_t held = deadline;
  for (uint64_t t = deadline; t <= deadline + (1ull << 31); t += 1ull << 28) {
    held = webapp::holdPassedDeadline(static_cast<uint32_t>(t), held);
  }
  const uint32_t now = deadline + (1u << 31) + 5000;
  TEST_ASSERT_TRUE(webapp::webappShouldTeardown(now, webapp::holdPassedDeadline(now, held), false));
}

// A deadline still ahead is left alone.
void test_hold_leaves_a_deadline_ahead() {
  TEST_ASSERT_EQUAL_UINT32(9000, webapp::holdPassedDeadline(5000, 9000));
}

// The idle deadline passed; the loop loads it to hold it, then a request on
// another task raises it before the hold lands. The raise must survive.
void test_raise_between_load_and_hold_wins() {
  std::atomic<uint32_t> deadline{1000};
  const uint32_t now = 5000;
  const uint32_t seen = deadline.load();
  webapp::raiseDeadline(deadline, now + 120000, deadline.load());
  webapp::holdPassedDeadline(deadline, now, seen);
  TEST_ASSERT_EQUAL_UINT32(now + 120000, deadline.load());
}

// A raise never lowers a later deadline, and a held deadline is raised past.
void test_raise_only_moves_the_deadline_later() {
  std::atomic<uint32_t> deadline{9000};
  webapp::raiseDeadline(deadline, 8000, deadline.load());
  TEST_ASSERT_EQUAL_UINT32(9000, deadline.load());
  webapp::holdPassedDeadline(deadline, 9500, deadline.load());
  TEST_ASSERT_EQUAL_UINT32(9500, deadline.load());
  webapp::raiseDeadline(deadline, 9600, deadline.load());
  TEST_ASSERT_EQUAL_UINT32(9600, deadline.load());
}

// Another write raised the deadline after this raise loaded it: the raise
// must not lower it.
void test_raise_never_lowers_a_later_write() {
  std::atomic<uint32_t> deadline{9000};
  const uint32_t staleSeen = 1000;
  webapp::raiseDeadline(deadline, 8000, staleSeen);
  TEST_ASSERT_EQUAL_UINT32(9000, deadline.load());
}

// --- OtaApFollower: the softAP across an OTA radio pause ---

using webapp::OtaApFollower;

// The pause takes an AP that is up down, and its end brings it back, once.
void test_ota_pause_stops_the_ap_and_its_end_restores_it() {
  OtaApFollower f;
  TEST_ASSERT_TRUE(f.stop(/*radioPaused=*/true, /*apTakenDown=*/true));
  TEST_ASSERT_FALSE(f.restore(/*radioPaused=*/true, /*apWanted=*/true));
  TEST_ASSERT_TRUE(f.restore(false, true));
  TEST_ASSERT_FALSE(f.restore(false, true));
}

// A second pause starting before the first one's restore ran finds the AP
// already down; the AP still comes back after it.
void test_reentered_pause_keeps_the_ap_owed() {
  OtaApFollower f;
  TEST_ASSERT_TRUE(f.stop(true, true));
  TEST_ASSERT_FALSE(f.stop(true, false));
  TEST_ASSERT_TRUE(f.restore(false, true));
}

// An AP that was already down when the pause began stays down after it.
void test_ota_pause_leaves_a_down_ap_down() {
  OtaApFollower f;
  TEST_ASSERT_FALSE(f.stop(true, false));
  TEST_ASSERT_FALSE(f.restore(false, true));
}

// An AP the webapp would have closed meanwhile is not brought back, and the
// next pause, finding the AP down, owes nothing.
void test_ota_pause_end_skips_an_ap_no_longer_wanted() {
  OtaApFollower f;
  TEST_ASSERT_TRUE(f.stop(true, true));
  TEST_ASSERT_FALSE(f.restore(false, false));
  TEST_ASSERT_FALSE(f.stop(true, false));
  TEST_ASSERT_FALSE(f.restore(false, true));
}

// No pause, no AP change.
void test_no_pause_no_stop() {
  OtaApFollower f;
  TEST_ASSERT_FALSE(f.stop(false, true));
  TEST_ASSERT_FALSE(f.restore(false, true));
}

// A served page (BLE down for it) always gets its AP back; otherwise only a
// window the webapp would still keep open does.
void test_ap_wanted_after_ota() {
  TEST_ASSERT_TRUE(webapp::apWantedAfterOta(5000, 9000, false, false));
  TEST_ASSERT_FALSE(webapp::apWantedAfterOta(9000, 9000, false, false));
  TEST_ASSERT_TRUE(webapp::apWantedAfterOta(9000, 9000, true, false));
  TEST_ASSERT_TRUE(webapp::apWantedAfterOta(9000, 1000, false, true));
  TEST_ASSERT_TRUE(webapp::apWantedAfterOta(0xFFFFFF80u, 0x00000100u, false, false));
  TEST_ASSERT_FALSE(webapp::apWantedAfterOta(0x00000200u, 0x00000100u, false, false));
}


using webapp::SoftApState;

static void spinUntil(const std::atomic<bool>& flag, int timeoutMs) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (!flag.load() && std::chrono::steady_clock::now() < until) std::this_thread::yield();
}

// The loop and the OTA pausing task both take the AP down. A second caller
// arriving while the first is still inside its teardown runs nothing.
void test_ap_teardown_runs_once_across_two_tasks() {
  SoftApState ap;
  ap.markUp();
  std::atomic<int> runs{0};
  std::atomic<bool> firstInside{false};
  std::atomic<bool> secondReturned{false};
  bool firstClaimed = false;
  std::thread first([&] {
    firstClaimed = ap.tearDown([&] {
      ++runs;
      firstInside = true;
      spinUntil(secondReturned, 500);
    });
  });
  spinUntil(firstInside, 500);
  const bool secondClaimed = ap.tearDown([&] { ++runs; });
  secondReturned = true;
  first.join();
  TEST_ASSERT_TRUE(firstClaimed);
  TEST_ASSERT_FALSE(secondClaimed);
  TEST_ASSERT_EQUAL(1, runs.load());
  TEST_ASSERT_FALSE(ap.up());
}

// A down AP has nothing to tear down; each up period gets one teardown.
void test_ap_teardown_once_per_up_period() {
  SoftApState ap;
  int runs = 0;
  TEST_ASSERT_FALSE(ap.up());
  TEST_ASSERT_FALSE(ap.tearDown([&] { ++runs; }));
  ap.markUp();
  TEST_ASSERT_TRUE(ap.up());
  TEST_ASSERT_TRUE(ap.tearDown([&] { ++runs; }));
  TEST_ASSERT_FALSE(ap.tearDown([&] { ++runs; }));
  ap.markUp();
  TEST_ASSERT_TRUE(ap.tearDown([&] { ++runs; }));
  TEST_ASSERT_EQUAL(2, runs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_never_expire_always_false);
  RUN_TEST(test_finite_deadline_before_and_after);
  RUN_TEST(test_wrap_safe_finite);
  RUN_TEST(test_restore_ble_reboots_once_idle_regardless_of_station);
  RUN_TEST(test_armed_reboot_waits_for_ota_only);
  RUN_TEST(test_armed_reboot_waits_out_ota_up_to_the_ceiling);
  RUN_TEST(test_a_reboot_armed_at_zero_fires);
  RUN_TEST(test_passed_deadline_stays_passed_past_half_a_wrap);
  RUN_TEST(test_hold_leaves_a_deadline_ahead);
  RUN_TEST(test_raise_between_load_and_hold_wins);
  RUN_TEST(test_raise_only_moves_the_deadline_later);
  RUN_TEST(test_raise_never_lowers_a_later_write);
  RUN_TEST(test_ota_pause_stops_the_ap_and_its_end_restores_it);
  RUN_TEST(test_reentered_pause_keeps_the_ap_owed);
  RUN_TEST(test_ota_pause_leaves_a_down_ap_down);
  RUN_TEST(test_ota_pause_end_skips_an_ap_no_longer_wanted);
  RUN_TEST(test_no_pause_no_stop);
  RUN_TEST(test_ap_wanted_after_ota);
  RUN_TEST(test_ap_teardown_runs_once_across_two_tasks);
  RUN_TEST(test_ap_teardown_once_per_up_period);
  return UNITY_END();
}
