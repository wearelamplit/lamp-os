// Native-host tests for the real lamp::ResendRing (mesh_link's spaced
// re-broadcast). MeshLink itself is Arduino/ESP-NOW coupled and not natively
// buildable, but ResendRing is a native-safe template, so these exercise the
// production ring directly. buildControlOp/parseControlOp/DedupRing are the
// real production types (lamp_protocol.hpp is native-safe) so the dedup-collapse
// case runs the actual receive-side dedup, not a mirror of it.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "components/network/mesh/resend_ring.hpp"
#include "components/network/protocol/lamp_protocol.hpp"

namespace lp = lamp_protocol;
using lamp::ResendRing;

void setUp(void) {}
void tearDown(void) {}

namespace {

constexpr uint8_t kResends = 2;
constexpr uint32_t kGapMs = 40;

const uint8_t kSrc[6]    = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
const uint8_t kBcast[6]  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const uint8_t kPayload[] = {'{', '}'};

// Records every frame service() re-emits.
struct Capture {
  std::vector<std::vector<uint8_t>> frames;
  void operator()(const uint8_t* buf, size_t len) {
    frames.emplace_back(buf, buf + len);
  }
};

}  // namespace

// A single-slot ring re-emits exactly kResends copies of the same frame, one
// per service() call once the gap has elapsed, never bunched in one call.
void test_single_slot_emits_n_copies_one_per_service() {
  ResendRing<lp::CONTROL_MAX_SIZE, 1> ring;
  uint8_t frame[lp::CONTROL_MAX_SIZE];
  const size_t n = lp::buildControlOp(frame, sizeof(frame), /*seq=*/7,
                                      kBcast, kSrc, kPayload, sizeof(kPayload));
  TEST_ASSERT_TRUE(n > 0);
  ring.enqueue(frame, n, /*now=*/1000, kResends, kGapMs);

  Capture cap;
  for (uint32_t now = 1010; now <= 1200; now += 10) {
    ring.service(now, cap);
  }
  TEST_ASSERT_EQUAL_INT(kResends, (int)cap.frames.size());
  for (const auto& f : cap.frames) {
    lp::ParsedControlOp parsed;
    TEST_ASSERT_TRUE(lp::parseControlOp(f.data(), f.size(), parsed));
    TEST_ASSERT_EQUAL_UINT16(7, parsed.seq);
  }
}

// service() fires at most one copy per slot per call, even called long after
// both gaps elapsed, so copies straddle loop iterations and never bunch up.
void test_single_slot_at_most_one_per_service() {
  ResendRing<lp::CONTROL_MAX_SIZE, 1> ring;
  uint8_t frame[lp::CONTROL_MAX_SIZE];
  const size_t n = lp::buildControlOp(frame, sizeof(frame), 3,
                                      kBcast, kSrc, kPayload, sizeof(kPayload));
  ring.enqueue(frame, n, 0, kResends, kGapMs);

  Capture cap;
  ring.service(100000, cap);  // jump way past both gaps
  TEST_ASSERT_EQUAL_INT(1, (int)cap.frames.size());
}

// A fresh enqueue on a full size-1 ring overwrites the slot: later copies carry
// the new seq, the superseded frame never resurfaces.
void test_enqueue_overwrites_size_one_slot() {
  ResendRing<lp::CONTROL_MAX_SIZE, 1> ring;
  uint8_t a[lp::CONTROL_MAX_SIZE];
  const size_t na = lp::buildControlOp(a, sizeof(a), 1, kBcast, kSrc,
                                       kPayload, sizeof(kPayload));
  ring.enqueue(a, na, 0, kResends, kGapMs);

  Capture cap;
  ring.service(kGapMs, cap);  // one copy of A

  uint8_t b[lp::CONTROL_MAX_SIZE];
  const size_t nb = lp::buildControlOp(b, sizeof(b), 2, kBcast, kSrc,
                                       kPayload, sizeof(kPayload));
  ring.enqueue(b, nb, kGapMs, kResends, kGapMs);

  for (uint32_t now = 2 * kGapMs; now <= 6 * kGapMs; now += kGapMs) {
    ring.service(now, cap);
  }
  for (size_t i = 1; i < cap.frames.size(); ++i) {
    lp::ParsedControlOp parsed;
    TEST_ASSERT_TRUE(lp::parseControlOp(cap.frames[i].data(),
                                        cap.frames[i].size(), parsed));
    TEST_ASSERT_EQUAL_UINT16(2, parsed.seq);
  }
}

// A frame larger than the slot buffer is rejected; the caller's single send
// stands, no resends.
void test_oversize_frame_rejected() {
  ResendRing<8, 1> ring;
  uint8_t frame[16] = {0};
  TEST_ASSERT_FALSE(ring.enqueue(frame, sizeof(frame), 0, kResends, kGapMs));
  Capture cap;
  ring.service(1000, cap);
  TEST_ASSERT_EQUAL_INT(0, (int)cap.frames.size());
}

// A fan-out: several distinct frames enqueued at once each keep their own slot
// and all replay on the due tick (one wave of copies).
void test_fanout_all_slots_replay() {
  ResendRing<lp::CONTROL_MAX_SIZE, 4> ring;
  for (uint16_t i = 0; i < 4; ++i) {
    uint8_t frame[lp::CONTROL_MAX_SIZE];
    const size_t n = lp::buildControlOp(frame, sizeof(frame), i, kBcast, kSrc,
                                        kPayload, sizeof(kPayload));
    ring.enqueue(frame, n, 0, /*resends=*/1, kGapMs);
  }
  Capture cap;
  ring.service(kGapMs, cap);
  TEST_ASSERT_EQUAL_INT(4, (int)cap.frames.size());
}

// The initial send + N resends are identical frames (same seq) at the receiver;
// the production DedupRing collapses them to exactly one apply.
void test_resend_copies_dedup_to_one_apply() {
  lp::DedupRing<32> dedup;
  uint8_t frame[lp::CONTROL_MAX_SIZE];
  const size_t n = lp::buildControlOp(frame, sizeof(frame), 42, kBcast, kSrc,
                                      kPayload, sizeof(kPayload));
  int applyCount = 0;
  for (int copy = 0; copy < 1 + kResends; ++copy) {
    lp::ParsedControlOp parsed;
    TEST_ASSERT_TRUE(lp::parseControlOp(frame, n, parsed));
    if (dedup.record(parsed.sourceMac, lp::MSG_CONTROL_OP, parsed.seq)) {
      ++applyCount;
    }
  }
  TEST_ASSERT_EQUAL_INT(1, applyCount);
}

// The command resend ring covers a 384 B frame (358 B payload budget) and
// rejects anything past it, so a rich cascade payload replays instead of
// falling back to a single send.
void test_command_ring_covers_384_rejects_385() {
  constexpr size_t kBudget =
      lp::COMMAND_FIXED_SIZE + 358 + lp::COMMAND_TAG_SIZE;
  TEST_ASSERT_EQUAL_INT(384, (int)kBudget);
  ResendRing<kBudget, 10> ring;
  uint8_t frame[kBudget + 1] = {0};
  TEST_ASSERT_TRUE(ring.enqueue(frame, kBudget, 0, kResends, kGapMs));
  TEST_ASSERT_FALSE(ring.enqueue(frame, kBudget + 1, 0, kResends, kGapMs));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_single_slot_emits_n_copies_one_per_service);
  RUN_TEST(test_single_slot_at_most_one_per_service);
  RUN_TEST(test_enqueue_overwrites_size_one_slot);
  RUN_TEST(test_oversize_frame_rejected);
  RUN_TEST(test_fanout_all_slots_replay);
  RUN_TEST(test_resend_copies_dedup_to_one_apply);
  RUN_TEST(test_command_ring_covers_384_rejects_385);
  return UNITY_END();
}
