// Native tests for SeqSource, the one seq every wisp frame draws from: each
// boot starts at a random value, never at a fixed one.

#include <unity.h>

#include <cstdint>

#include "status/seq_source.hpp"

void setUp(void) { lamp_protocol::nativeSeqEntropy() = 0; }
void tearDown(void) {}

void test_boot_starts_at_drawn_value() {
  lamp_protocol::nativeSeqEntropy() = 0x0007B3E1u;
  wisp::SeqSource seq;
  TEST_ASSERT_EQUAL_UINT16(0xB3E1, seq.next());
  TEST_ASSERT_EQUAL_UINT16(0xB3E2, seq.next());
}

void test_each_boot_starts_where_its_draw_says() {
  lamp_protocol::nativeSeqEntropy() = 0x1200;
  wisp::SeqSource first;
  TEST_ASSERT_EQUAL_UINT16(0x1200, first.next());
  lamp_protocol::nativeSeqEntropy() = 0x9A51;
  wisp::SeqSource second;
  TEST_ASSERT_EQUAL_UINT16(0x9A51, second.next());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_starts_at_drawn_value);
  RUN_TEST(test_each_boot_starts_where_its_draw_says);
  return UNITY_END();
}
