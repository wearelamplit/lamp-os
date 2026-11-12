// Native tests for the cascade fan-out drain: every peer is reached, at most a
// batch is queued per pass, each frame's delayMs keeps the peer's intended
// fire time, and the frames are byte-identical to serializeInvocation.

#include <unity.h>

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#include "expressions/cascade_scheduler.hpp"
#include "expressions/expression_invocation.hpp"

#include "../../src/util/color.cpp"
#include "../../src/expressions/expression_invocation.cpp"
#include "../../src/expressions/cascade_scheduler.cpp"

using namespace lamp;

void setUp(void) {}
void tearDown(void) {}

namespace {

struct Sent {
  uint8_t peer;
  std::string payload;
};

ExpressionInvocation sampleInvocation() {
  ExpressionInvocation inv;
  inv.type = "glitchy";
  inv.target = 3;
  inv.colors = {Color(1, 2, 3, 4)};
  inv.parameters = {{"durationMin", 250}, {"durationMax", 900}};
  return inv;
}

void beginWith(CascadeScheduler& s, const ExpressionInvocation& inv, size_t peers,
               uint32_t staggerMs, uint32_t nowMs) {
  std::string json;
  serializeInvocation(inv, json);
  const auto queued = s.begin(json.data(), json.size(), staggerMs, nowMs);
  TEST_ASSERT_TRUE(queued == CascadeScheduler::Begin::Queued ||
                   queued == CascadeScheduler::Begin::Replaced);
  for (size_t i = 0; i < peers; ++i) {
    const uint8_t mac[6] = {0x02, 0, 0, 0, 0, static_cast<uint8_t>(i)};
    s.addPeer(mac);
  }
}

size_t drain(CascadeScheduler& s, uint32_t nowMs, std::vector<Sent>& out,
             size_t room = SIZE_MAX) {
  return s.tick(nowMs, [&](const uint8_t* mac, const char* payload, size_t len) {
    if (room == 0) return false;
    --room;
    out.push_back({mac[5], std::string(payload, len)});
    return true;
  });
}

}  // namespace

void test_delay_keeps_the_intended_fire_time() {
  TEST_ASSERT_EQUAL_UINT32(100, CascadeScheduler::delayFor(0, 100, 0));
  TEST_ASSERT_EQUAL_UINT32(500, CascadeScheduler::delayFor(4, 100, 0));
  TEST_ASSERT_EQUAL_UINT32(450, CascadeScheduler::delayFor(4, 100, 50));
  TEST_ASSERT_EQUAL_UINT32(kMaxDelayMs, CascadeScheduler::delayFor(20, 100, 0));
  TEST_ASSERT_EQUAL_UINT32(kMaxDelayMs - 50, CascadeScheduler::delayFor(20, 100, 50));
  TEST_ASSERT_EQUAL_UINT32(0, CascadeScheduler::delayFor(0, 100, 150));
  TEST_ASSERT_EQUAL_UINT32(0, CascadeScheduler::delayFor(3, 0, 10));
  TEST_ASSERT_EQUAL_UINT32(kMaxDelayMs, CascadeScheduler::delayFor(49, 0xFFFFFFFFu, 0));
}

// 50 peers drain in batches of kBatchSize, one batch per gap, every peer once, closest
// first, each frame what serializeInvocation writes for its remaining delay.
void test_every_peer_is_reached_in_bounded_batches() {
  CascadeScheduler s;
  const ExpressionInvocation inv = sampleInvocation();
  const uint32_t start = 1000;
  const uint32_t stagger = 40;
  beginWith(s, inv, 50, stagger, start);
  std::vector<Sent> sent;
  uint32_t now = start;
  size_t batches = 0;
  while (s.active()) {
    const size_t n = drain(s, now, sent);
    TEST_ASSERT_TRUE(n <= CascadeScheduler::kBatchSize);
    if (n > 0) ++batches;
    TEST_ASSERT_EQUAL(0, drain(s, now + CascadeScheduler::kBatchGapMs - 1, sent));
    now += CascadeScheduler::kBatchGapMs;
  }
  TEST_ASSERT_EQUAL(50, sent.size());
  TEST_ASSERT_EQUAL((50 + CascadeScheduler::kBatchSize - 1) / CascadeScheduler::kBatchSize,
                    batches);
  for (size_t i = 0; i < sent.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT8(i, sent[i].peer);
    const uint32_t elapsed = static_cast<uint32_t>(i / CascadeScheduler::kBatchSize) *
                             CascadeScheduler::kBatchGapMs;
    ExpressionInvocation expected = inv;
    expected.delayMs = CascadeScheduler::delayFor(i, stagger, elapsed);
    std::string json;
    serializeInvocation(expected, json);
    TEST_ASSERT_EQUAL_STRING(json.c_str(), sent[i].payload.c_str());
  }
}

// A second cascade waits behind the one draining and still reaches every
// peer; a third replaces the one waiting, never the one draining.
void test_cascades_queue_behind_the_drain() {
  CascadeScheduler s;
  beginWith(s, sampleInvocation(), 20, 0, 0);
  std::vector<Sent> sent;
  drain(s, 0, sent);
  const size_t left = 20 - CascadeScheduler::kBatchSize;
  TEST_ASSERT_EQUAL(left, s.pending());
  beginWith(s, sampleInvocation(), 3, 0, 10);
  TEST_ASSERT_EQUAL(left + 3, s.pending());
  std::string json;
  serializeInvocation(sampleInvocation(), json);
  TEST_ASSERT_TRUE(s.begin(json.data(), json.size(), 0, 20) ==
                   CascadeScheduler::Begin::Replaced);
  for (int i = 0; i < 5; ++i) {
    const uint8_t mac[6] = {0x03, 0, 0, 0, 0, static_cast<uint8_t>(i)};
    s.addPeer(mac);
  }
  TEST_ASSERT_EQUAL(left + 5, s.pending());
  for (uint32_t t = 100; s.active(); t += CascadeScheduler::kBatchGapMs) drain(s, t, sent);
  TEST_ASSERT_EQUAL(25, sent.size());
}

// Two fan-outs queued, the first ending on a full batch: the second's first
// batch still waits the gap, so its frames never overwrite the last batch's
// pending resends.
void test_the_gap_holds_between_fan_outs() {
  CascadeScheduler s;
  beginWith(s, sampleInvocation(), CascadeScheduler::kBatchSize, 0, 0);
  beginWith(s, sampleInvocation(), 3, 0, 0);
  std::vector<Sent> sent;
  TEST_ASSERT_EQUAL(CascadeScheduler::kBatchSize, drain(s, 0, sent));
  TEST_ASSERT_EQUAL(3, s.pending());
  TEST_ASSERT_EQUAL(0, drain(s, CascadeScheduler::kBatchGapMs - 1, sent));
  TEST_ASSERT_EQUAL(3, drain(s, CascadeScheduler::kBatchGapMs, sent));
}

// A frame the driver cannot take now is retried on a later pass, not lost.
void test_refused_send_is_retried() {
  CascadeScheduler s;
  beginWith(s, sampleInvocation(), 10, 0, 0);
  std::vector<Sent> sent;
  TEST_ASSERT_EQUAL(3, drain(s, 0, sent, 3));
  TEST_ASSERT_EQUAL(7, s.pending());
  TEST_ASSERT_EQUAL(0, drain(s, 10, sent));
  drain(s, CascadeScheduler::kBatchGapMs, sent);
  drain(s, 2 * CascadeScheduler::kBatchGapMs, sent);
  TEST_ASSERT_EQUAL(10, sent.size());
  for (size_t i = 0; i < sent.size(); ++i) TEST_ASSERT_EQUAL_UINT8(i, sent[i].peer);
}

// The loop's form: frames go out only while the driver reports room, and a
// frame held back is the next one sent.
void test_gated_tick_holds_frames_while_the_driver_is_full() {
  CascadeScheduler s;
  beginWith(s, sampleInvocation(), 5, 0, 0);
  std::vector<Sent> sent;
  size_t room = 2;
  auto ready = [&] { return room > 0; };
  auto send = [&](const uint8_t* mac, const char* payload, size_t len) {
    --room;
    sent.push_back({mac[5], std::string(payload, len)});
  };
  TEST_ASSERT_EQUAL(2, s.tickGated(0, ready, send));
  TEST_ASSERT_EQUAL(3, s.pending());
  room = 0;
  TEST_ASSERT_EQUAL(0, s.tickGated(CascadeScheduler::kBatchGapMs, ready, send));
  room = 8;
  TEST_ASSERT_EQUAL(3, s.tickGated(2 * CascadeScheduler::kBatchGapMs, ready, send));
  for (size_t i = 0; i < sent.size(); ++i) TEST_ASSERT_EQUAL_UINT8(i, sent[i].peer);
}

void test_cancel_stops_the_drain() {
  CascadeScheduler s;
  beginWith(s, sampleInvocation(), 20, 0, 0);
  s.cancel();
  std::vector<Sent> sent;
  TEST_ASSERT_EQUAL(0, drain(s, 0, sent));
}

void test_oversize_payload_is_refused() {
  CascadeScheduler s;
  std::string json = "{\"delayMs\":0,\"pad\":\"" +
                     std::string(CascadeScheduler::kMaxPayloadLen, 'x') + "\"}";
  TEST_ASSERT_TRUE(s.begin(json.data(), json.size(), 0, 0) ==
                   CascadeScheduler::Begin::TooLong);
  const char noDelay[] = "{\"type\":\"pulse\"}";
  TEST_ASSERT_TRUE(s.begin(noDelay, sizeof(noDelay) - 1, 0, 0) ==
                   CascadeScheduler::Begin::NoDelayField);
  TEST_ASSERT_FALSE(s.active());
  TEST_ASSERT_FALSE(s.active());
}

// The payload buffer holds the longest invocation the caps allow, with the
// widest delay.
void test_worst_case_invocation_fits() {
  ExpressionInvocation inv;
  inv.type = std::string(kMaxExpressionTypeLen, 't');
  inv.target = 3;
  inv.delayMs = kMaxDelayMs;
  inv.colors.assign(kMaxExpressionColors, Color(255, 255, 255, 255));
  std::vector<size_t> byLength;
  for (size_t i = 0; i < kExpressionParamKeyCount; ++i) byLength.push_back(i);
  std::sort(byLength.begin(), byLength.end(), [](size_t a, size_t b) {
    return std::strlen(kExpressionParamKeys[a]) > std::strlen(kExpressionParamKeys[b]);
  });
  for (size_t i = 0; i < kMaxExpressionParams; ++i) {
    inv.parameters.setIndex(byLength[i], 0xFFFFFFFFu);
  }
  std::string json;
  serializeInvocation(inv, json);
  printf("[cascade] worst-case invocation %zu of %zu bytes\n", json.size(),
         CascadeScheduler::kMaxPayloadLen);
  TEST_ASSERT_TRUE(json.size() <= CascadeScheduler::kMaxPayloadLen);
}

// One fan-out sent, then nothing for a whole wrap: the next fan-out's first
// batch goes at once, not after the batch gap.
void test_batch_gap_does_not_hold_a_fan_out_a_wrap_later() {
  CascadeScheduler s;
  std::vector<Sent> out;
  const uint32_t sentMs = 1000;
  beginWith(s, sampleInvocation(), 1, 100, sentMs);
  TEST_ASSERT_EQUAL_size_t(1, drain(s, sentMs, out));
  for (uint64_t t = sentMs; t < sentMs + (1ull << 32); t += 1ull << 28) {
    s.retireBatchGap(static_cast<uint32_t>(t));
  }
  beginWith(s, sampleInvocation(), 1, 100, sentMs + 10);
  TEST_ASSERT_EQUAL_size_t(1, drain(s, sentMs + 10, out));
}

// Inside the gap retire keeps it.
void test_retire_keeps_a_running_batch_gap() {
  CascadeScheduler s;
  std::vector<Sent> out;
  beginWith(s, sampleInvocation(), 2 * CascadeScheduler::kBatchSize, 100, 1000);
  drain(s, 1000, out);
  s.retireBatchGap(1000 + CascadeScheduler::kBatchGapMs - 1);
  TEST_ASSERT_EQUAL_size_t(0, drain(s, 1000 + CascadeScheduler::kBatchGapMs - 1, out));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_delay_keeps_the_intended_fire_time);
  RUN_TEST(test_every_peer_is_reached_in_bounded_batches);
  RUN_TEST(test_cascades_queue_behind_the_drain);
  RUN_TEST(test_the_gap_holds_between_fan_outs);
  RUN_TEST(test_refused_send_is_retried);
  RUN_TEST(test_gated_tick_holds_frames_while_the_driver_is_full);
  RUN_TEST(test_cancel_stops_the_drain);
  RUN_TEST(test_oversize_payload_is_refused);
  RUN_TEST(test_worst_case_invocation_fits);
  RUN_TEST(test_batch_gap_does_not_hold_a_fan_out_a_wrap_later);
  RUN_TEST(test_retire_keeps_a_running_batch_gap);
  return UNITY_END();
}
