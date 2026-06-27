#include <unity.h>
#include <ArduinoJson.h>
#include "status_json.hpp"

constexpr size_t CAP = 230;

void setUp() {}
void tearDown() {}

void test_worst_case_stays_under_cap() {
  int zones[16];
  for (int i = 0; i < 16; ++i) zones[i] = 2147483647;   // pathological width
  wisp::WispStatusFields f{ 2147483647, "firstSeen", zones, 16,
                            true, true, "abcdef12", 4294967295u, "aurora",
                            255, 255, 255, true };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(n <= CAP);                            // the guarantee
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));  // bool is true on error
  TEST_ASSERT_TRUE(d["observedZones"].as<JsonArray>().size() < 16); // cap engaged
  TEST_ASSERT_FALSE(d["source"].isNull());              // field preserved
  TEST_ASSERT_FALSE(d["offColor"].isNull());            // field preserved
}

void test_empty_observed_fits() {
  wisp::WispStatusFields f{ 3, "appOp", nullptr, 0,
                            false, false, "abcdef12", 1000u, "off",
                            10, 20, 30, true };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_worst_case_stays_under_cap);
  RUN_TEST(test_empty_observed_fits);
  return UNITY_END();
}
