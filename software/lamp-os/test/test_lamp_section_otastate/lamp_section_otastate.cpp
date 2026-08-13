#include <unity.h>

#include <ArduinoJson.h>

#include <cstring>

#include "util/bd_addr.hpp"

// Keep in sync with the otaState/otaSendingTo emit in config.cpp::asLampJson.
std::string emitOtaStateSnippet(uint8_t otaState, const uint8_t* targetMac = nullptr) {
  JsonDocument doc;
  doc.to<JsonObject>();
  if (otaState != 0) doc["otaState"] = otaState;
  if (targetMac) {
    char macBuf[18];
    lamp::formatBdAddr(targetMac, macBuf);
    doc["otaSendingTo"] = macBuf;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

void setUp(void) {}
void tearDown(void) {}

void test_emits_otastate_when_nonzero() {
  std::string json = emitOtaStateSnippet(2);
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"otaState\":2"));
}

void test_idle_otastate_omitted() {
  TEST_ASSERT_EQUAL_STRING("{}", emitOtaStateSnippet(0).c_str());
}

void test_emits_otasendingto_when_sending() {
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  std::string json = emitOtaStateSnippet(1, mac);
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"otaSendingTo\":\"AA:BB:CC:DD:EE:FF\""));
}

void test_otasendingto_omitted_without_target() {
  std::string json = emitOtaStateSnippet(1);
  TEST_ASSERT_NULL(strstr(json.c_str(), "otaSendingTo"));
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_emits_otastate_when_nonzero);
  RUN_TEST(test_idle_otastate_omitted);
  RUN_TEST(test_emits_otasendingto_when_sending);
  RUN_TEST(test_otasendingto_omitted_without_target);
  return UNITY_END();
}
