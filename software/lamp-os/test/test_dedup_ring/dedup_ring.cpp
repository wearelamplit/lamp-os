// Native tests for DedupRing: (mac, msgType, seq) keying, oldest-first
// eviction, and the age past which a recorded tuple stops suppressing.

#include <unity.h>

#include <cstdint>

#include <lampos/protocol/dedup_ring.hpp>

using lamp_protocol::kDedupMaxAgeMs;

void setUp(void) {}
void tearDown(void) {}

static const uint8_t kMacA[6] = {0xAA, 0x11, 0x22, 0x33, 0x44, 0x01};
static const uint8_t kMacB[6] = {0xBB, 0x11, 0x22, 0x33, 0x44, 0x02};

// Off zero so a ring that ignored time would still read as fresh.
constexpr uint32_t kT0 = 123456;

void test_first_record_returns_true_and_records() {
  lamp_protocol::DedupRing<> ring;
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, 100, kT0));
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x02, 100, kT0));
}

void test_different_seq_is_independent() {
  lamp_protocol::DedupRing<> ring;
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, 100, kT0));
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, 101, kT0));
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x02, 101, kT0));
}

void test_different_msgtype_is_independent() {
  lamp_protocol::DedupRing<> ring;
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x01, 50, kT0));
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, 50, kT0));
}

void test_different_mac_is_independent() {
  lamp_protocol::DedupRing<> ring;
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, 100, kT0));
  TEST_ASSERT_TRUE(ring.record(kMacB, 0x02, 100, kT0));
}

void test_full_ring_evicts_oldest_first() {
  lamp_protocol::DedupRing<> ring;
  for (uint16_t s = 0; s < 64; ++s) {
    TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, s, kT0));
  }
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x02, 0, kT0));
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x02, 63, kT0));

  // A 65th tuple takes seq 0's slot; re-adding seq 0 then takes seq 1's.
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, 64, kT0));
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, 0, kT0));
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x02, 2, kT0));
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, 1, kT0));
}

void test_empty_ring_does_not_match_zero_mac() {
  lamp_protocol::DedupRing<> ring;
  const uint8_t zeroMac[6] = {0, 0, 0, 0, 0, 0};
  TEST_ASSERT_TRUE(ring.record(zeroMac, 0x00, 0, 0));
  TEST_ASSERT_FALSE(ring.record(zeroMac, 0x00, 0, 0));
}

void test_tuple_suppresses_through_max_age() {
  lamp_protocol::DedupRing<16> ring;
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x40, 7, kT0));
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x40, 7, kT0 + 1500));
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x40, 7, kT0 + kDedupMaxAgeMs));
}

void test_tuple_expires_past_max_age() {
  lamp_protocol::DedupRing<16> ring;
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x40, 7, kT0));
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x40, 7, kT0 + kDedupMaxAgeMs + 1));
  // Re-recorded at the later time, it suppresses again from there.
  TEST_ASSERT_FALSE(
      ring.record(kMacA, 0x40, 7, kT0 + kDedupMaxAgeMs + 1 + kDedupMaxAgeMs));
}

void test_age_holds_across_millis_wrap() {
  lamp_protocol::DedupRing<16> ring;
  const uint32_t nearWrap = 0xFFFFFFFFu - 5000;
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x40, 9, nearWrap));
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x40, 9, nearWrap + 6000));
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x40, 9, nearWrap + kDedupMaxAgeMs + 1));
}

// The send path records with its own millis(), which can land a moment after
// the recv task read nowMs; that tuple must still read as seen.
void test_stamp_slightly_ahead_still_suppresses() {
  lamp_protocol::DedupRing<16> ring;
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x03, 11, kT0 + 5));
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x03, 11, kT0));
}

// A stale tuple for one sender doesn't age out a fresh one beside it.
void test_expiry_is_per_entry() {
  lamp_protocol::DedupRing<16> ring;
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x03, 1, kT0));
  TEST_ASSERT_TRUE(ring.record(kMacB, 0x03, 1, kT0 + 15000));
  const uint32_t later = kT0 + kDedupMaxAgeMs + 1;
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x03, 1, later));
  TEST_ASSERT_FALSE(ring.record(kMacB, 0x03, 1, later));
}

// A scan that finds an entry past the age clears it, so one wrap of millis()
// later the same tuple, whose raw age reads near 0 again, is still new.
void test_expired_entry_cleared_before_millis_wraps_back() {
  lamp_protocol::DedupRing<16> ring;
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x40, 3, kT0));
  TEST_ASSERT_TRUE(ring.record(kMacB, 0x40, 4, kT0 + 30000));
  const uint32_t oneWrapLater = static_cast<uint32_t>(kT0 + (uint64_t{1} << 32));
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x40, 3, oneWrapLater));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_first_record_returns_true_and_records);
  RUN_TEST(test_different_seq_is_independent);
  RUN_TEST(test_different_msgtype_is_independent);
  RUN_TEST(test_different_mac_is_independent);
  RUN_TEST(test_full_ring_evicts_oldest_first);
  RUN_TEST(test_empty_ring_does_not_match_zero_mac);
  RUN_TEST(test_tuple_suppresses_through_max_age);
  RUN_TEST(test_tuple_expires_past_max_age);
  RUN_TEST(test_age_holds_across_millis_wrap);
  RUN_TEST(test_stamp_slightly_ahead_still_suppresses);
  RUN_TEST(test_expiry_is_per_entry);
  RUN_TEST(test_expired_entry_cleared_before_millis_wraps_back);
  return UNITY_END();
}
