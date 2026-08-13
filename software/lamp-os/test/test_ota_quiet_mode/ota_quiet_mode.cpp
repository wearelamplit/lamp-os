// Native-host unit tests for ota_quiet_mode. Links the production .cpp
// directly (same pattern as test_ota_channel / test_firmware_signature):
// the ble_control coex calls are compiled out under neither ARDUINO nor
// ESP_PLATFORM, so the refcount + visible-bit logic is host-testable as-is.

#include <unity.h>

#include "components/firmware/ota_quiet_mode.hpp"
#include "components/firmware/ota_quiet_mode.cpp"

using lamp::ota_quiet_mode::enterQuiet;
using lamp::ota_quiet_mode::exitQuiet;
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

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_fw_session_reports_visible);
  RUN_TEST(test_fs_session_reports_silent);
  RUN_TEST(test_teardown_radio_does_not_affect_visible_bit);
  RUN_TEST(test_fw_then_fs_no_stale_visible);
  RUN_TEST(test_fs_then_fw_no_stale_silent);
  RUN_TEST(test_second_entrant_same_type_keeps_visible_until_last_exit);

  return UNITY_END();
}
