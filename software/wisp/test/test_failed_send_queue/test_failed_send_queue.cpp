// Native tests for FailedSendQueue.
//
// Pins the contract:
//   - push/pop round-trips a MAC, FIFO order.
//   - pop on empty returns false.
//   - push of an already-pending MAC is a no-op (dedup), doesn't grow.
//   - push past kCapacity is dropped silently, doesn't overwrite pending entries.
//   - pop after wraparound (interleaved push/pop past the array end) stays FIFO.

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "net/failed_send_queue.hpp"

using wisp::FailedSendQueue;

namespace {

void mac(uint8_t out[6], uint8_t last) {
  const uint8_t m[6] = {0x11, 0x22, 0x33, 0x44, 0x55, last};
  std::memcpy(out, m, 6);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_push_pop_roundtrips_fifo() {
  FailedSendQueue q;
  uint8_t m1[6], m2[6];
  mac(m1, 1);
  mac(m2, 2);
  q.push(m1);
  q.push(m2);
  TEST_ASSERT_EQUAL_size_t(2, q.size());

  uint8_t out[6];
  TEST_ASSERT_TRUE(q.pop(out));
  TEST_ASSERT_EQUAL_MEMORY(m1, out, 6);
  TEST_ASSERT_TRUE(q.pop(out));
  TEST_ASSERT_EQUAL_MEMORY(m2, out, 6);
  TEST_ASSERT_EQUAL_size_t(0, q.size());
}

void test_pop_empty_returns_false() {
  FailedSendQueue q;
  uint8_t out[6];
  TEST_ASSERT_FALSE(q.pop(out));
}

void test_push_dedups_pending_mac() {
  FailedSendQueue q;
  uint8_t m1[6];
  mac(m1, 1);
  q.push(m1);
  q.push(m1);
  q.push(m1);
  TEST_ASSERT_EQUAL_size_t(1, q.size());
}

void test_push_past_capacity_drops_silently() {
  FailedSendQueue q;
  for (uint8_t i = 0; i < FailedSendQueue::kCapacity + 3; i++) {
    uint8_t m[6];
    mac(m, i);
    q.push(m);
  }
  TEST_ASSERT_EQUAL_size_t(FailedSendQueue::kCapacity, q.size());

  // The first kCapacity entries survive; the overflow ones never landed.
  uint8_t out[6];
  for (uint8_t i = 0; i < FailedSendQueue::kCapacity; i++) {
    uint8_t expect[6];
    mac(expect, i);
    TEST_ASSERT_TRUE(q.pop(out));
    TEST_ASSERT_EQUAL_MEMORY(expect, out, 6);
  }
  TEST_ASSERT_FALSE(q.pop(out));
}

void test_wraparound_stays_fifo() {
  FailedSendQueue q;
  uint8_t out[6];
  // Fill, drain half, push more so the tail wraps past the array end,
  // then confirm pop order still matches push order.
  for (uint8_t i = 0; i < FailedSendQueue::kCapacity; i++) {
    uint8_t m[6];
    mac(m, i);
    q.push(m);
  }
  for (uint8_t i = 0; i < FailedSendQueue::kCapacity / 2; i++) {
    TEST_ASSERT_TRUE(q.pop(out));
  }
  for (uint8_t i = 100; i < 100 + FailedSendQueue::kCapacity / 2; i++) {
    uint8_t m[6];
    mac(m, i);
    q.push(m);
  }

  for (uint8_t i = FailedSendQueue::kCapacity / 2; i < FailedSendQueue::kCapacity; i++) {
    uint8_t expect[6];
    mac(expect, i);
    TEST_ASSERT_TRUE(q.pop(out));
    TEST_ASSERT_EQUAL_MEMORY(expect, out, 6);
  }
  for (uint8_t i = 100; i < 100 + FailedSendQueue::kCapacity / 2; i++) {
    uint8_t expect[6];
    mac(expect, i);
    TEST_ASSERT_TRUE(q.pop(out));
    TEST_ASSERT_EQUAL_MEMORY(expect, out, 6);
  }
  TEST_ASSERT_FALSE(q.pop(out));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_push_pop_roundtrips_fifo);
  RUN_TEST(test_pop_empty_returns_false);
  RUN_TEST(test_push_dedups_pending_mac);
  RUN_TEST(test_push_past_capacity_drops_silently);
  RUN_TEST(test_wraparound_stays_fifo);
  return UNITY_END();
}
