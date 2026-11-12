// Native-host unit tests for ota_quiet_mode. Links the production .cpp
// directly (same pattern as test_ota_channel / test_firmware_signature). The
// radio calls are no-ops off-device; a test swaps in fakes through s_radio.

#include <unity.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "components/firmware/ota_quiet_mode.hpp"
#include "components/firmware/ota_quiet_mode.cpp"

using lamp::ota_quiet_mode::enterQuiet;
using lamp::ota_quiet_mode::exitQuiet;
using lamp::ota_quiet_mode::holders;
using lamp::ota_quiet_mode::isQuiet;
using lamp::ota_quiet_mode::visibleOtaActive;

void setUp(void) {}
void tearDown(void) {}

// Firmware OTA (visible) fully wraps: visible while active, clean exit.
void test_fw_session_reports_visible(void) {
  enterQuiet(/*tearDownRadio=*/true, /*visible=*/true);
  TEST_ASSERT_TRUE(isQuiet());
  TEST_ASSERT_TRUE(visibleOtaActive());
  exitQuiet();
  TEST_ASSERT_FALSE(isQuiet());
}

// FS OTA (silent) fully wraps: silent while active, clean exit.
void test_fs_session_reports_silent(void) {
  enterQuiet(/*tearDownRadio=*/true, /*visible=*/false);
  TEST_ASSERT_TRUE(isQuiet());
  TEST_ASSERT_FALSE(visibleOtaActive());
  exitQuiet();
  TEST_ASSERT_FALSE(isQuiet());
}

// tearDownRadio and visible are orthogonal: all four combinations report the
// visible bit they were entered with, regardless of tearDownRadio. FS always
// keeps tearDownRadio=true as insurance on the transfer, but that must not be
// what determines visibility.
void test_teardown_radio_does_not_affect_visible_bit(void) {
  enterQuiet(/*tearDownRadio=*/true, /*visible=*/false);
  TEST_ASSERT_FALSE(visibleOtaActive());
  exitQuiet();

  enterQuiet(/*tearDownRadio=*/false, /*visible=*/false);
  TEST_ASSERT_FALSE(visibleOtaActive());
  exitQuiet();

  enterQuiet(/*tearDownRadio=*/true, /*visible=*/true);
  TEST_ASSERT_TRUE(visibleOtaActive());
  exitQuiet();

  enterQuiet(/*tearDownRadio=*/false, /*visible=*/true);
  TEST_ASSERT_TRUE(visibleOtaActive());
  exitQuiet();
}

// A firmware session followed by an FS session must not leak the prior
// visible state; the new 0->1 entry must overwrite it.
void test_fw_then_fs_no_stale_visible(void) {
  enterQuiet(/*tearDownRadio=*/true, /*visible=*/true);
  TEST_ASSERT_TRUE(visibleOtaActive());
  exitQuiet();

  enterQuiet(/*tearDownRadio=*/true, /*visible=*/false);
  TEST_ASSERT_FALSE(visibleOtaActive());
  exitQuiet();
}

// Symmetric: an FS session followed by a firmware session must not leak the
// prior silent state.
void test_fs_then_fw_no_stale_silent(void) {
  enterQuiet(/*tearDownRadio=*/true, /*visible=*/false);
  TEST_ASSERT_FALSE(visibleOtaActive());
  exitQuiet();

  enterQuiet(/*tearDownRadio=*/true, /*visible=*/true);
  TEST_ASSERT_TRUE(visibleOtaActive());
  exitQuiet();
}

void test_holders_counts_unmatched_entries(void) {
  TEST_ASSERT_EQUAL_INT(0, holders());
  enterQuiet(/*tearDownRadio=*/true, /*visible=*/true);
  enterQuiet(/*tearDownRadio=*/true, /*visible=*/false);
  TEST_ASSERT_EQUAL_INT(2, holders());
  exitQuiet();
  TEST_ASSERT_EQUAL_INT(1, holders());
  exitQuiet();
  TEST_ASSERT_EQUAL_INT(0, holders());
}

// Refcount >1 of the SAME type (e.g. receiver + distributor both FS): the
// second entrant is a no-op beyond the count, so visibility must stay
// latched at the first entrant's value until the last matching exit.
void test_second_entrant_same_type_keeps_visible_until_last_exit(void) {
  enterQuiet(/*tearDownRadio=*/true, /*visible=*/false);
  enterQuiet(/*tearDownRadio=*/true, /*visible=*/false);
  TEST_ASSERT_TRUE(isQuiet());
  TEST_ASSERT_FALSE(visibleOtaActive());

  exitQuiet();  // First exit: still one session active.
  TEST_ASSERT_TRUE(isQuiet());
  TEST_ASSERT_FALSE(visibleOtaActive());

  exitQuiet();  // Last exit: fully out of quiet mode.
  TEST_ASSERT_FALSE(isQuiet());
}

// A receiver's last exit stalls inside its radio resume while a distributor's
// first entry arrives on another task. The entry's pause must land after the
// resume, so the radio stays paused for the distributor's send.
static std::atomic<bool>       s_fakeRadioPaused{false};
static std::mutex              s_raceMu;
static std::condition_variable s_raceCv;
static bool                    s_stallResume = false;
static bool                    s_resumeStalled = false;
static bool                    s_entryDone = false;

static void fakePause() { s_fakeRadioPaused = true; }
static void fakeResume() {
  {
    std::unique_lock<std::mutex> lk(s_raceMu);
    if (s_stallResume) {
      s_resumeStalled = true;
      s_raceCv.notify_all();
      s_raceCv.wait_for(lk, std::chrono::milliseconds(100), [] { return s_entryDone; });
    }
  }
  s_fakeRadioPaused = false;
}

void test_last_exit_racing_a_first_entry_leaves_the_radio_paused(void) {
  lamp::ota_quiet_mode::s_radio = {fakePause, fakeResume};
  enterQuiet(/*tearDownRadio=*/true, /*visible=*/true);
  TEST_ASSERT_TRUE(s_fakeRadioPaused);
  s_stallResume = true;

  std::thread exiter([] { exitQuiet(); });
  bool reached = false;
  {
    std::unique_lock<std::mutex> lk(s_raceMu);
    reached = s_raceCv.wait_for(lk, std::chrono::seconds(1), [] { return s_resumeStalled; });
  }
  std::thread entrant([] {
    enterQuiet(/*tearDownRadio=*/true, /*visible=*/true);
    std::lock_guard<std::mutex> lk(s_raceMu);
    s_entryDone = true;
    s_raceCv.notify_all();
  });
  exiter.join();
  entrant.join();

  TEST_ASSERT_TRUE_MESSAGE(reached, "the exit never reached the radio resume");
  TEST_ASSERT_EQUAL_INT(1, holders());
  TEST_ASSERT_TRUE(s_fakeRadioPaused);

  s_stallResume = false;
  exitQuiet();
  TEST_ASSERT_FALSE(s_fakeRadioPaused);
  lamp::ota_quiet_mode::s_radio = {[] {}, [] {}};
}

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_fw_session_reports_visible);
  RUN_TEST(test_holders_counts_unmatched_entries);
  RUN_TEST(test_fs_session_reports_silent);
  RUN_TEST(test_teardown_radio_does_not_affect_visible_bit);
  RUN_TEST(test_fw_then_fs_no_stale_visible);
  RUN_TEST(test_fs_then_fw_no_stale_silent);
  RUN_TEST(test_second_entrant_same_type_keeps_visible_until_last_exit);
  RUN_TEST(test_last_exit_racing_a_first_entry_leaves_the_radio_paused);

  return UNITY_END();
}
