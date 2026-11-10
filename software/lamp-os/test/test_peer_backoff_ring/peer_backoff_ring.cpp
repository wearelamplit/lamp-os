// Native tests for PeerBackoffRing, the firmware distributor's per-peer OTA
// backoff, with the cases that straddle the 32-bit millis() wrap.

#include <unity.h>

#include <cstdint>

// Native-test seam: exercise the shipped class.
#include "../../src/components/firmware/peer_backoff_ring.cpp"

using lamp::PeerBackoffRing;

namespace {

constexpr uint32_t kBackoffMs = 600000;

void macFor(uint8_t i, uint8_t out[6]) {
  const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x09, i};
  for (int b = 0; b < 6; ++b) out[b] = mac[b];
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_transient_backoff_holds_then_expires() {
  PeerBackoffRing ring;
  uint8_t a[6], b[6];
  macFor(1, a);
  macFor(2, b);
  ring.note(a, 1000, kBackoffMs, false);
  TEST_ASSERT_TRUE(ring.inBackoff(a, 1000));
  TEST_ASSERT_TRUE(ring.inBackoff(a, 1000 + kBackoffMs - 1));
  TEST_ASSERT_FALSE(ring.inBackoff(a, 1000 + kBackoffMs));
  TEST_ASSERT_FALSE(ring.inBackoff(b, 1000));
}

// Noted 1 s before the wrap: the backoff still holds just before and well
// after the wrap, and ends one duration after it began.
void test_backoff_noted_before_the_wrap_holds_across_it() {
  PeerBackoffRing ring;
  uint8_t a[6];
  macFor(1, a);
  const uint32_t noted = UINT32_MAX - 999;
  ring.note(a, noted, kBackoffMs, false);
  TEST_ASSERT_TRUE(ring.inBackoff(a, noted + 500));
  TEST_ASSERT_TRUE(ring.inBackoff(a, 100000));
  TEST_ASSERT_FALSE(ring.inBackoff(a, noted + kBackoffMs));
}

// A backoff that ended before the wrap stays ended after it.
void test_backoff_expired_before_the_wrap_stays_expired() {
  PeerBackoffRing ring;
  uint8_t a[6];
  macFor(1, a);
  ring.note(a, UINT32_MAX - (kBackoffMs + 100000), kBackoffMs, false);
  TEST_ASSERT_FALSE(ring.inBackoff(a, UINT32_MAX - 50000));
  TEST_ASSERT_FALSE(ring.inBackoff(a, 1000));
}

void test_persistent_block_never_expires() {
  PeerBackoffRing ring;
  uint8_t a[6];
  macFor(1, a);
  ring.note(a, 1000, 0, true);
  TEST_ASSERT_TRUE(ring.inBackoff(a, 1000 + 10 * kBackoffMs));
}

// A ring full of persistent blocks set before the wrap; block 0, the first,
// is renewed after it. A new persistent block replaces block 1, now the
// longest held, and keeps block 0.
void test_full_persistent_ring_replaces_longest_held_across_the_wrap() {
  PeerBackoffRing ring;
  const uint32_t start = UINT32_MAX - 20000;
  uint8_t mac[6];
  for (uint8_t i = 0; i < PeerBackoffRing::kSize; ++i) {
    macFor(i, mac);
    ring.note(mac, start + i * 1000u, 0, true);
  }
  macFor(0, mac);
  ring.note(mac, 100, 0, true);
  uint8_t newcomer[6];
  macFor(PeerBackoffRing::kSize, newcomer);
  ring.note(newcomer, 200, 0, true);

  uint8_t first[6], second[6];
  macFor(0, first);
  macFor(1, second);
  TEST_ASSERT_TRUE(ring.inBackoff(first, 300));
  TEST_ASSERT_FALSE(ring.inBackoff(second, 300));
  TEST_ASSERT_TRUE(ring.inBackoff(newcomer, 300));
}

// A transient penalty that expired before the wrap is the slot a new peer
// takes after it, ahead of a still-active transient.
void test_expired_transient_slot_reused_across_the_wrap() {
  PeerBackoffRing ring;
  uint8_t mac[6];
  for (uint8_t i = 0; i < PeerBackoffRing::kSize; ++i) {
    macFor(i, mac);
    ring.note(mac, 1000, 0, true);
  }
  // Slot 0 becomes a transient still active after the wrap; slot 1 one that
  // ended before it.
  macFor(0, mac);
  ring.note(mac, UINT32_MAX - 1000, kBackoffMs, false);
  macFor(1, mac);
  ring.note(mac, UINT32_MAX - (kBackoffMs + 100000), kBackoffMs, false);

  uint8_t newcomer[6];
  macFor(PeerBackoffRing::kSize, newcomer);
  ring.note(newcomer, 5000, kBackoffMs, false);

  uint8_t active[6];
  macFor(0, active);
  TEST_ASSERT_TRUE(ring.inBackoff(newcomer, 6000));
  TEST_ASSERT_TRUE(ring.inBackoff(active, 6000));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_transient_backoff_holds_then_expires);
  RUN_TEST(test_backoff_noted_before_the_wrap_holds_across_it);
  RUN_TEST(test_backoff_expired_before_the_wrap_stays_expired);
  RUN_TEST(test_persistent_block_never_expires);
  RUN_TEST(test_full_persistent_ring_replaces_longest_held_across_the_wrap);
  RUN_TEST(test_expired_transient_slot_reused_across_the_wrap);
  return UNITY_END();
}
