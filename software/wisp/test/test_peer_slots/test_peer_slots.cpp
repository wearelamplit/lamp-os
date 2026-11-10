// Native tests for PeerSlots: the LRU table of ESP-NOW unicast peers.
#include <unity.h>

#include <cstdint>
#include <cstring>

#include "net/peer_slots.hpp"

using wisp::PeerSlots;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint64_t kWrap = uint64_t{1} << 32;

void macFor(uint8_t out[6], uint8_t id) {
  const uint8_t m[6] = {0xCC, 0x00, 0x00, 0x00, 0x01, id};
  std::memcpy(out, m, 6);
}

// Fills every slot with peers 1..kCapacity, peer i touched at stampOf(i).
template <typename StampOf>
void fill(PeerSlots& slots, StampOf stampOf) {
  for (uint8_t i = 1; i <= PeerSlots::kCapacity; ++i) {
    uint8_t m[6];
    macFor(m, i);
    slots.touch(m, stampOf(i));
  }
}

}  // namespace

void test_new_mac_is_added_once() {
  PeerSlots slots;
  uint8_t m[6];
  macFor(m, 1);
  const PeerSlots::Change first = slots.touch(m, 1000);
  TEST_ASSERT_TRUE(first.added);
  TEST_ASSERT_FALSE(first.evicted);
  const PeerSlots::Change again = slots.touch(m, 2000);
  TEST_ASSERT_FALSE(again.added);
  TEST_ASSERT_FALSE(again.evicted);
}

void test_table_fills_without_eviction() {
  PeerSlots slots;
  for (uint8_t i = 1; i <= PeerSlots::kCapacity; ++i) {
    uint8_t m[6];
    macFor(m, i);
    const PeerSlots::Change c = slots.touch(m, 1000 + i);
    TEST_ASSERT_TRUE(c.added);
    TEST_ASSERT_FALSE(c.evicted);
  }
}

// Peer 1 is re-used after the others, so the least recently used is peer 2.
void test_full_table_evicts_least_recently_used() {
  PeerSlots slots;
  fill(slots, [](uint8_t i) { return uint64_t{1000} + i * 7; });
  uint8_t m[6];
  macFor(m, 1);
  slots.touch(m, 5000);

  uint8_t fresh[6];
  macFor(fresh, 200);
  const PeerSlots::Change c = slots.touch(fresh, 6000);
  TEST_ASSERT_TRUE(c.added);
  TEST_ASSERT_TRUE(c.evicted);
  uint8_t expected[6];
  macFor(expected, 2);
  TEST_ASSERT_EQUAL_MEMORY(expected, c.evictedMac, 6);
}

// Stamps straddle the point millis() wraps: peer 1 was used 1 s before it,
// the rest after it. Peer 1 is the oldest.
void test_eviction_orders_stamps_across_the_millis_wrap() {
  PeerSlots slots;
  fill(slots, [](uint8_t i) {
    return i == 1 ? kWrap - 1000 : kWrap + 10 + i;
  });
  uint8_t fresh[6];
  macFor(fresh, 200);
  const PeerSlots::Change c = slots.touch(fresh, kWrap + 500);
  TEST_ASSERT_TRUE(c.evicted);
  uint8_t expected[6];
  macFor(expected, 1);
  TEST_ASSERT_EQUAL_MEMORY(expected, c.evictedMac, 6);
}

// Peer 1 sat unused for a full millis() wrap while the others were used
// within the last few seconds. Peer 1 is still the oldest.
void test_peer_idle_for_a_full_wrap_is_evicted_first() {
  PeerSlots slots;
  fill(slots, [](uint8_t i) {
    return i == 1 ? uint64_t{1000} : kWrap + 900 - i * 50;
  });
  uint8_t fresh[6];
  macFor(fresh, 200);
  const PeerSlots::Change c = slots.touch(fresh, kWrap + 2000);
  TEST_ASSERT_TRUE(c.evicted);
  uint8_t expected[6];
  macFor(expected, 1);
  TEST_ASSERT_EQUAL_MEMORY(expected, c.evictedMac, 6);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_new_mac_is_added_once);
  RUN_TEST(test_table_fills_without_eviction);
  RUN_TEST(test_full_table_evicts_least_recently_used);
  RUN_TEST(test_eviction_orders_stamps_across_the_millis_wrap);
  RUN_TEST(test_peer_idle_for_a_full_wrap_is_evicted_first);
  return UNITY_END();
}
