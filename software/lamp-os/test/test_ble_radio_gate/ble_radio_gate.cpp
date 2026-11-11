// BleRadioGate: while the OTA radio pause holds, no path restarts advertising
// or the central scan; outside it every disconnect brings advertising back;
// resume restarts each exactly once and still honours the GATT-client hold.

#include <unity.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "components/network/ble/ble_radio_gate.cpp"

using ble_control::BleRadio;
using ble_control::BleRadioGate;

// Models NimBLE: a start while already running is the "already active" no-op.
class FakeRadio : public BleRadio {
 public:
  bool advertising = false;
  bool scanning    = false;
  int  advStarts   = 0;
  int  scanStarts  = 0;
  int  redundantAdvStarts  = 0;
  int  redundantScanStarts = 0;
  std::function<void()> onScanStart;

  void startAdvertising() override {
    ++advStarts;
    if (advertising) ++redundantAdvStarts;
    advertising = true;
  }
  void stopAdvertising() override { advertising = false; }
  void startScan() override {
    ++scanStarts;
    if (scanning) ++redundantScanStarts;
    scanning = true;
    if (onScanStart) onScanStart();
  }
  void stopScan() override { scanning = false; }

  void clearCounts() { advStarts = scanStarts = redundantAdvStarts = redundantScanStarts = 0; }
};

// Boot state: advertising and the continuous scan both running.
static void boot(FakeRadio& r) {
  r.advertising = true;
  r.scanning    = true;
}

// A phone connects (the controller stops connectable advertising), then the
// OTA pause begins and kicks it.
static void pauseWithClientKicked(BleRadioGate& g, FakeRadio& r) {
  boot(r);
  g.clientConnected();
  r.advertising = false;
  TEST_ASSERT_TRUE(g.pauseForOta());
  g.clientDisconnected();
}

void setUp(void) {}
void tearDown(void) {}

void test_pause_stops_advertising_and_scan() {
  FakeRadio r;
  BleRadioGate g(r);
  boot(r);
  TEST_ASSERT_TRUE(g.pauseForOta());
  TEST_ASSERT_TRUE(g.otaPaused());
  TEST_ASSERT_FALSE(r.advertising);
  TEST_ASSERT_FALSE(r.scanning);
}

void test_disconnect_during_pause_restarts_nothing() {
  FakeRadio r;
  BleRadioGate g(r);
  pauseWithClientKicked(g, r);
  TEST_ASSERT_FALSE(r.advertising);
  TEST_ASSERT_FALSE(r.scanning);
  TEST_ASSERT_EQUAL_INT(0, r.advStarts);
  TEST_ASSERT_EQUAL_INT(0, r.scanStarts);
}

void test_scan_end_during_pause_restarts_nothing() {
  FakeRadio r;
  BleRadioGate g(r);
  boot(r);
  g.pauseForOta();
  g.scanEnded();
  TEST_ASSERT_FALSE(r.scanning);
  TEST_ASSERT_EQUAL_INT(0, r.scanStarts);
}

void test_social_window_during_pause_restarts_nothing() {
  FakeRadio r;
  BleRadioGate g(r);
  boot(r);
  g.clientConnected();
  g.pauseForOta();
  g.openScanWindow();
  TEST_ASSERT_FALSE(r.scanning);
  TEST_ASSERT_EQUAL_INT(0, r.scanStarts);
}

void test_advertising_restart_during_pause_restarts_nothing() {
  FakeRadio r;
  BleRadioGate g(r);
  boot(r);
  g.pauseForOta();
  g.restartAdvertising();
  TEST_ASSERT_FALSE(r.advertising);
  TEST_ASSERT_EQUAL_INT(0, r.advStarts);
}

void test_every_disconnect_outside_pause_resumes_advertising() {
  FakeRadio r;
  BleRadioGate g(r);
  boot(r);
  for (int i = 0; i < 3; ++i) {
    g.clientConnected();
    r.advertising = false;
    TEST_ASSERT_FALSE(r.scanning);
    g.clientDisconnected();
    TEST_ASSERT_TRUE(r.advertising);
    TEST_ASSERT_TRUE(r.scanning);
  }
}

void test_disconnect_after_a_paused_session_resumes_advertising() {
  FakeRadio r;
  BleRadioGate g(r);
  pauseWithClientKicked(g, r);
  g.resumeAfterOta();
  g.clientConnected();
  r.advertising = false;
  g.clientDisconnected();
  TEST_ASSERT_TRUE(r.advertising);
  TEST_ASSERT_TRUE(r.scanning);
}

void test_resume_starts_each_exactly_once() {
  FakeRadio r;
  BleRadioGate g(r);
  pauseWithClientKicked(g, r);
  TEST_ASSERT_TRUE(g.resumeAfterOta());
  TEST_ASSERT_FALSE(g.otaPaused());
  TEST_ASSERT_EQUAL_INT(1, r.advStarts);
  TEST_ASSERT_EQUAL_INT(1, r.scanStarts);
  TEST_ASSERT_EQUAL_INT(0, r.redundantAdvStarts);
  TEST_ASSERT_EQUAL_INT(0, r.redundantScanStarts);
  TEST_ASSERT_TRUE(r.advertising);
  TEST_ASSERT_TRUE(r.scanning);
}

void test_resume_honours_the_gatt_client_scan_hold() {
  FakeRadio r;
  BleRadioGate g(r);
  boot(r);
  g.pauseForOta();
  g.clientConnected();
  TEST_ASSERT_TRUE(g.resumeAfterOta());
  TEST_ASSERT_FALSE(r.scanning);
  TEST_ASSERT_EQUAL_INT(0, r.scanStarts);
  TEST_ASSERT_EQUAL_INT(1, r.advStarts);
  g.scanEnded();
  TEST_ASSERT_FALSE(r.scanning);
}

void test_repeat_pause_and_resume_touch_nothing() {
  FakeRadio r;
  BleRadioGate g(r);
  boot(r);
  g.pauseForOta();
  r.clearCounts();
  TEST_ASSERT_FALSE(g.pauseForOta());
  TEST_ASSERT_TRUE(g.resumeAfterOta());
  r.clearCounts();
  TEST_ASSERT_FALSE(g.resumeAfterOta());
  TEST_ASSERT_EQUAL_INT(0, r.advStarts);
  TEST_ASSERT_EQUAL_INT(0, r.scanStarts);
}

void test_scan_end_restarts_only_without_a_hold() {
  FakeRadio r;
  BleRadioGate g(r);
  g.scanEnded();
  TEST_ASSERT_TRUE(r.scanning);
  g.clientConnected();
  g.scanEnded();
  TEST_ASSERT_FALSE(r.scanning);
}

void test_social_window_closes_only_under_a_hold() {
  FakeRadio r;
  BleRadioGate g(r);
  boot(r);
  g.closeScanWindow();
  TEST_ASSERT_TRUE(r.scanning);
  g.clientConnected();
  g.openScanWindow();
  TEST_ASSERT_TRUE(r.scanning);
  g.closeScanWindow();
  TEST_ASSERT_FALSE(r.scanning);
}

// A pause taken on another task while a disconnect is mid-restart must still
// hold once both finish. The disconnect stalls inside its scan start; a pause
// that slips into that stall would leave advertising running under it.
void test_pause_racing_a_disconnect_still_holds() {
  FakeRadio r;
  BleRadioGate g(r);
  boot(r);
  g.clientConnected();
  r.advertising = false;

  std::mutex m;
  std::condition_variable cv;
  bool stalled = false;
  bool pauseReturned = false;
  r.onScanStart = [&] {
    std::unique_lock<std::mutex> lk(m);
    stalled = true;
    cv.notify_all();
    cv.wait_for(lk, std::chrono::milliseconds(100), [&] { return pauseReturned; });
  };

  std::thread host([&] { g.clientDisconnected(); });
  {
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, [&] { return stalled; });
  }
  g.pauseForOta();
  {
    std::lock_guard<std::mutex> lk(m);
    pauseReturned = true;
  }
  cv.notify_all();
  host.join();

  TEST_ASSERT_TRUE(g.otaPaused());
  TEST_ASSERT_FALSE(r.advertising);
  TEST_ASSERT_FALSE(r.scanning);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_pause_stops_advertising_and_scan);
  RUN_TEST(test_disconnect_during_pause_restarts_nothing);
  RUN_TEST(test_scan_end_during_pause_restarts_nothing);
  RUN_TEST(test_social_window_during_pause_restarts_nothing);
  RUN_TEST(test_advertising_restart_during_pause_restarts_nothing);
  RUN_TEST(test_every_disconnect_outside_pause_resumes_advertising);
  RUN_TEST(test_disconnect_after_a_paused_session_resumes_advertising);
  RUN_TEST(test_resume_starts_each_exactly_once);
  RUN_TEST(test_resume_honours_the_gatt_client_scan_hold);
  RUN_TEST(test_repeat_pause_and_resume_touch_nothing);
  RUN_TEST(test_scan_end_restarts_only_without_a_hold);
  RUN_TEST(test_social_window_closes_only_under_a_hold);
  RUN_TEST(test_pause_racing_a_disconnect_still_holds);
  return UNITY_END();
}
