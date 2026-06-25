// test/test_lamp_type_emit/lamp_type_emit.cpp
//
// Mirror-pattern test: verifies the lampType emit logic in asLampJson
// without linking real Config (which pulls Arduino + Preferences deps).
// Same approach as test_section_emit_colors_editable.

#include <unity.h>
#include <string>

// Mirror of Config::asLampJson's lampType emit line. Keep in sync with
// the production emit at config.cpp::asLampJson.
std::string emitLampTypeSnippet(const std::string& lampType) {
  std::string j = "{\"lampType\":\"";
  j += lampType;
  j += "\"}";
  return j;
}

void setUp(void) {}
void tearDown(void) {}

void test_emit_empty_string() {
  TEST_ASSERT_EQUAL_STRING("{\"lampType\":\"\"}", emitLampTypeSnippet("").c_str());
}

void test_emit_standard() {
  TEST_ASSERT_EQUAL_STRING("{\"lampType\":\"standard\"}",
                           emitLampTypeSnippet("standard").c_str());
}

void test_emit_snafu() {
  TEST_ASSERT_EQUAL_STRING("{\"lampType\":\"snafu\"}",
                           emitLampTypeSnippet("snafu").c_str());
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_emit_empty_string);
  RUN_TEST(test_emit_standard);
  RUN_TEST(test_emit_snafu);
  return UNITY_END();
}
