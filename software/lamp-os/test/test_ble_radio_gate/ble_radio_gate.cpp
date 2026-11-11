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

// Counts a start while already running, NimBLE's "already active" no-op. Each
// hook fires inside the call before it takes effect, so a test can hold a call
// in flight.
class FakeRadio : public BleRadio {
 public:
  bool advertising = false;
  bool scanning    = false;
  int  advStarts   = 0;
  int  scanStarts  = 0;
  int  redundantAdvStarts  = 0;
  int  redundantScanStarts = 0;
  std::function<void()> onAdvStart;
  std::function<void()> onScanStart;
  std::function<void()> onIsAdvertising;

  void startAdvertising() override {
    if (onAdvStart) onAdvStart();
    ++advStarts;
    if (advertising) ++redundantAdvStarts;
    advertising = true;
  }
  void stopAdvertising() override { advertising = false; }
  void startScan() override {
    if (onScanStart) onScanStart();
    ++scanStarts;
    if (scanning) ++redundantScanStarts;
    scanning = true;
  }
  void stopScan() override { scanning = false; }
  bool isAdvertising() override {
    if (onIsAdvertising) onIsAdvertising();
    return advertising;
  }

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

// Runs `inFlight` on a second thread and holds it inside the radio call `hook`
// fires from, runs `racer` on this thread meanwhile, then lets it finish. The
// gate serializes its calls, so `racer` blocks until `inFlight` is done; were
// it to slip in, the final state shows the hold undone.
static void race(std::function<void()>& hook, const std::function<void()>& inFlight,
                 const std::function<void()>& racer) {
  std::mutex m;
  std::condition_variable cv;
  bool stalled = false;
  bool racerDone = false;
  hook = [&] {
    std::unique_lock<std::mutex> lk(m);
    if (stalled) return;
    stalled = true;
    cv.notify_all();
    cv.wait_for(lk, std::chrono::milliseconds(100), [&] { return racerDone; });
  };
  std::thread t(inFlight);
  {
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, [&] { return stalled; });
  }
  racer();
  {
    std::lock_guard<std::mutex> lk(m);
    racerDone = true;
  }
  cv.notify_all();
  t.join();
  hook = nullptr;
}

void test_pause_racing_a_disconnect_still_holds() {
  FakeRadio r;
  BleRadioGate g(r);
  boot(r);
  g.clientConnected();
  r.advertising = false;
  race(r.onScanStart, [&] { g.clientDisconnected(); }, [&] { g.pauseForOta(); });
  TEST_ASSERT_TRUE(g.otaPaused());
  TEST_ASSERT_FALSE(r.advertising);
  TEST_ASSERT_FALSE(r.scanning);
}

void test_pause_racing_a_scan_end_still_holds() {
  FakeRadio r;
  BleRadioGate g(r);
  race(r.onScanStart, [&] { g.scanEnded(); }, [&] { g.pauseForOta(); });
  TEST_ASSERT_TRUE(g.otaPaused());
  TEST_ASSERT_FALSE(r.scanning);
}

void test_pause_racing_a_social_window_still_holds() {
  FakeRadio r;
  BleRadioGate g(r);
  g.clientConnected();
  race(r.onScanStart, [&] { g.openScanWindow(); }, [&] { g.pauseForOta(); });
  TEST_ASSERT_TRUE(g.otaPaused());
  TEST_ASSERT_FALSE(r.scanning);
}

void test_pause_racing_an_advertising_restart_still_holds() {
  FakeRadio r;
  BleRadioGate g(r);
  boot(r);
  race(r.onAdvStart, [&] { g.restartAdvertising(); }, [&] { g.pauseForOta(); });
  TEST_ASSERT_TRUE(g.otaPaused());
  TEST_ASSERT_FALSE(r.advertising);
}

void test_connect_racing_a_resume_keeps_the_client_scan_hold() {
  FakeRadio r;
  BleRadioGate g(r);
  boot(r);
  g.pauseForOta();
  race(r.onScanStart, [&] { g.resumeAfterOta(); }, [&] { g.clientConnected(); });
  TEST_ASSERT_FALSE(r.scanning);
  g.scanEnded();
  TEST_ASSERT_FALSE(r.scanning);
}

void test_enforce_stops_advertising_nimble_restarts_under_the_pause() {
  FakeRadio r;
  BleRadioGate g(r);
  boot(r);
  g.pauseForOta();
  r.advertising = true;  // host reset re-sync restarts it inside NimBLE
  g.enforcePause();
  TEST_ASSERT_FALSE(r.advertising);
  TEST_ASSERT_TRUE(g.resumeAfterOta());
  TEST_ASSERT_EQUAL_INT(1, r.advStarts);
  TEST_ASSERT_EQUAL_INT(0, r.redundantAdvStarts);
}

void test_enforce_leaves_advertising_alone_outside_the_pause() {
  FakeRadio r;
  BleRadioGate g(r);
  boot(r);
  g.enforcePause();
  TEST_ASSERT_TRUE(r.advertising);
  g.pauseForOta();
  g.resumeAfterOta();
  g.enforcePause();
  TEST_ASSERT_TRUE(r.advertising);
}

void test_resume_racing_enforce_leaves_advertising_on() {
  FakeRadio r;
  BleRadioGate g(r);
  boot(r);
  g.pauseForOta();
  r.advertising = true;
  race(r.onIsAdvertising, [&] { g.enforcePause(); }, [&] { g.resumeAfterOta(); });
  TEST_ASSERT_FALSE(g.otaPaused());
  TEST_ASSERT_TRUE(r.advertising);
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
  RUN_TEST(test_pause_racing_a_scan_end_still_holds);
  RUN_TEST(test_pause_racing_a_social_window_still_holds);
  RUN_TEST(test_pause_racing_an_advertising_restart_still_holds);
  RUN_TEST(test_connect_racing_a_resume_keeps_the_client_scan_hold);
  RUN_TEST(test_enforce_stops_advertising_nimble_restarts_under_the_pause);
  RUN_TEST(test_enforce_leaves_advertising_alone_outside_the_pause);
  RUN_TEST(test_resume_racing_enforce_leaves_advertising_on);
  return UNITY_END();
}
