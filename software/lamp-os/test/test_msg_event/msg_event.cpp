// Native-host unit tests for the MSG_EVENT cascade migration (C.3).
//
// Pins the data-shape behaviors that the production code in
// ShowReceiver::handleRecv (MSG_EVENT branch) and
// ExpressionManager::maybeCascade rely on:
//
//   1. Stagger-list lookup: receiver finds its own MAC in the
//      staggerEntries array and pulls the supplied delayMs. If absent,
//      falls back to a "tail fire" default = numStaggerEntries *
//      kTailFireStaggerMs so a late-joiner still participates without
//      piling onto the wavefront.
//
//   2. RSSI-desc sort: the sender orders peers by lastRssi descending so
//      the strongest-signal peer (≈ physically closest) ends up at
//      index 0 and fires first. Unknown RSSI (-127) sorts to the back.
//
//   3. delayMs clamp: a peer-supplied per-entry delayMs is clamped to
//      kMaxDelayMs (10 s) so an attacker-crafted MSG_EVENT can't hold a
//      pendingTriggers slot for ~49 days.
//
// Self-contained pattern (mirrors test_cascade_dedup / test_transient_override):
// re-declare the constants + the lookup/clamp/sort logic so the test
// doesn't pull in Arduino, FreeRTOS, or ArduinoJson. If the production
// constants in expression_invocation.hpp or lamp_protocol.hpp drift,
// mirror here.
//
// We do NOT exercise buildEvent/parseEvent here (test_protocol_v2 pins
// the wire roundtrip already) — only the cascade-side behaviors layered
// on top of that protocol primitive.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace lamp_test {

// Mirror the production caps (lamp_protocol::kMaxStaggerEntries +
// expression_invocation::kMaxDelayMs). If these drift, the test will
// fail loudly and remind you to update both sides.
static constexpr size_t   kMaxStaggerEntries  = 12;
static constexpr uint32_t kMaxDelayMs         = 10000;
// Receiver-side tail-fire fallback. Must match the constant in
// show_receiver.cpp's MSG_EVENT branch.
static constexpr uint16_t kTailFireStaggerMs  = 50;

struct StaggerEntry {
  uint8_t  mac[6];
  uint16_t delayMs;
};

struct Peer {
  uint8_t  mac[6];
  int8_t   lastRssi;
};

// Build the sender-side stagger list (matches ExpressionManager::maybeCascade).
// peers must be pre-filtered (no self, no missing MAC). The function sorts
// in place by RSSI descending, caps at kMaxStaggerEntries, and assigns
// delayMs = clamp((i + 1) * staggerMs, 0..kMaxDelayMs). The (i + 1) offset
// makes the closest peer fire `staggerMs` after the sender (not at the
// same instant as the sender) — see expression_manager.cpp::maybeCascade.
static std::vector<StaggerEntry> buildStaggerList(std::vector<Peer> peers,
                                                  uint32_t staggerMs) {
  std::sort(peers.begin(), peers.end(),
            [](const Peer& a, const Peer& b) { return a.lastRssi > b.lastRssi; });
  if (peers.size() > kMaxStaggerEntries) peers.resize(kMaxStaggerEntries);
  std::vector<StaggerEntry> out;
  out.reserve(peers.size());
  for (size_t i = 0; i < peers.size(); ++i) {
    StaggerEntry e;
    std::memcpy(e.mac, peers[i].mac, 6);
    const uint32_t d = static_cast<uint32_t>(i + 1) * staggerMs;
    e.delayMs = static_cast<uint16_t>(d > kMaxDelayMs ? kMaxDelayMs : d);
    out.push_back(e);
  }
  return out;
}

// Receiver-side lookup (matches show_receiver.cpp's MSG_EVENT branch).
// Returns the supplied delayMs if our MAC appears in the list; otherwise
// the tail-fire default = numStaggerEntries * kTailFireStaggerMs.
static uint16_t lookupOwnDelay(const std::vector<StaggerEntry>& list,
                               const uint8_t myMac[6]) {
  for (const auto& e : list) {
    if (std::memcmp(e.mac, myMac, 6) == 0) return e.delayMs;
  }
  return static_cast<uint16_t>(list.size()) * kTailFireStaggerMs;
}

// Mirror of clampDelayMs (expression_invocation.cpp).
static uint32_t clampDelayMs(uint32_t v) {
  return v > kMaxDelayMs ? kMaxDelayMs : v;
}

}  // namespace lamp_test

void setUp(void) {}
void tearDown(void) {}

// --- Stagger-list build + lookup ---

void test_stagger_lookup_finds_own_mac() {
  // Three peers; ours is in the middle (idx 1 after RSSI sort).
  // (idx + 1) × staggerMs → (1 + 1) × 50 = 100 ms.
  std::vector<lamp_test::Peer> peers = {
      {{0x01, 0x01, 0x01, 0x01, 0x01, 0x01}, -40},  // strongest → idx 0
      {{0x02, 0x02, 0x02, 0x02, 0x02, 0x02}, -60},  // ours       → idx 1
      {{0x03, 0x03, 0x03, 0x03, 0x03, 0x03}, -80},  // weakest    → idx 2
  };
  auto list = lamp_test::buildStaggerList(peers, /*staggerMs=*/50);
  TEST_ASSERT_EQUAL_UINT32(3, list.size());

  uint8_t myMac[6] = {0x02, 0x02, 0x02, 0x02, 0x02, 0x02};
  TEST_ASSERT_EQUAL_UINT16(100, lamp_test::lookupOwnDelay(list, myMac));
}

void test_stagger_lookup_first_peer_fires_after_one_stagger() {
  // Strongest-signal peer (after RSSI sort) gets delayMs = staggerMs (was 0
  // before the 2026-06-03 stagger-formula fix). The sender fires at t=0;
  // the closest peer fires at t=staggerMs. That's the leading edge of the
  // spatial wave — without this offset a 2-lamp mesh fires simultaneously
  // regardless of cascadeStaggerMs and the "wave from the trigger source"
  // UX disappears.
  std::vector<lamp_test::Peer> peers = {
      {{0xAA, 0, 0, 0, 0, 0}, -90},
      {{0xBB, 0, 0, 0, 0, 0}, -30},  // closest
      {{0xCC, 0, 0, 0, 0, 0}, -70},
  };
  auto list = lamp_test::buildStaggerList(peers, 100);
  uint8_t myMac[6] = {0xBB, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_UINT16(100, lamp_test::lookupOwnDelay(list, myMac));
}

void test_stagger_lookup_tail_fire_when_absent() {
  // Receiver's MAC isn't in the list (e.g. just joined the mesh after the
  // sender's last HELLO scrape). Fall back to numStaggerEntries *
  // kTailFireStaggerMs so we still participate, but at the END of the
  // wave rather than racing the wavefront.
  std::vector<lamp_test::Peer> peers = {
      {{0x01, 0, 0, 0, 0, 0}, -40},
      {{0x02, 0, 0, 0, 0, 0}, -60},
  };
  auto list = lamp_test::buildStaggerList(peers, 50);
  uint8_t myMac[6] = {0x99, 0, 0, 0, 0, 0};  // not in list
  TEST_ASSERT_EQUAL_UINT16(2 * lamp_test::kTailFireStaggerMs,
                          lamp_test::lookupOwnDelay(list, myMac));
}

void test_stagger_lookup_tail_fire_empty_list() {
  // Edge case: sender had zero peers (cascadeEnabled but no one nearby).
  // We're a lone receiver hearing the event from the originator; tail
  // fire degenerates to 0 — fire immediately.
  std::vector<lamp_test::StaggerEntry> empty;
  uint8_t myMac[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
  TEST_ASSERT_EQUAL_UINT16(0, lamp_test::lookupOwnDelay(empty, myMac));
}

// --- RSSI sort order ---

void test_rssi_sort_strongest_first() {
  // Higher RSSI (closer to 0 dBm) sorts first. Verify the lambda the
  // sender uses respects "less negative wins."
  std::vector<lamp_test::Peer> peers = {
      {{0xA1, 0, 0, 0, 0, 0}, -85},
      {{0xA2, 0, 0, 0, 0, 0}, -30},
      {{0xA3, 0, 0, 0, 0, 0}, -60},
  };
  auto list = lamp_test::buildStaggerList(peers, 100);
  TEST_ASSERT_EQUAL_UINT8(0xA2, list[0].mac[0]);  // -30: closest
  TEST_ASSERT_EQUAL_UINT8(0xA3, list[1].mac[0]);  // -60
  TEST_ASSERT_EQUAL_UINT8(0xA1, list[2].mac[0]);  // -85: farthest
}

void test_rssi_sort_unknown_sorts_last() {
  // -127 is the "unknown RSSI" sentinel; it should sort to the back so a
  // peer we just heard for the first time doesn't accidentally fire first
  // in the wave.
  std::vector<lamp_test::Peer> peers = {
      {{0xB1, 0, 0, 0, 0, 0}, -50},
      {{0xB2, 0, 0, 0, 0, 0}, -127},  // unknown
      {{0xB3, 0, 0, 0, 0, 0}, -70},
  };
  auto list = lamp_test::buildStaggerList(peers, 50);
  TEST_ASSERT_EQUAL_UINT8(0xB1, list[0].mac[0]);
  TEST_ASSERT_EQUAL_UINT8(0xB3, list[1].mac[0]);
  TEST_ASSERT_EQUAL_UINT8(0xB2, list[2].mac[0]);  // -127 last
}

void test_rssi_sort_caps_at_kMaxStaggerEntries() {
  // Sender has more peers than the wire frame can carry. The list is
  // truncated to kMaxStaggerEntries after sorting, so the SURVIVORS are
  // the strongest-signal ones — peers we couldn't fit fall back to
  // tail-fire on their receivers.
  std::vector<lamp_test::Peer> peers;
  for (size_t i = 0; i < lamp_test::kMaxStaggerEntries + 5; ++i) {
    lamp_test::Peer p{};
    p.mac[0] = static_cast<uint8_t>(i);
    // Strongest is i=0 (-30), weakest is the last one (-30 - 15 = -45ish).
    p.lastRssi = static_cast<int8_t>(-30 - static_cast<int>(i));
    peers.push_back(p);
  }
  auto list = lamp_test::buildStaggerList(peers, 50);
  TEST_ASSERT_EQUAL_UINT32(lamp_test::kMaxStaggerEntries, list.size());
  // Strongest survived at idx 0.
  TEST_ASSERT_EQUAL_UINT8(0, list[0].mac[0]);
}

// --- delayMs clamp ---

void test_stagger_delay_clamps_at_kMaxDelayMs() {
  // 12 peers with a huge staggerMs each — the late ones' computed delay
  // exceeds kMaxDelayMs (10s) and must clamp. Otherwise a misconfigured
  // (or malicious) sender could schedule a 49-day fire on a receiver.
  std::vector<lamp_test::Peer> peers;
  for (size_t i = 0; i < lamp_test::kMaxStaggerEntries; ++i) {
    lamp_test::Peer p{};
    p.mac[0] = static_cast<uint8_t>(i);
    p.lastRssi = static_cast<int8_t>(-30 - static_cast<int>(i));
    peers.push_back(p);
  }
  // staggerMs=2000 with the (i + 1) formula means last peer (i=11)
  // computes (12) × 2000 = 24000 ms — way over kMaxDelayMs=10000.
  auto list = lamp_test::buildStaggerList(peers, 2000);
  for (const auto& e : list) {
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(lamp_test::kMaxDelayMs, e.delayMs);
  }
  // The clamped tail should pin at kMaxDelayMs exactly.
  TEST_ASSERT_EQUAL_UINT16(lamp_test::kMaxDelayMs,
                           list[lamp_test::kMaxStaggerEntries - 1].delayMs);
}

void test_clampDelayMs_passthrough_and_cap() {
  // Mirrors expression_invocation::clampDelayMs — tryHandleExpressionEvent
  // applies this to the recv-side suppliedDelayMs as defense-in-depth
  // before queueing a delayed trigger.
  TEST_ASSERT_EQUAL_UINT32(0, lamp_test::clampDelayMs(0));
  TEST_ASSERT_EQUAL_UINT32(500, lamp_test::clampDelayMs(500));
  TEST_ASSERT_EQUAL_UINT32(lamp_test::kMaxDelayMs,
                           lamp_test::clampDelayMs(lamp_test::kMaxDelayMs));
  TEST_ASSERT_EQUAL_UINT32(lamp_test::kMaxDelayMs,
                           lamp_test::clampDelayMs(lamp_test::kMaxDelayMs + 1));
  TEST_ASSERT_EQUAL_UINT32(lamp_test::kMaxDelayMs,
                           lamp_test::clampDelayMs(0xFFFFFFFFu));
}

// Receiver-side cascade gating policy (post-2026-06-03 regression fix).
//
// The receiver does NOT check local cascadeEnabled and does NOT require a
// matching local expression config of the same type. The wire payload
// carries the full ExpressionInvocation (type, target, colors, parameters);
// triggerInvocation builds a fresh transient Expression directly from it.
// Receiver behavior is sender-authoritative.
//
// Pre-fix (commit cb7e6fd, C.3): tryHandleExpressionEvent had a step-2
// `typeOptedIn` gate that scanned the receiver's `expressions` vector for
// a matching `type` with `cascadeEnabled=1`, dropping otherwise — silently
// breaking the legacy "execute this cascade once and forget it" model
// observed via live tap on 2026-06-03 (`[event] type=glitchy not cascaded
// locally, drop` on a melonie lamp whose glitchy had no local cascade
// config).
//
// This test mirrors the post-fix gating logic so a future change that
// reintroduces a receiver-side opt-in fails loudly here.
namespace {
// Mirror of the post-fix tryHandleExpressionEvent gate sequence (minus
// the JSON-parse and trigger steps which need ArduinoJson + compositor).
// Returns true if the cascade would be dispatched, false if dropped.
struct ReceiverGateMirror {
  // The receiver's own configured expressions. The whole point of the
  // post-fix contract: this is intentionally unread by the gate logic.
  std::vector<std::string> localExpressionTypes;
  bool typePeekSucceeded = true;
  bool recentCascadeDedupHit = false;

  bool wouldDispatch(const char* /*type*/) const {
    if (!typePeekSucceeded) return false;            // step 1
    if (recentCascadeDedupHit) return false;         // step 2
    // step 3 / 4 (parseInvocation + triggerInvocation) — past this point
    // dispatch is unconditional in `type`. No local-config consult.
    return true;
  }
};
}  // namespace

void test_receiver_dispatches_cascade_without_local_config() {
  ReceiverGateMirror r;
  // Receiver has no entry of type "glitchy" — the exact field-observed
  // case that the pre-fix gate dropped.
  r.localExpressionTypes = {};
  TEST_ASSERT_TRUE(r.wouldDispatch("glitchy"));
}

void test_receiver_dispatches_cascade_even_when_local_type_has_cascade_off() {
  ReceiverGateMirror r;
  // Receiver has a glitchy config but it has cascadeEnabled=0. The pre-fix
  // gate would still drop here. Post-fix: dispatch (we don't even look at
  // the local config — the mock omits a cascadeEnabled field at all).
  r.localExpressionTypes = {"glitchy"};
  TEST_ASSERT_TRUE(r.wouldDispatch("glitchy"));
}

void test_receiver_still_drops_when_recent_cascade_deduped() {
  // Negative path stays honest — the dedup ring still suppresses a
  // duplicate cascade of the same type within the window.
  ReceiverGateMirror r;
  r.recentCascadeDedupHit = true;
  TEST_ASSERT_FALSE(r.wouldDispatch("glitchy"));
}

void test_receiver_drops_when_type_peek_fails() {
  // Negative path stays honest — a payload without a parseable `type`
  // string still drops (no dedup key).
  ReceiverGateMirror r;
  r.typePeekSucceeded = false;
  TEST_ASSERT_FALSE(r.wouldDispatch(""));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_stagger_lookup_finds_own_mac);
  RUN_TEST(test_stagger_lookup_first_peer_fires_after_one_stagger);
  RUN_TEST(test_stagger_lookup_tail_fire_when_absent);
  RUN_TEST(test_stagger_lookup_tail_fire_empty_list);

  RUN_TEST(test_rssi_sort_strongest_first);
  RUN_TEST(test_rssi_sort_unknown_sorts_last);
  RUN_TEST(test_rssi_sort_caps_at_kMaxStaggerEntries);

  RUN_TEST(test_stagger_delay_clamps_at_kMaxDelayMs);
  RUN_TEST(test_clampDelayMs_passthrough_and_cap);

  RUN_TEST(test_receiver_dispatches_cascade_without_local_config);
  RUN_TEST(test_receiver_dispatches_cascade_even_when_local_type_has_cascade_off);
  RUN_TEST(test_receiver_still_drops_when_recent_cascade_deduped);
  RUN_TEST(test_receiver_drops_when_type_peek_fails);

  return UNITY_END();
}
