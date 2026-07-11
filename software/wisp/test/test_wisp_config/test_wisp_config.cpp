// Native test seam: include the .cpp directly (it is excluded from the native
// build filter). Arduino.h / Preferences.h stubs live alongside this file.
#include "config/wisp_config.cpp"  // NOLINT(build/include)

#include <unity.h>

// kv_reset() is defined inside Preferences.h's anonymous namespace.
// Call it in setUp() to isolate each test.
void setUp()    { kv_reset(); }
void tearDown() {}

// ---- Tests ------------------------------------------------------------------

void test_name_default_is_empty() {
  wisp::WispConfig cfg;
  cfg.begin();
  TEST_ASSERT_EQUAL_STRING("", cfg.name().c_str());
}

void test_setname_persists_and_getter_returns_it() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setName("living-room");
  TEST_ASSERT_EQUAL_STRING("living-room", cfg.name().c_str());
}

void test_setname_clamps_to_20_chars() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setName("123456789012345678901234");  // 24 chars
  TEST_ASSERT_EQUAL_INT(20, (int)cfg.name().length());
  TEST_ASSERT_EQUAL_STRING("12345678901234567890", cfg.name().c_str());
}

void test_setname_20_chars_exact_not_clamped() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setName("12345678901234567890");  // exactly 20
  TEST_ASSERT_EQUAL_INT(20, (int)cfg.name().length());
}

void test_setname_empty_allowed() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setName("something");
  cfg.setName("");
  TEST_ASSERT_EQUAL_STRING("", cfg.name().c_str());
}

void test_password_default_is_empty() {
  wisp::WispConfig cfg;
  cfg.begin();
  TEST_ASSERT_EQUAL_STRING("", cfg.password().c_str());
}

void test_setpassword_persists_and_getter_returns_it() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setPassword("secret");
  TEST_ASSERT_EQUAL_STRING("secret", cfg.password().c_str());
}

void test_setpassword_empty_clears() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setPassword("secret");
  cfg.setPassword("");
  TEST_ASSERT_EQUAL_STRING("", cfg.password().c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_name_default_is_empty);
  RUN_TEST(test_setname_persists_and_getter_returns_it);
  RUN_TEST(test_setname_clamps_to_20_chars);
  RUN_TEST(test_setname_20_chars_exact_not_clamped);
  RUN_TEST(test_setname_empty_allowed);
  RUN_TEST(test_password_default_is_empty);
  RUN_TEST(test_setpassword_persists_and_getter_returns_it);
  RUN_TEST(test_setpassword_empty_clears);
  return UNITY_END();
}
