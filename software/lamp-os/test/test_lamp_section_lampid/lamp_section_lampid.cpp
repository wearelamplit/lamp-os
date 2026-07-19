#include <unity.h>

#include <ArduinoJson.h>

#include <cstring>
#include <string>

// Keep in sync with the lampId emit in config.cpp::asLampJson.
std::string emitLampIdSnippet(const std::string& lampId) {
  JsonDocument doc;
  doc.to<JsonObject>();
  if (!lampId.empty()) {
    doc["lampId"] = lampId;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

void setUp(void) {}
void tearDown(void) {}

void test_emits_lampid_no_bdaddr() {
  std::string json = emitLampIdSnippet("C4:DD:57:EB:64:60");
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"lampId\":\"C4:DD:57:EB:64:60\""));
  TEST_ASSERT_NULL(strstr(json.c_str(), "\"bdAddr\""));
}

void test_empty_lampid_omitted() {
  TEST_ASSERT_EQUAL_STRING("{}", emitLampIdSnippet("").c_str());
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_emits_lampid_no_bdaddr);
  RUN_TEST(test_empty_lampid_omitted);
  return UNITY_END();
}
