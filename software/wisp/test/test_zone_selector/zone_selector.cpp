#include <unity.h>
#include "config/zone_selector.hpp"

void setUp() {}
void tearDown() {}

void test_dedup_and_fifo_cap() {
  wisp::ZoneSelector z;
  for (int i = 0; i < 20; ++i) z.observe(i);   // overflow the 16-cap
  int buf[16];
  size_t n = z.copyObserved(buf, 16);
  TEST_ASSERT_EQUAL_UINT(16, n);
  TEST_ASSERT_EQUAL_INT(4,  buf[0]);            // oldest 0..3 evicted
  TEST_ASSERT_EQUAL_INT(19, buf[15]);

  z.observe(10);                                // already present → no-op
  n = z.copyObserved(buf, 16);
  TEST_ASSERT_EQUAL_UINT(16, n);                // no growth, no reorder
  TEST_ASSERT_EQUAL_INT(19, buf[15]);
}

void test_copyObserved_respects_outCap() {
  wisp::ZoneSelector z;
  for (int i = 0; i < 8; ++i) z.observe(i);
  int buf[4];
  size_t n = z.copyObserved(buf, 4);
  TEST_ASSERT_EQUAL_UINT(4, n);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_dedup_and_fifo_cap);
  RUN_TEST(test_copyObserved_respects_outCap);
  return UNITY_END();
}
