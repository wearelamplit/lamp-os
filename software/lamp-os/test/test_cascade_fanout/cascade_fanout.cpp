// test/test_cascade_fanout/cascade_fanout.cpp
// Native-host tests for the cascade-on-MSG_COMMAND reimplementation.
//
// Pins:
//   1. Fan-out delay math: N nearby lamps sorted by RSSI desc, each gets
//      (rank+1)*staggerMs as delayMs. No kMaxStaggerEntries cap (every
//      nearby lamp gets its own MSG_COMMAND).
//   2. cascadeEnabled gate: disabled → zero commands issued.
//   3. Loop-break: a lamp that receives a MSG_COMMAND runs the expression
//      with suppressCascade=true and does not enqueue a further cascade.
//   4. RSSI sort: strongest signal (closest) gets rank 0 → shortest delay.
//   5. delayMs clamp at kMaxDelayMs (10 s).

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace lamp_test {

static constexpr uint32_t kMaxDelayMs = 10000;

struct Peer {
    uint8_t mac[6];
    int8_t  lastRssi;
};

struct ScheduledCommand {
    uint8_t  mac[6];
    uint32_t delayMs;
};

// Mirror of the new maybeCascade fan-out logic (no cap, no stagger frame).
// peers is pre-filtered (no self, hasMac=true). Sorted by RSSI desc.
// Each peer i (0-based after sort) gets delayMs = clamp((i+1)*staggerMs, kMaxDelayMs).
static std::vector<ScheduledCommand> computeCascadeCommands(
        std::vector<Peer> peers, uint32_t staggerMs) {
    std::sort(peers.begin(), peers.end(), [](const Peer& a, const Peer& b) {
        return a.lastRssi > b.lastRssi;
    });
    std::vector<ScheduledCommand> out;
    out.reserve(peers.size());
    for (size_t i = 0; i < peers.size(); ++i) {
        ScheduledCommand cmd;
        std::memcpy(cmd.mac, peers[i].mac, 6);
        const uint32_t d = static_cast<uint32_t>(i + 1) * staggerMs;
        cmd.delayMs = d > kMaxDelayMs ? kMaxDelayMs : d;
        out.push_back(cmd);
    }
    return out;
}

// Mirror of the cascadeEnabled gate: if 0, no commands.
static std::vector<ScheduledCommand> computeCascadeCommandsGated(
        std::vector<Peer> peers, uint32_t staggerMs, uint32_t cascadeEnabled) {
    if (cascadeEnabled == 0) return {};
    return computeCascadeCommands(peers, staggerMs);
}

// Mirror of the loop-break invariant. A received MSG_COMMAND triggers
// triggerInvocation which sets suppressCascade=true before running the
// transient expression. The struct below models just that flag and the decision.
struct CascadeSuppressionModel {
    bool suppressCascade = false;
    bool cascadeEnabled  = true;

    bool wouldCascade() const {
        if (suppressCascade) return false;
        if (!cascadeEnabled) return false;
        return true;
    }
};

}  // namespace lamp_test

void setUp(void) {}
void tearDown(void) {}

// --- Fan-out delay math ---

void test_fanout_three_peers_correct_delays() {
    // 3 peers → 3 commands with delays 1×, 2×, 3× staggerMs.
    std::vector<lamp_test::Peer> peers = {
        {{0xAA, 0, 0, 0, 0, 0}, -60},
        {{0xBB, 0, 0, 0, 0, 0}, -30},  // strongest
        {{0xCC, 0, 0, 0, 0, 0}, -80},
    };
    auto cmds = lamp_test::computeCascadeCommands(peers, 200);
    TEST_ASSERT_EQUAL_UINT32(3, cmds.size());
    // After sort: BB(-30) idx0, AA(-60) idx1, CC(-80) idx2.
    TEST_ASSERT_EQUAL_UINT8(0xBB, cmds[0].mac[0]);
    TEST_ASSERT_EQUAL_UINT32(200,  cmds[0].delayMs);  // (0+1)*200
    TEST_ASSERT_EQUAL_UINT8(0xAA, cmds[1].mac[0]);
    TEST_ASSERT_EQUAL_UINT32(400,  cmds[1].delayMs);  // (1+1)*200
    TEST_ASSERT_EQUAL_UINT8(0xCC, cmds[2].mac[0]);
    TEST_ASSERT_EQUAL_UINT32(600,  cmds[2].delayMs);  // (2+1)*200
}

void test_fanout_one_peer_fires_after_stagger_not_zero() {
    // Sender fires at t=0; single closest peer fires at staggerMs,
    // NOT at t=0. This is the (rank+1) offset — a 2-lamp mesh must
    // produce a perceptible wave, not a simultaneous fire.
    std::vector<lamp_test::Peer> peers = {
        {{0xDD, 0, 0, 0, 0, 0}, -45},
    };
    auto cmds = lamp_test::computeCascadeCommands(peers, 800);
    TEST_ASSERT_EQUAL_UINT32(1, cmds.size());
    TEST_ASSERT_EQUAL_UINT32(800, cmds[0].delayMs);
}

void test_fanout_no_cap_15_peers() {
    // Cascade sends to every nearby lamp; no per-frame peer cap.
    std::vector<lamp_test::Peer> peers;
    for (size_t i = 0; i < 15; ++i) {
        lamp_test::Peer p{};
        p.mac[0] = static_cast<uint8_t>(i);
        p.lastRssi = static_cast<int8_t>(-30 - static_cast<int>(i));
        peers.push_back(p);
    }
    auto cmds = lamp_test::computeCascadeCommands(peers, 100);
    TEST_ASSERT_EQUAL_UINT32(15, cmds.size());
}

// --- cascadeEnabled gate ---

void test_cascade_disabled_issues_no_commands() {
    std::vector<lamp_test::Peer> peers = {
        {{0x01, 0, 0, 0, 0, 0}, -50},
        {{0x02, 0, 0, 0, 0, 0}, -60},
    };
    auto cmds = lamp_test::computeCascadeCommandsGated(peers, 200, /*cascadeEnabled=*/0);
    TEST_ASSERT_EQUAL_UINT32(0, cmds.size());
}

void test_cascade_enabled_issues_commands() {
    std::vector<lamp_test::Peer> peers = {
        {{0x01, 0, 0, 0, 0, 0}, -50},
    };
    auto cmds = lamp_test::computeCascadeCommandsGated(peers, 200, /*cascadeEnabled=*/1);
    TEST_ASSERT_EQUAL_UINT32(1, cmds.size());
}

// --- Loop-break ---

void test_received_command_does_not_cascade() {
    // Exercises CascadeSuppressionModel decision logic only.
    // The production invariant (suppressCascade_ set at fire time in
    // expression_manager.cpp) is not covered here.
    lamp_test::CascadeSuppressionModel m;
    m.suppressCascade = true;   // set by triggerInvocation on the receive path
    m.cascadeEnabled  = true;   // expression is configured to cascade
    TEST_ASSERT_FALSE(m.wouldCascade());
}

void test_local_trigger_with_cascade_enabled_does_cascade() {
    // Positive path: a locally-fired expression with cascadeEnabled and
    // suppressCascade=false DOES fan out. This is the maybeCascade trigger
    // path (not the receive path).
    lamp_test::CascadeSuppressionModel m;
    m.suppressCascade = false;
    m.cascadeEnabled  = true;
    TEST_ASSERT_TRUE(m.wouldCascade());
}

// --- RSSI sort ---

void test_rssi_sort_strongest_first() {
    std::vector<lamp_test::Peer> peers = {
        {{0xA1, 0, 0, 0, 0, 0}, -85},
        {{0xA2, 0, 0, 0, 0, 0}, -30},
        {{0xA3, 0, 0, 0, 0, 0}, -60},
    };
    auto cmds = lamp_test::computeCascadeCommands(peers, 100);
    TEST_ASSERT_EQUAL_UINT8(0xA2, cmds[0].mac[0]);
    TEST_ASSERT_EQUAL_UINT8(0xA3, cmds[1].mac[0]);
    TEST_ASSERT_EQUAL_UINT8(0xA1, cmds[2].mac[0]);
}

void test_rssi_sort_unknown_rssi_sorts_last() {
    std::vector<lamp_test::Peer> peers = {
        {{0xB1, 0, 0, 0, 0, 0}, -50},
        {{0xB2, 0, 0, 0, 0, 0}, -127},
        {{0xB3, 0, 0, 0, 0, 0}, -70},
    };
    auto cmds = lamp_test::computeCascadeCommands(peers, 50);
    TEST_ASSERT_EQUAL_UINT8(0xB1, cmds[0].mac[0]);
    TEST_ASSERT_EQUAL_UINT8(0xB3, cmds[1].mac[0]);
    TEST_ASSERT_EQUAL_UINT8(0xB2, cmds[2].mac[0]);
}

// --- delayMs clamp ---

void test_delay_clamps_at_kMaxDelayMs() {
    // 15 peers at staggerMs=1000: peer at rank 14 computes 15*1000=15000 > 10000.
    std::vector<lamp_test::Peer> peers;
    for (size_t i = 0; i < 15; ++i) {
        lamp_test::Peer p{};
        p.mac[0] = static_cast<uint8_t>(i);
        p.lastRssi = static_cast<int8_t>(-30 - static_cast<int>(i));
        peers.push_back(p);
    }
    auto cmds = lamp_test::computeCascadeCommands(peers, 1000);
    for (const auto& c : cmds) {
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(lamp_test::kMaxDelayMs, c.delayMs);
    }
    // Peer at rank 9 computes 10*1000=10000 (exactly at cap).
    TEST_ASSERT_EQUAL_UINT32(lamp_test::kMaxDelayMs, cmds[9].delayMs);
    // Peers at rank 10+ are clamped.
    TEST_ASSERT_EQUAL_UINT32(lamp_test::kMaxDelayMs, cmds[14].delayMs);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_fanout_three_peers_correct_delays);
    RUN_TEST(test_fanout_one_peer_fires_after_stagger_not_zero);
    RUN_TEST(test_fanout_no_cap_15_peers);
    RUN_TEST(test_cascade_disabled_issues_no_commands);
    RUN_TEST(test_cascade_enabled_issues_commands);
    RUN_TEST(test_received_command_does_not_cascade);
    RUN_TEST(test_local_trigger_with_cascade_enabled_does_cascade);
    RUN_TEST(test_rssi_sort_strongest_first);
    RUN_TEST(test_rssi_sort_unknown_rssi_sorts_last);
    RUN_TEST(test_delay_clamps_at_kMaxDelayMs);

    return UNITY_END();
}
