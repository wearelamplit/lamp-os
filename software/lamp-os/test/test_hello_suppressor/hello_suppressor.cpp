// Native tests for HelloRelaySuppressor: counter-based HELLO rebroadcast
// suppression. A first-seen HELLO is enqueued (not relayed now); each
// duplicate heard before the delay elapses bumps a coverage counter; at fire
// time an over-covered entry is dropped, otherwise relayed. Overflow fails
// open (relay immediately). Clock is a passed-in nowMs; jitter is deterministic
// per (own MAC, mac, seq).

#define LAMP_DEBUG 1  // exercise the debug-gated suppression counters

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "../../src/components/network/mesh/hello_relay_suppressor.cpp"
#include "../../src/components/network/mesh/relay_delay.cpp"

using lamp::HelloRelaySuppressor;
using lamp::kHelloSuppressThreshold;
using lamp::kHelloRelayJitterMinMs;
using lamp::kHelloRelayJitterMaxMs;
using lamp::kHelloPendingSlots;

void setUp(void) {}
void tearDown(void) {}

static const uint8_t kMacA[6] = {0xAA, 0x11, 0x22, 0x33, 0x44, 0x01};
static const uint8_t kMacB[6] = {0xBB, 0x11, 0x22, 0x33, 0x44, 0x02};
// This lamp, and a neighbour from the same reel (last byte differs).
static const uint8_t kOwn[6] = {0x24, 0x6F, 0x28, 0x10, 0x20, 0x40};
static const uint8_t kOwnNext[6] = {0x24, 0x6F, 0x28, 0x10, 0x20, 0x41};

// Records every relayed frame so tests assert count + byte-identity.
struct RelayLog {
  std::vector<std::vector<uint8_t>> frames;
  HelloRelaySuppressor::RelayFn fn() {
    return [this](const uint8_t* f, size_t l) {
      frames.emplace_back(f, f + l);
    };
  }
};

void test_sparse_relays_once_after_fire() {
  HelloRelaySuppressor s;
  s.begin(kOwn);
  RelayLog log;
  const uint8_t frame[8] = {0x01, 0x05, 0, 1, 2, 3, 4, 5};

  const bool relayNow = s.onFirstSeen(kMacA, 7, frame, sizeof(frame), 1000);
  TEST_ASSERT_FALSE(relayNow);

  const uint32_t fireAt = 1000 + s.jitterMs(kMacA, 7);
  s.tick(fireAt, log.fn());

  TEST_ASSERT_EQUAL_UINT32(1, log.frames.size());
}

void test_dense_suppresses_when_threshold_met() {
  HelloRelaySuppressor s;
  s.begin(kOwn);
  RelayLog log;
  const uint8_t frame[8] = {0x01, 0x05, 0, 9, 8, 7, 6, 5};

  s.onFirstSeen(kMacA, 7, frame, sizeof(frame), 1000);
  for (uint8_t i = 0; i < kHelloSuppressThreshold; ++i) {
    s.onDuplicate(kMacA, 7);
  }

  const uint32_t fireAt = 1000 + s.jitterMs(kMacA, 7);
  s.tick(fireAt, log.fn());

  TEST_ASSERT_EQUAL_UINT32(0, log.frames.size());
}

void test_below_threshold_still_relays() {
  HelloRelaySuppressor s;
  s.begin(kOwn);
  RelayLog log;
  const uint8_t frame[8] = {0x01, 0x05, 0, 9, 8, 7, 6, 5};

  s.onFirstSeen(kMacA, 7, frame, sizeof(frame), 1000);
  for (uint8_t i = 0; i < kHelloSuppressThreshold - 1; ++i) {
    s.onDuplicate(kMacA, 7);
  }

  const uint32_t fireAt = 1000 + s.jitterMs(kMacA, 7);
  s.tick(fireAt, log.fn());

  TEST_ASSERT_EQUAL_UINT32(1, log.frames.size());
}

void test_overflow_fails_open() {
  HelloRelaySuppressor s;
  s.begin(kOwn);
  const uint8_t frame[4] = {0x01, 0x05, 0, 0};

  for (uint16_t seq = 0; seq < kHelloPendingSlots; ++seq) {
    TEST_ASSERT_FALSE(s.onFirstSeen(kMacA, seq, frame, sizeof(frame), 1000));
  }
  // Table full: a new first-seen must relay immediately.
  TEST_ASSERT_TRUE(s.onFirstSeen(kMacA, kHelloPendingSlots, frame,
                                 sizeof(frame), 1000));
}

void test_distinct_entries_tracked_independently() {
  HelloRelaySuppressor s;
  s.begin(kOwn);
  RelayLog log;
  const uint8_t frameA[4] = {0x01, 0x05, 0, 0xAA};
  const uint8_t frameB[4] = {0x01, 0x05, 0, 0xBB};

  s.onFirstSeen(kMacA, 7, frameA, sizeof(frameA), 1000);
  s.onFirstSeen(kMacB, 7, frameB, sizeof(frameB), 1000);
  // Saturate only A's coverage; a dup for A must not touch B.
  for (uint8_t i = 0; i < kHelloSuppressThreshold; ++i) {
    s.onDuplicate(kMacA, 7);
  }

  const uint32_t fireAtA = 1000 + s.jitterMs(kMacA, 7);
  const uint32_t fireAtB = 1000 + s.jitterMs(kMacB, 7);
  const uint32_t after = (fireAtA > fireAtB ? fireAtA : fireAtB);
  s.tick(after, log.fn());

  // A suppressed, B relayed: exactly one relay, and it is B's frame.
  TEST_ASSERT_EQUAL_UINT32(1, log.frames.size());
  TEST_ASSERT_EQUAL_UINT8(0xBB, log.frames[0][3]);
}

void test_does_not_fire_before_fire_time() {
  HelloRelaySuppressor s;
  s.begin(kOwn);
  RelayLog log;
  const uint8_t frame[4] = {0x01, 0x05, 0, 0};

  s.onFirstSeen(kMacA, 7, frame, sizeof(frame), 1000);
  const uint32_t fireAt = 1000 + s.jitterMs(kMacA, 7);

  s.tick(fireAt - 1, log.fn());
  TEST_ASSERT_EQUAL_UINT32(0, log.frames.size());

  s.tick(fireAt, log.fn());
  TEST_ASSERT_EQUAL_UINT32(1, log.frames.size());
}

void test_jitter_within_bounds_and_deterministic() {
  HelloRelaySuppressor s;
  s.begin(kOwn);
  const uint32_t a1 = s.jitterMs(kMacA, 7);
  const uint32_t a2 = s.jitterMs(kMacA, 7);
  const uint32_t b = s.jitterMs(kMacB, 7);

  TEST_ASSERT_EQUAL_UINT32(a1, a2);
  TEST_ASSERT_TRUE(a1 >= kHelloRelayJitterMinMs && a1 <= kHelloRelayJitterMaxMs);
  TEST_ASSERT_TRUE(b >= kHelloRelayJitterMinMs && b <= kHelloRelayJitterMaxMs);
}

void test_relayed_frame_is_byte_identical() {
  HelloRelaySuppressor s;
  s.begin(kOwn);
  RelayLog log;
  const uint8_t frame[12] = {0x01, 0x05, 0x04, 0xDE, 0xAD, 0xBE,
                             0xEF, 0x10, 0x20, 0x30, 0x40, 0x50};

  s.onFirstSeen(kMacA, 42, frame, sizeof(frame), 5000);
  const uint32_t fireAt = 5000 + s.jitterMs(kMacA, 42);
  s.tick(fireAt, log.fn());

  TEST_ASSERT_EQUAL_UINT32(1, log.frames.size());
  TEST_ASSERT_EQUAL_UINT32(sizeof(frame), log.frames[0].size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(frame, log.frames[0].data(), sizeof(frame));
}

void test_slot_freed_after_fire() {
  HelloRelaySuppressor s;
  s.begin(kOwn);
  RelayLog log;
  const uint8_t frame[4] = {0x01, 0x05, 0, 0};

  s.onFirstSeen(kMacA, 7, frame, sizeof(frame), 1000);
  const uint32_t fireAt = 1000 + s.jitterMs(kMacA, 7);
  s.tick(fireAt, log.fn());
  TEST_ASSERT_EQUAL_UINT32(1, log.frames.size());

  // A second tick must not re-relay a fired entry.
  s.tick(fireAt + 10000, log.fn());
  TEST_ASSERT_EQUAL_UINT32(1, log.frames.size());
}

void test_counters_classify_suppressed_relayed_and_failopen() {
  HelloRelaySuppressor s;
  s.begin(kOwn);
  RelayLog log;
  const uint8_t frame[4] = {0x01, 0x05, 0, 0};

  // One dense entry (suppressed) and one sparse (relayed).
  s.onFirstSeen(kMacA, 1, frame, sizeof(frame), 1000);
  for (uint8_t i = 0; i < kHelloSuppressThreshold; ++i) s.onDuplicate(kMacA, 1);
  s.onFirstSeen(kMacB, 2, frame, sizeof(frame), 1000);
  s.tick(1000 + kHelloRelayJitterMaxMs + 1, log.fn());

  TEST_ASSERT_EQUAL_UINT32(1, s.suppressedWindow());
  TEST_ASSERT_EQUAL_UINT32(1, s.relayedWindow());

  // Fail-open (table full) counts as a relay; resetWindow clears the window.
  s.resetWindow();
  for (uint16_t seq = 0; seq < kHelloPendingSlots; ++seq) {
    s.onFirstSeen(kMacA, 100 + seq, frame, sizeof(frame), 2000);
  }
  TEST_ASSERT_TRUE(s.onFirstSeen(kMacA, 999, frame, sizeof(frame), 2000));
  TEST_ASSERT_EQUAL_UINT32(0, s.suppressedWindow());
  TEST_ASSERT_EQUAL_UINT32(1, s.relayedWindow());
}

// First seen 20 ms before the millis() wrap, so its fire time lands after it.
// A tick still before the wrap is inside the jitter and must not relay, or
// suppression is skipped.
void test_fire_time_past_the_wrap_waits_for_it() {
  HelloRelaySuppressor s;
  s.begin(kOwn);
  RelayLog log;
  const uint8_t frame[8] = {0x01, 0x05, 0, 1, 2, 3, 4, 5};
  const uint32_t t0 = UINT32_MAX - 20;
  const uint32_t fireAt = t0 + s.jitterMs(kMacA, 7);
  TEST_ASSERT_TRUE(fireAt < t0);

  s.onFirstSeen(kMacA, 7, frame, sizeof(frame), t0);
  s.tick(t0 + 5, log.fn());
  s.tick(fireAt - 1, log.fn());
  TEST_ASSERT_EQUAL_UINT32(0, log.frames.size());

  s.tick(fireAt, log.fn());
  TEST_ASSERT_EQUAL_UINT32(1, log.frames.size());
}

// Fire time before the wrap, first tick after it (a loop stalled across the
// wrap): the entry still fires and frees its slot, so the table keeps its
// full capacity instead of failing open early.
void test_fire_time_before_the_wrap_fires_after_it() {
  HelloRelaySuppressor s;
  s.begin(kOwn);
  RelayLog log;
  const uint8_t frame[8] = {0x01, 0x05, 0, 1, 2, 3, 4, 5};
  const uint32_t t0 = UINT32_MAX - 300;
  const uint32_t fireAt = t0 + s.jitterMs(kMacA, 7);
  TEST_ASSERT_TRUE(fireAt > t0);

  s.onFirstSeen(kMacA, 7, frame, sizeof(frame), t0);
  s.tick(0x200, log.fn());
  TEST_ASSERT_EQUAL_UINT32(1, log.frames.size());

  for (uint16_t seq = 0; seq < kHelloPendingSlots; ++seq) {
    TEST_ASSERT_FALSE(s.onFirstSeen(kMacB, seq, frame, sizeof(frame), 0x300));
  }
}

// Two neighbours hear the same HELLO: each relays it at its own time, so the
// first ring of relayers does not fire in step.
static uint32_t firstRelayAt(const uint8_t own[6], uint16_t seq) {
  HelloRelaySuppressor s;
  s.begin(own);
  RelayLog log;
  const uint8_t frame[8] = {0x01, 0x05, 0, 1, 2, 3, 4, 5};
  s.onFirstSeen(kMacA, seq, frame, sizeof(frame), 1000);
  for (uint32_t t = 1000; t <= 1000 + kHelloRelayJitterMaxMs; ++t) {
    s.tick(t, log.fn());
    if (!log.frames.empty()) return t;
  }
  return 0;
}

void test_two_relayers_fire_apart() {
  uint32_t apart = 0;
  for (uint16_t seq = 0; seq < 20; ++seq) {
    const uint32_t a = firstRelayAt(kOwn, seq);
    const uint32_t b = firstRelayAt(kOwnNext, seq);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, a);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, b);
    if (a != b) ++apart;
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(18, apart);
}

// The HELLO delay window: the offset between two neighbours changes from
// frame to frame, and every delay stays inside it.
void test_hello_pair_offset_varies_and_bounded() {
  constexpr uint32_t span = kHelloRelayJitterMaxMs - kHelloRelayJitterMinMs + 1;
  uint32_t counts[span] = {0};
  for (uint16_t seq = 0; seq < 200; ++seq) {
    const uint32_t a = lamp::relayDelayMs(kOwn, kMacA, lamp_protocol::MSG_HELLO, seq,
                                          kHelloRelayJitterMinMs, kHelloRelayJitterMaxMs);
    const uint32_t b = lamp::relayDelayMs(kOwnNext, kMacA, lamp_protocol::MSG_HELLO, seq,
                                          kHelloRelayJitterMinMs, kHelloRelayJitterMaxMs);
    TEST_ASSERT_TRUE(a >= kHelloRelayJitterMinMs && a <= kHelloRelayJitterMaxMs);
    TEST_ASSERT_TRUE(b >= kHelloRelayJitterMinMs && b <= kHelloRelayJitterMaxMs);
    counts[(a + span - b) % span]++;
  }
  uint32_t distinct = 0, most = 0;
  for (uint32_t c : counts) {
    if (c) ++distinct;
    if (c > most) most = c;
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(100, distinct);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(200 * 5 / 100, most);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_sparse_relays_once_after_fire);
  RUN_TEST(test_dense_suppresses_when_threshold_met);
  RUN_TEST(test_below_threshold_still_relays);
  RUN_TEST(test_overflow_fails_open);
  RUN_TEST(test_distinct_entries_tracked_independently);
  RUN_TEST(test_does_not_fire_before_fire_time);
  RUN_TEST(test_jitter_within_bounds_and_deterministic);
  RUN_TEST(test_relayed_frame_is_byte_identical);
  RUN_TEST(test_slot_freed_after_fire);
  RUN_TEST(test_counters_classify_suppressed_relayed_and_failopen);
  RUN_TEST(test_fire_time_past_the_wrap_waits_for_it);
  RUN_TEST(test_fire_time_before_the_wrap_fires_after_it);
  RUN_TEST(test_two_relayers_fire_apart);
  RUN_TEST(test_hello_pair_offset_varies_and_bounded);
  return UNITY_END();
}
