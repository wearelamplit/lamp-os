// Native tests for RelayStagger and relayDelayMs: every queued relay goes out
// once, byte-identical, at its fire time, only while a TX credit is ready, and
// is dropped once kMaxRelayLateMs late, failing open during a known loop
// stall; the delay desynchronizes relayers per pair and per frame.

#define LAMP_DEBUG 1  // exercise the debug-gated window counters

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <set>
#include <vector>

#include "../../src/components/network/mesh/relay_delay.cpp"
#include "../../src/components/network/mesh/relay_stagger.hpp"

using lamp::kMaxRelayLateMs;
using lamp::kRelayStaggerWindowMs;
using lamp::RelayStagger;
using lamp::relayDelayMs;

void setUp(void) {}
void tearDown(void) {}

namespace {

constexpr uint32_t kW = kRelayStaggerWindowMs;
constexpr uint8_t kType = 0x03;
constexpr size_t kBuf = 64;
constexpr size_t kSlots = 4;
const uint8_t kWisp[6] = {0x54, 0x32, 0x04, 0xAA, 0xBB, 0x01};

// Sequential MACs from one production reel: only the last byte differs.
void ownMac(uint8_t i, uint8_t out[6]) {
  const uint8_t base[6] = {0x24, 0x6F, 0x28, 0x10, 0x20, 0x00};
  std::memcpy(out, base, 6);
  out[5] = i;
}

std::vector<uint8_t> frameFor(uint16_t seq, size_t len = 24) {
  std::vector<uint8_t> f(len);
  for (size_t i = 0; i < len; ++i) f[i] = static_cast<uint8_t>(seq * 7 + i);
  f[0] = static_cast<uint8_t>(seq);
  f[1] = static_cast<uint8_t>(seq >> 8);
  return f;
}

uint32_t delayFor(uint8_t own, uint16_t seq) {
  uint8_t mac[6];
  ownMac(own, mac);
  return relayDelayMs(mac, kWisp, kType, seq, 0, kW);
}

struct Sent {
  uint32_t atMs;
  std::vector<uint8_t> frame;
};

struct Link {
  std::vector<Sent> sent;
  uint32_t attempts = 0;
  uint32_t failNext = 0;
  bool alwaysFail = false;
  bool ready = true;
  uint32_t now = 0;

  auto readyFn() {
    return [this](uint32_t) { return ready; };
  }
  auto sendFn() {
    return [this](const uint8_t* f, size_t len) {
      ++attempts;
      if (alwaysFail) return false;
      if (failNext) {
        --failNext;
        return false;
      }
      sent.push_back({now, std::vector<uint8_t>(f, f + len)});
      return true;
    };
  }
  template <class Stagger>
  void service(Stagger& s, uint32_t nowMs) {
    now = nowMs;
    s.service(nowMs, readyFn(), sendFn());
  }
};

template <size_t B, size_t S>
void beginAs(RelayStagger<B, S>& s, uint8_t own) {
  uint8_t mac[6];
  ownMac(own, mac);
  s.begin(mac);
}

}  // namespace

// 50 frames, one every 40 ms so the 4-slot table never fills: each goes out
// exactly once and byte-identical.
void test_exactly_once() {
  RelayStagger<kBuf, kSlots> s;
  beginAs(s, 1);
  Link link;
  const uint32_t t0 = 10000;
  const uint16_t kFrames = 50;
  for (uint32_t t = t0; t <= t0 + kFrames * 40 + kW + 10; ++t) {
    if (t < t0 + kFrames * 40 && (t - t0) % 40 == 0) {
      const uint16_t seq = static_cast<uint16_t>((t - t0) / 40);
      const auto f = frameFor(seq);
      TEST_ASSERT_TRUE(s.enqueue(f.data(), f.size(), kType, kWisp, seq, t));
    }
    link.service(s, t);
  }
  TEST_ASSERT_EQUAL_UINT32(kFrames, link.sent.size());
  for (uint16_t seq = 0; seq < kFrames; ++seq) {
    const auto f = frameFor(seq);
    const auto n = std::count_if(link.sent.begin(), link.sent.end(),
                                 [&](const Sent& x) { return x.frame == f; });
    TEST_ASSERT_EQUAL_INT(1, n);
  }
  TEST_ASSERT_EQUAL_UINT32(kFrames, s.window().queued);
  TEST_ASSERT_EQUAL_UINT32(0, s.window().failOpen);
  TEST_ASSERT_EQUAL_UINT32(0, s.window().dropped);
}

// Serviced every ms with a credit ready, a frame goes out at its fire time:
// never before it, never after it.
void test_never_early_and_by_deadline() {
  for (uint16_t seq = 0; seq < 40; ++seq) {
    RelayStagger<kBuf, kSlots> s;
    beginAs(s, 2);
    Link link;
    const uint32_t t0 = 5000 + seq * 13;
    const auto f = frameFor(seq);
    TEST_ASSERT_TRUE(s.enqueue(f.data(), f.size(), kType, kWisp, seq, t0));
    for (uint32_t t = t0; t <= t0 + kW + 10; ++t) link.service(s, t);
    TEST_ASSERT_EQUAL_UINT32(1, link.sent.size());
    TEST_ASSERT_EQUAL_UINT32(t0 + delayFor(2, seq), link.sent[0].atMs);
    TEST_ASSERT_EQUAL_UINT32(0, s.window().maxLateMs);
  }
}

// A full table and an oversize frame both fail open; the oversize frame is
// never stored, so never sent truncated.
void test_fails_open() {
  RelayStagger<kBuf, kSlots> s;
  beginAs(s, 3);
  Link link;
  for (uint16_t seq = 0; seq < kSlots + 3; ++seq) {
    const auto f = frameFor(seq);
    TEST_ASSERT_EQUAL(seq < kSlots, s.enqueue(f.data(), f.size(), kType, kWisp, seq, 100));
  }
  TEST_ASSERT_EQUAL_UINT32(kSlots, s.window().queued);
  TEST_ASSERT_EQUAL_UINT32(3, s.window().failOpen);

  RelayStagger<kBuf, kSlots> big;
  beginAs(big, 3);
  const auto over = frameFor(9, kBuf + 1);
  TEST_ASSERT_FALSE(big.enqueue(over.data(), over.size(), kType, kWisp, 9, 100));
  const auto fits = frameFor(10, kBuf);
  TEST_ASSERT_TRUE(big.enqueue(fits.data(), fits.size(), kType, kWisp, 10, 100));
  for (uint32_t t = 100; t <= 100 + kW + 10; ++t) link.service(big, t);
  TEST_ASSERT_EQUAL_UINT32(1, link.sent.size());
  TEST_ASSERT_TRUE(link.sent[0].frame == fits);
  TEST_ASSERT_EQUAL_UINT32(1, big.window().failOpen);
}

// No credit means no submit: 30 passes without one send nothing, then the
// relay goes out once.
void test_not_ready_defers() {
  RelayStagger<kBuf, kSlots> s;
  beginAs(s, 4);
  Link link;
  const auto f = frameFor(7);
  TEST_ASSERT_TRUE(s.enqueue(f.data(), f.size(), kType, kWisp, 7, 2000));
  const uint32_t fire = 2000 + delayFor(4, 7);
  link.ready = false;
  for (uint32_t i = 0; i < 30; ++i) link.service(s, fire + i);
  TEST_ASSERT_EQUAL_UINT32(0, link.attempts);
  TEST_ASSERT_EQUAL_UINT32(30, s.window().deferred);
  link.ready = true;
  for (uint32_t i = 30; i < 60; ++i) link.service(s, fire + i);
  TEST_ASSERT_EQUAL_UINT32(1, link.attempts);
  TEST_ASSERT_EQUAL_UINT32(1, link.sent.size());
  TEST_ASSERT_EQUAL_UINT32(30, s.window().maxLateMs);
}

// A submit refused with a credit ready is a driver error: retried on the next
// pass and sent once.
void test_driver_error_retries() {
  RelayStagger<kBuf, kSlots> s;
  beginAs(s, 5);
  Link link;
  link.failNext = 1;
  const auto f = frameFor(8);
  TEST_ASSERT_TRUE(s.enqueue(f.data(), f.size(), kType, kWisp, 8, 3000));
  const uint32_t fire = 3000 + delayFor(5, 8);
  link.service(s, fire);
  TEST_ASSERT_EQUAL_UINT32(1, link.attempts);
  TEST_ASSERT_EQUAL_UINT32(0, link.sent.size());
  link.service(s, fire + 1);
  TEST_ASSERT_EQUAL_UINT32(2, link.attempts);
  TEST_ASSERT_EQUAL_UINT32(1, link.sent.size());
  TEST_ASSERT_TRUE(link.sent[0].frame == f);
  for (uint32_t t = fire + 2; t < fire + 2000; ++t) link.service(s, t);
  TEST_ASSERT_EQUAL_UINT32(2, link.attempts);
}

// A driver that never takes the frame: the slot retries until kMaxRelayLateMs
// past its fire time, then is freed and counted dropped, with no send after.
void test_late_cap() {
  RelayStagger<kBuf, 1> s;
  beginAs(s, 6);
  Link link;
  link.alwaysFail = true;
  const auto f = frameFor(11);
  TEST_ASSERT_TRUE(s.enqueue(f.data(), f.size(), kType, kWisp, 11, 4000));
  const uint32_t fire = 4000 + delayFor(6, 11);
  for (uint32_t t = 4000; t < fire + kMaxRelayLateMs; ++t) link.service(s, t);
  TEST_ASSERT_EQUAL_UINT32(kMaxRelayLateMs, link.attempts);
  TEST_ASSERT_EQUAL_UINT32(0, s.window().dropped);
  link.service(s, fire + kMaxRelayLateMs);
  TEST_ASSERT_EQUAL_UINT32(kMaxRelayLateMs, link.attempts);
  TEST_ASSERT_EQUAL_UINT32(1, s.window().dropped);
  for (uint32_t t = fire + kMaxRelayLateMs + 1; t < fire + 3000; ++t) link.service(s, t);
  TEST_ASSERT_EQUAL_UINT32(kMaxRelayLateMs, link.attempts);
  TEST_ASSERT_EQUAL_UINT32(1, s.window().dropped);
  const auto g = frameFor(12);
  TEST_ASSERT_TRUE(s.enqueue(g.data(), g.size(), kType, kWisp, 12, fire + 3000));
}

// One frame heard by 32 sequential-MAC lamps: their delays spread over the
// window instead of stacking.
void test_desync_across_lamps() {
  std::set<uint32_t> distinct;
  uint32_t lo = UINT32_MAX, hi = 0;
  for (uint8_t i = 0; i < 32; ++i) {
    const uint32_t d = delayFor(i, 500);
    distinct.insert(d);
    lo = std::min(lo, d);
    hi = std::max(hi, d);
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(24, distinct.size());
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(kW * 8 / 10, hi - lo);
}

void test_varies_across_frames() {
  uint32_t lo = UINT32_MAX, hi = 0;
  for (uint16_t seq = 0; seq < 200; ++seq) {
    const uint32_t d = delayFor(9, seq);
    lo = std::min(lo, d);
    hi = std::max(hi, d);
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(kW * 8 / 10, hi - lo);
}

// Two lamps whose MACs differ in the last byte: their offset changes from
// frame to frame, so no fixed offset (a pair that always collides) dominates.
void test_pair_offset_varies() {
  uint32_t counts[kW + 1] = {0};
  for (uint16_t seq = 0; seq < 200; ++seq) {
    const uint32_t off = (delayFor(0x40, seq) + (kW + 1) - delayFor(0x41, seq)) % (kW + 1);
    counts[off]++;
  }
  uint32_t distinct = 0, most = 0;
  for (uint32_t c : counts) {
    if (c) ++distinct;
    most = std::max(most, c);
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(100, distinct);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(200 * 5 / 100, most);
}

void test_bounds() {
  for (uint8_t i = 0; i < 32; ++i) {
    uint8_t mac[6];
    ownMac(i, mac);
    for (uint16_t seq = 0; seq < 200; ++seq) {
      const uint32_t d = relayDelayMs(mac, kWisp, kType, seq, 0, kW);
      TEST_ASSERT_LESS_OR_EQUAL_UINT32(kW, d);
      const uint32_t h = relayDelayMs(mac, kWisp, 0x01, seq, 50, 200);
      TEST_ASSERT_TRUE(h >= 50 && h <= 200);
    }
    TEST_ASSERT_EQUAL_UINT32(77, relayDelayMs(mac, kWisp, kType, 3, 77, 77));
  }
}

// Enqueued half a delay before the millis() wrap, so every fire time lies past
// it: nothing goes out before the fire time, then exactly one copy.
void test_wrap() {
  for (uint16_t seq = 0; seq < 40; ++seq) {
    const uint32_t d = delayFor(10, seq);
    if (d < 2) continue;
    RelayStagger<kBuf, kSlots> s;
    beginAs(s, 10);
    Link link;
    const uint32_t t0 = 0u - d / 2;
    const uint32_t fire = t0 + d;
    TEST_ASSERT_TRUE(fire < t0);
    const auto f = frameFor(seq);
    TEST_ASSERT_TRUE(s.enqueue(f.data(), f.size(), kType, kWisp, seq, t0));
    for (uint32_t t = t0; t != fire; ++t) link.service(s, t);
    TEST_ASSERT_EQUAL_UINT32(0, link.sent.size());
    link.service(s, fire);
    link.service(s, fire + 1);
    TEST_ASSERT_EQUAL_UINT32(1, link.sent.size());
    TEST_ASSERT_EQUAL_UINT32(fire, link.sent[0].atMs);
  }
}

// During a known loop stall nothing is queued: enqueue fails open so the
// caller relays inline. A copy queued before the stall still goes out once
// after it, and enqueue queues again once the stall ends.
void test_known_stall_fails_open() {
  RelayStagger<kBuf, kSlots> s;
  beginAs(s, 11);
  Link link;
  const auto before = frameFor(20);
  TEST_ASSERT_TRUE(s.enqueue(before.data(), before.size(), kType, kWisp, 20, 6000));
  // Asserts wait until the guard is gone: a failed assert unwinds without
  // its destructor and would leave the flag set for later tests.
  bool flagged = false;
  uint32_t queuedInStall = 0;
  {
    const lamp::KnownLoopStall stall;
    flagged = lamp::inKnownLoopStall();
    for (uint16_t seq = 21; seq < 21 + kSlots + 2; ++seq) {
      const auto f = frameFor(seq);
      if (s.enqueue(f.data(), f.size(), kType, kWisp, seq, 6000)) ++queuedInStall;
    }
  }
  TEST_ASSERT_TRUE(flagged);
  TEST_ASSERT_EQUAL_UINT32(0, queuedInStall);
  TEST_ASSERT_FALSE(lamp::inKnownLoopStall());
  TEST_ASSERT_EQUAL_UINT32(kSlots + 2, s.window().failOpen);
  const auto after = frameFor(40);
  TEST_ASSERT_TRUE(s.enqueue(after.data(), after.size(), kType, kWisp, 40, 6000));
  for (uint32_t t = 6000; t <= 6000 + kW + 10; ++t) link.service(s, t);
  TEST_ASSERT_EQUAL_UINT32(2, link.sent.size());
  TEST_ASSERT_EQUAL_UINT32(2, s.window().queued);
  TEST_ASSERT_EQUAL_UINT32(0, s.window().dropped);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_exactly_once);
  RUN_TEST(test_never_early_and_by_deadline);
  RUN_TEST(test_fails_open);
  RUN_TEST(test_not_ready_defers);
  RUN_TEST(test_driver_error_retries);
  RUN_TEST(test_late_cap);
  RUN_TEST(test_desync_across_lamps);
  RUN_TEST(test_varies_across_frames);
  RUN_TEST(test_pair_offset_varies);
  RUN_TEST(test_bounds);
  RUN_TEST(test_wrap);
  RUN_TEST(test_known_stall_fails_open);
  return UNITY_END();
}
