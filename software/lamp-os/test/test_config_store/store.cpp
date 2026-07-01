// Native-host test for the ConfigStore seam via its in-memory fake. Proves
// the contract Config relies on: default-on-absent reads, write round-trips,
// overwrite, and clear() wiping the namespace. The fake is the second
// implementation that makes ConfigStore a test seam, not speculation.

#include <unity.h>

#include "config/config_store.hpp"

void setUp(void) {}
void tearDown(void) {}

void test_read_returns_default_when_absent() {
  lamp::InMemoryConfigStore s;
  TEST_ASSERT_EQUAL_STRING("{}", s.read("cfg", "{}").c_str());
}

void test_write_then_read_round_trips() {
  lamp::InMemoryConfigStore s;
  const size_t n = s.write("cfg", "{\"a\":1}");
  TEST_ASSERT_EQUAL_UINT(7, n);
  TEST_ASSERT_EQUAL_STRING("{\"a\":1}", s.read("cfg", "{}").c_str());
}

void test_write_overwrites_existing_key() {
  lamp::InMemoryConfigStore s;
  s.write("k", "first");
  s.write("k", "second");
  TEST_ASSERT_EQUAL_STRING("second", s.read("k", "").c_str());
}

void test_clear_wipes_every_key() {
  lamp::InMemoryConfigStore s;
  s.write("cfg", "x");
  s.write("dispositions", "y");
  TEST_ASSERT_TRUE(s.clear());
  TEST_ASSERT_EQUAL_STRING("{}", s.read("cfg", "{}").c_str());
  TEST_ASSERT_EQUAL_STRING("def", s.read("dispositions", "def").c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_read_returns_default_when_absent);
  RUN_TEST(test_write_then_read_round_trips);
  RUN_TEST(test_write_overwrites_existing_key);
  RUN_TEST(test_clear_wipes_every_key);
  return UNITY_END();
}
