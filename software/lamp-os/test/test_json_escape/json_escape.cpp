// Native-host unit tests for the JSON string escaper used by the
// nearby-section emitter. Peer names are attacker-reachable (BLE adv +
// MSG_HELLO), so quote, backslash, and control bytes must never pass
// through unescaped.

#include <unity.h>

#include <string>

#include "util/json_escape.hpp"

void setUp(void) {}
void tearDown(void) {}

static std::string escaped(const char* s) {
  std::string out;
  lamp::appendJsonEscaped(out, s);
  return out;
}

void test_plain_passthrough() {
  TEST_ASSERT_EQUAL_STRING("jacko", escaped("jacko").c_str());
  TEST_ASSERT_EQUAL_STRING("", escaped("").c_str());
}

void test_quote_escaped() {
  TEST_ASSERT_EQUAL_STRING("a\\\"b", escaped("a\"b").c_str());
}

void test_backslash_escaped() {
  TEST_ASSERT_EQUAL_STRING("a\\\\b", escaped("a\\b").c_str());
}

void test_control_chars_unicode_escaped() {
  TEST_ASSERT_EQUAL_STRING("a\\u000Ab", escaped("a\nb").c_str());
  TEST_ASSERT_EQUAL_STRING("\\u0001", escaped("\x01").c_str());
  TEST_ASSERT_EQUAL_STRING("\\u001F", escaped("\x1f").c_str());
}

void test_injection_attempt_neutralized() {
  // A name crafted to close the string and inject a field must come out
  // as inert escaped text.
  TEST_ASSERT_EQUAL_STRING("x\\\",\\\"otaState\\\":9,\\\"y\\\":\\\"",
                           escaped("x\",\"otaState\":9,\"y\":\"").c_str());
}

void test_high_bytes_passthrough() {
  // UTF-8 multibyte sequences (bytes >= 0x80) pass through untouched.
  TEST_ASSERT_EQUAL_STRING("l\xC3\xA4mp", escaped("l\xC3\xA4mp").c_str());
}

void test_appends_to_existing_content() {
  std::string out = "{\"name\":\"";
  lamp::appendJsonEscaped(out, "a\"b");
  TEST_ASSERT_EQUAL_STRING("{\"name\":\"a\\\"b", out.c_str());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_plain_passthrough);
  RUN_TEST(test_quote_escaped);
  RUN_TEST(test_backslash_escaped);
  RUN_TEST(test_control_chars_unicode_escaped);
  RUN_TEST(test_injection_attempt_neutralized);
  RUN_TEST(test_high_bytes_passthrough);
  RUN_TEST(test_appends_to_existing_content);

  return UNITY_END();
}
