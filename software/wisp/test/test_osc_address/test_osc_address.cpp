#include <unity.h>

#include "osc/osc_target.hpp"
#include "osc_address_cases.hpp"

void setUp() {}
void tearDown() {}

void test_every_case_matches_the_rule() {
  for (const OscAddressCase& c : kOscAddressCases) {
    TEST_ASSERT_EQUAL_MESSAGE(c.valid, wisp::oscCustomAddressValid(c.address),
                              c.address);
  }
}

void test_null_is_invalid() {
  TEST_ASSERT_FALSE(wisp::oscCustomAddressValid(nullptr));
}

// The longest valid case is the cap, so the cases pin it.
void test_cases_reach_the_cap() {
  size_t longest = 0;
  for (const OscAddressCase& c : kOscAddressCases) {
    size_t n = 0;
    while (c.address[n]) ++n;
    if (c.valid && n > longest) longest = n;
  }
  TEST_ASSERT_EQUAL_size_t(wisp::kOscCustomAddressMaxLen, longest);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_case_matches_the_rule);
  RUN_TEST(test_null_is_invalid);
  RUN_TEST(test_cases_reach_the_cap);
  return UNITY_END();
}
