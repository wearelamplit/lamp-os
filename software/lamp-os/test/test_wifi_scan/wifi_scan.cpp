// The real wifi scan state machine over a host WiFi stand-in: a background
// (home-presence) scan keeps only which SSIDs it saw, an app-requested scan
// publishes its list until the app disconnects, and homeSsidVisible answers
// from the latest scan of either kind until it goes stale.

#include <unity.h>

#include <string>

#include "../../src/components/network/transport/ssid_presence.cpp"
#include "../../src/components/network/transport/wifi.cpp"
#include "../replaced_new/heap_tally.hpp"

namespace ble_control {
static bool g_clientConnected = false;
bool isClientConnected() { return g_clientConnected; }
}  // namespace ble_control

using heap_tally::g_liveBytes;

static constexpr uint32_t kBoot = 1000;
static constexpr uint32_t kFirstScanAt = kBoot + 60 * 1000 + 1;

void setUp() {
  set_mock_millis(kBoot);
  ble_control::g_clientConnected = false;
  WiFi = MockWiFi{};
  // Strongest first; the second SSID is past the 15-char small-string buffer,
  // so a kept copy of it would hold heap.
  WiFi.aps = {{"home", -40, WIFI_AUTH_WPA2_PSK},
              {"the-neighbours-network-5g", -70, WIFI_AUTH_WPA2_PSK},
              {"cafe", -85, WIFI_AUTH_OPEN}};
  wifi::dropScanResults();
  wifi::s_presence = wifi::SsidPresence{};
  wifi::s_lastScanCompleteMs = 0;
  wifi::s_state = wifi::IDLE;
  wifi::setHomeModeEnabledGetter([] { return true; });
  wifi::begin();
}
void tearDown() {}

static void completeScan() {
  WiFi.scanResult = static_cast<int16_t>(WiFi.aps.size());
  wifi::tick();
}

static void runBackgroundScan() {
  set_mock_millis(kFirstScanAt);
  wifi::tick();
  TEST_ASSERT_EQUAL(wifi::SCANNING, wifi::state());
  completeScan();
  TEST_ASSERT_EQUAL(wifi::IDLE, wifi::state());
}

void test_background_scan_holds_no_list() {
  set_mock_millis(kFirstScanAt);
  wifi::tick();
  TEST_ASSERT_EQUAL(1, WiFi.scansStarted);
  const size_t before = g_liveBytes;
  completeScan();
  TEST_ASSERT_EQUAL(wifi::IDLE, wifi::state());
  TEST_ASSERT_EQUAL_size_t(before, g_liveBytes);
  TEST_ASSERT_TRUE(wifi::peekScanResults().empty());
}

void test_background_scan_sees_each_ssid() {
  runBackgroundScan();
  TEST_ASSERT_TRUE(wifi::homeSsidVisible("home"));
  TEST_ASSERT_TRUE(wifi::homeSsidVisible("the-neighbours-network-5g"));
  TEST_ASSERT_TRUE(wifi::homeSsidVisible("cafe"));
  TEST_ASSERT_FALSE(wifi::homeSsidVisible("hom"));
  TEST_ASSERT_FALSE(wifi::homeSsidVisible("elsewhere"));
  TEST_ASSERT_FALSE(wifi::homeSsidVisible(""));
}

void test_no_scan_sees_nothing() {
  TEST_ASSERT_FALSE(wifi::homeSsidVisible("home"));
}

void test_presence_goes_stale_after_90s() {
  runBackgroundScan();
  set_mock_millis(kFirstScanAt + 90 * 1000);
  TEST_ASSERT_TRUE(wifi::homeSsidVisible("home"));
  set_mock_millis(kFirstScanAt + 90 * 1000 + 1);
  TEST_ASSERT_FALSE(wifi::homeSsidVisible("home"));
}

void test_presence_holds_while_the_next_scan_runs() {
  runBackgroundScan();
  set_mock_millis(kFirstScanAt + 60 * 1000 + 1);
  wifi::tick();
  TEST_ASSERT_EQUAL(wifi::SCANNING, wifi::state());
  TEST_ASSERT_TRUE(wifi::homeSsidVisible("home"));
}

void test_a_later_scan_replaces_presence() {
  runBackgroundScan();
  WiFi.aps = {{"cafe", -50, WIFI_AUTH_OPEN}};
  set_mock_millis(kFirstScanAt + 60 * 1000 + 1);
  wifi::tick();
  completeScan();
  TEST_ASSERT_FALSE(wifi::homeSsidVisible("home"));
  TEST_ASSERT_TRUE(wifi::homeSsidVisible("cafe"));
}

void test_app_scan_publishes_its_list_until_the_app_leaves() {
  ble_control::g_clientConnected = true;
  const size_t before = g_liveBytes;
  wifi::startScan();
  completeScan();
  wifi::tick();
  {
    const auto results = wifi::peekScanResults();
    TEST_ASSERT_EQUAL_size_t(3, results.size());
    TEST_ASSERT_EQUAL_STRING("the-neighbours-network-5g", results[1].ssid.c_str());
    TEST_ASSERT_EQUAL_INT8(-70, results[1].rssi);
    TEST_ASSERT_TRUE(results[1].encrypted);
    TEST_ASSERT_FALSE(results[2].encrypted);
  }
  TEST_ASSERT_TRUE(wifi::homeSsidVisible("home"));

  ble_control::g_clientConnected = false;
  wifi::tick();
  TEST_ASSERT_TRUE(wifi::peekScanResults().empty());
  TEST_ASSERT_EQUAL_size_t(before, g_liveBytes);
  TEST_ASSERT_TRUE(wifi::homeSsidVisible("home"));
}

static std::string ssidNo(int i) { return "net-" + std::to_string(i); }

void test_presence_keeps_the_strongest_up_to_its_cap() {
  wifi::SsidPresence p;
  for (int i = 0; i <= static_cast<int>(wifi::SsidPresence::kCapacity); ++i) p.add(ssidNo(i));
  for (int i = 0; i < static_cast<int>(wifi::SsidPresence::kCapacity); ++i) {
    TEST_ASSERT_TRUE(p.contains(ssidNo(i)));
  }
  TEST_ASSERT_FALSE(p.contains(ssidNo(wifi::SsidPresence::kCapacity)));
}

// A full table flags only a new SSID it had to drop; a repeat of a kept one is
// no overflow.
void test_presence_reports_its_overflow() {
  wifi::SsidPresence p;
  const int cap = static_cast<int>(wifi::SsidPresence::kCapacity);
  for (int i = 0; i < cap; ++i) p.add(ssidNo(i));
  TEST_ASSERT_EQUAL_size_t(wifi::SsidPresence::kCapacity, p.size());
  p.add(ssidNo(cap - 1));
  p.add(ssidNo(0));
  TEST_ASSERT_FALSE(p.overflowed());
  p.add(ssidNo(cap));
  TEST_ASSERT_TRUE(p.overflowed());
  TEST_ASSERT_EQUAL_size_t(wifi::SsidPresence::kCapacity, p.size());
}

// Several access points share one SSID; a repeat takes no slot.
void test_presence_repeats_take_no_slot() {
  wifi::SsidPresence p;
  for (int k = 0; k < 10; ++k) p.add("mesh");
  for (int i = 1; i < static_cast<int>(wifi::SsidPresence::kCapacity); ++i) p.add(ssidNo(i));
  TEST_ASSERT_TRUE(p.contains("mesh"));
  TEST_ASSERT_TRUE(p.contains(ssidNo(wifi::SsidPresence::kCapacity - 1)));
}

void test_presence_never_holds_the_empty_ssid() {
  wifi::SsidPresence p;
  p.add("");
  TEST_ASSERT_FALSE(p.contains(""));
  for (int i = 0; i < static_cast<int>(wifi::SsidPresence::kCapacity); ++i) p.add(ssidNo(i));
  TEST_ASSERT_TRUE(p.contains(ssidNo(wifi::SsidPresence::kCapacity - 1)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_background_scan_holds_no_list);
  RUN_TEST(test_background_scan_sees_each_ssid);
  RUN_TEST(test_no_scan_sees_nothing);
  RUN_TEST(test_presence_goes_stale_after_90s);
  RUN_TEST(test_presence_holds_while_the_next_scan_runs);
  RUN_TEST(test_a_later_scan_replaces_presence);
  RUN_TEST(test_app_scan_publishes_its_list_until_the_app_leaves);
  RUN_TEST(test_presence_keeps_the_strongest_up_to_its_cap);
  RUN_TEST(test_presence_reports_its_overflow);
  RUN_TEST(test_presence_repeats_take_no_slot);
  RUN_TEST(test_presence_never_holds_the_empty_ssid);
  return UNITY_END();
}
