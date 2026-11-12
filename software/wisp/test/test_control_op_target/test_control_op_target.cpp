// Native tests for controlOpForWisp, the target filter ControlOpInbox admits
// ops through:
//   - an op addressed to this wisp applies.
//   - an op addressed to a sibling wisp is dropped.
//   - a broadcast op is dropped, whoever sent it: a peer wisp's status or
//     config, or this wisp's own relayed back.

#include <unity.h>

#include <cstdint>

#include "net/control_op_inbox.hpp"

namespace {

const uint8_t kSelf[6]      = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
const uint8_t kSibling[6]   = {0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0};
const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void test_addressed_to_self_applies(void) {
  TEST_ASSERT_TRUE(wisp::controlOpForWisp(kSelf, kSelf));
}

void test_addressed_to_sibling_drops(void) {
  TEST_ASSERT_FALSE(wisp::controlOpForWisp(kSibling, kSelf));
}

// A peer wisp's wispStatus / wispConfig rides a broadcast CONTROL_OP. Accepted,
// it would overwrite an app op waiting in the slot.
void test_broadcast_drops(void) {
  TEST_ASSERT_FALSE(wisp::controlOpForWisp(kBroadcast, kSelf));
}

// Target bytes differing from self in a single octet, first and last, still drop.
void test_near_miss_target_drops(void) {
  uint8_t first[6] = {0x10, 0x22, 0x33, 0x44, 0x55, 0x66};
  uint8_t last[6]  = {0x11, 0x22, 0x33, 0x44, 0x55, 0x67};
  TEST_ASSERT_FALSE(wisp::controlOpForWisp(first, kSelf));
  TEST_ASSERT_FALSE(wisp::controlOpForWisp(last, kSelf));
}

// Before setSelfMac (self all-zero) nothing a lamp or wisp sends applies.
void test_unset_self_mac_accepts_no_real_target(void) {
  const uint8_t zero[6] = {0, 0, 0, 0, 0, 0};
  TEST_ASSERT_FALSE(wisp::controlOpForWisp(kBroadcast, zero));
  TEST_ASSERT_FALSE(wisp::controlOpForWisp(kSelf, zero));
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_addressed_to_self_applies);
  RUN_TEST(test_addressed_to_sibling_drops);
  RUN_TEST(test_broadcast_drops);
  RUN_TEST(test_near_miss_target_drops);
  RUN_TEST(test_unset_self_mac_accepts_no_real_target);
  return UNITY_END();
}
