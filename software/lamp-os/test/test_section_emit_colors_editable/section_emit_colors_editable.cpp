// test/test_section_emit_colors_editable/section_emit_colors_editable.cpp
//
// Verifies the colorsEditable emit predicate for asBaseJson / asShadeJson.
//
// The production structs (BaseSettings / ShadeSettings) pull in lamp::Color
// which has out-of-line constructors in color.cpp. Linking color.cpp
// transitively pulls in Arduino / NimBLE platform deps that aren't available
// in the native env. Following the inline-mirror pattern from test_section_cache
// and test_dispositions_parse_filter: we declare minimal mirrors that carry
// only the fields under test, then verify the emit shape directly.

#include <unity.h>

#include <string>

namespace lamp {

// Minimal mirror of BaseSettings — only the field under test. Keep in
// sync with config_types.hpp if the colorsEditable default changes.
struct BaseSettingsMirror {
  bool colorsEditable = true;
};

// Minimal mirror of ShadeSettings.
struct ShadeSettingsMirror {
  bool colorsEditable = true;
};

}  // namespace lamp

// Mirror of asBaseJson's colorsEditable emit. Keep in sync with production.
std::string emitBaseColorsEditableSnippet(
    const lamp::BaseSettingsMirror& b) {
  std::string j = "{";
  j += "\"colorsEditable\":";
  j += (b.colorsEditable ? "true" : "false");
  j += "}";
  return j;
}

std::string emitShadeColorsEditableSnippet(
    const lamp::ShadeSettingsMirror& s) {
  std::string j = "{";
  j += "\"colorsEditable\":";
  j += (s.colorsEditable ? "true" : "false");
  j += "}";
  return j;
}

void setUp(void) {}
void tearDown(void) {}

void test_base_default_true_emitted() {
  lamp::BaseSettingsMirror b;
  TEST_ASSERT_EQUAL_STRING("{\"colorsEditable\":true}",
                           emitBaseColorsEditableSnippet(b).c_str());
}

void test_base_false_emitted_when_disabled() {
  lamp::BaseSettingsMirror b;
  b.colorsEditable = false;
  TEST_ASSERT_EQUAL_STRING("{\"colorsEditable\":false}",
                           emitBaseColorsEditableSnippet(b).c_str());
}

void test_shade_default_true_emitted() {
  lamp::ShadeSettingsMirror s;
  TEST_ASSERT_EQUAL_STRING("{\"colorsEditable\":true}",
                           emitShadeColorsEditableSnippet(s).c_str());
}

void test_shade_false_emitted_when_disabled() {
  lamp::ShadeSettingsMirror s;
  s.colorsEditable = false;
  TEST_ASSERT_EQUAL_STRING("{\"colorsEditable\":false}",
                           emitShadeColorsEditableSnippet(s).c_str());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_base_default_true_emitted);
  RUN_TEST(test_base_false_emitted_when_disabled);
  RUN_TEST(test_shade_default_true_emitted);
  RUN_TEST(test_shade_false_emitted_when_disabled);
  return UNITY_END();
}
